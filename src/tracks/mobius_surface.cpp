//  MinkowskiKart - a fun racing game with go-kart
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 3
//  of the License, or (at your option) any later version.

#include "tracks/mobius_surface.hpp"

#include "tracks/track.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace MobiusSurface
{

namespace
{
const btScalar PI = btScalar(3.14159265358979323846f);
const btScalar TWO_PI = btScalar(2.0f) * PI;

btScalar clampScalar(btScalar value, btScalar minimum, btScalar maximum)
{
    return std::max(minimum, std::min(maximum, value));
}   // clampScalar

bool isFiniteVector(const btVector3& v)
{
    return std::isfinite((double)v.x()) &&
           std::isfinite((double)v.y()) &&
           std::isfinite((double)v.z());
}   // isFiniteVector

void refineParameters(const btVector3& position, btScalar* u, btScalar* v,
                      int refinements)
{
    for (int refinement = 0; refinement < refinements; refinement++)
    {
        const btVector3 p = point(*u, *v);
        const btVector3 residual = p - position;
        const btVector3 t_u = du(*u, *v);
        const btVector3 t_v = dv(*u);
        const btScalar du_len2 = std::max(t_u.length2(), btScalar(1.0e-6f));
        const btScalar dv_len2 = std::max(t_v.length2(), btScalar(1.0e-6f));
        const btScalar u_step = clampScalar(
            residual.dot(t_u) / du_len2, btScalar(-0.12f), btScalar(0.12f));
        const btScalar v_step = clampScalar(
            residual.dot(t_v) / dv_len2, btScalar(-0.85f), btScalar(0.85f));
        *u -= u_step;
        *v -= v_step;
        wrapParameters(u, v);
    }
}   // refineParameters

}   // anonymous namespace

// ----------------------------------------------------------------------------
bool isActive()
{
    static const Track* last_track = NULL;
    static bool active = false;
    const Track* track = Track::getCurrentTrack();
    if (track != last_track)
    {
        last_track = track;
        active = track && track->getIdent() == "mobius_track";
    }
    return active;
}   // isActive

// ----------------------------------------------------------------------------
btVector3 point(btScalar u, btScalar v)
{
    const btScalar cu = btCos(u);
    const btScalar su = btSin(u);
    const btScalar ch = btCos(u * btScalar(0.5f));
    return btVector3((RADIUS + v * ch) * cu,
                     v * btSin(u * btScalar(0.5f)),
                     (RADIUS + v * ch) * su);
}   // point

// ----------------------------------------------------------------------------
btVector3 du(btScalar u, btScalar v)
{
    const btScalar cu = btCos(u);
    const btScalar su = btSin(u);
    const btScalar ch = btCos(u * btScalar(0.5f));
    const btScalar sh = btSin(u * btScalar(0.5f));
    const btScalar radial = RADIUS + v * ch;
    const btScalar dr = btScalar(-0.5f) * v * sh;
    return btVector3(dr * cu - radial * su,
                     btScalar(0.5f) * v * ch,
                     dr * su + radial * cu);
}   // du

// ----------------------------------------------------------------------------
btVector3 dv(btScalar u)
{
    const btScalar cu = btCos(u);
    const btScalar su = btSin(u);
    const btScalar ch = btCos(u * btScalar(0.5f));
    const btScalar sh = btSin(u * btScalar(0.5f));
    return btVector3(ch * cu, sh, ch * su);
}   // dv

// ----------------------------------------------------------------------------
void wrapParameters(btScalar* u, btScalar* v)
{
    while (*u < btScalar(0.0f))
    {
        *u += TWO_PI;
        *v = -*v;
    }
    while (*u >= TWO_PI)
    {
        *u -= TWO_PI;
        *v = -*v;
    }
    *v = clampScalar(*v, -ROAD_HALF_WIDTH, ROAD_HALF_WIDTH);
}   // wrapParameters

// ----------------------------------------------------------------------------
Query evaluate(const btVector3& position, const btVector3& side_hint,
               btScalar u, btScalar v)
{
    Query query;
    query.m_u = u;
    query.m_v = v;
    query.m_closest_point = point(u, v);
    query.m_distance2 = (position - query.m_closest_point).length2();

    btVector3 surface_normal = du(u, v).cross(dv(u));
    if (surface_normal.length2() > btScalar(1.0e-12f))
        surface_normal.normalize();
    else
        surface_normal = btVector3(0.0f, 1.0f, 0.0f);

    btVector3 hint = side_hint;
    if (hint.length2() < btScalar(1.0e-6f))
        hint = position - query.m_closest_point;
    if (hint.length2() < btScalar(1.0e-6f))
        hint = surface_normal;

    if (surface_normal.dot(hint) < btScalar(0.0f))
        surface_normal = -surface_normal;

    query.m_surface_normal = surface_normal;
    return query;
}   // evaluate

// ----------------------------------------------------------------------------
bool solveContinuation(const btVector3& position, const btVector3& side_hint,
                       btScalar start_u, btScalar start_v, Query* query)
{
    if (!query)
        return false;

    btScalar u = start_u;
    btScalar v = start_v;
    wrapParameters(&u, &v);
    refineParameters(position, &u, &v, 6);
    *query = evaluate(position, side_hint, u, v);
    return isFiniteVector(query->m_closest_point) &&
           isFiniteVector(query->m_surface_normal);
}   // solveContinuation

// ----------------------------------------------------------------------------
bool solveGlobal(const btVector3& position, const btVector3& side_hint,
                 Query* query)
{
    if (!query)
        return false;

    btScalar base_u = btAtan2(position.z(), position.x());
    if (base_u < btScalar(0.0f))
        base_u += TWO_PI;

    Query best_query;
    btScalar best_distance2 = std::numeric_limits<btScalar>::max();

    for (int seed = 0; seed < 16; seed++)
    {
        btScalar u = base_u + TWO_PI * btScalar(seed) / btScalar(16.0f);
        btScalar v = btScalar(0.0f);
        wrapParameters(&u, &v);

        const btScalar cu = btCos(u);
        const btScalar su = btSin(u);
        const btVector3 center(RADIUS * cu, btScalar(0.0f), RADIUS * su);
        v = clampScalar((position - center).dot(dv(u)),
                        -ROAD_HALF_WIDTH, ROAD_HALF_WIDTH);

        refineParameters(position, &u, &v, 6);
        const Query candidate = evaluate(position, side_hint, u, v);
        if (candidate.m_distance2 < best_distance2)
        {
            best_distance2 = candidate.m_distance2;
            best_query = candidate;
        }
    }

    *query = best_query;
    return best_distance2 < std::numeric_limits<btScalar>::max() &&
           isFiniteVector(query->m_closest_point) &&
           isFiniteVector(query->m_surface_normal);
}   // solveGlobal

}   // namespace MobiusSurface
