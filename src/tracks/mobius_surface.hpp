//  MinkowskiKart - a fun racing game with go-kart
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 3
//  of the License, or (at your option) any later version.

#ifndef HEADER_MOBIUS_SURFACE_HPP
#define HEADER_MOBIUS_SURFACE_HPP

#include "LinearMath/btVector3.h"

/** Single shared implementation of the analytic Möbius strip surface used
 *  for custom gravity on mobius_track. Keep the constants in sync with
 *  BlenderConversionScripts/generate_mobius_track.py (RADIUS,
 *  ROAD_HALF_WIDTH); they are intentionally defined exactly once on the
 *  C++ side, here. */
namespace MobiusSurface
{
    const btScalar RADIUS = btScalar(82.0f);
    const btScalar ROAD_HALF_WIDTH = btScalar(8.0f);

    struct Query
    {
        btVector3 m_closest_point;
        btVector3 m_surface_normal;   // unit, oriented toward the query side
        btScalar  m_u;
        btScalar  m_v;
        btScalar  m_distance2;
    };

    // True when the current track uses the Möbius surface gravity. Cached
    // per track, so it is cheap to call every frame.
    bool isActive();

    btVector3 point(btScalar u, btScalar v);
    btVector3 du(btScalar u, btScalar v);
    btVector3 dv(btScalar u);
    void wrapParameters(btScalar* u, btScalar* v);

    // Newton-refines (u, v) toward the closest surface point and fills the
    // query. side_hint orients the returned normal: it points into the
    // half-space of side_hint (typically the body position relative to the
    // surface, or the chassis up vector when resting on the surface).
    Query evaluate(const btVector3& position, const btVector3& side_hint,
                   btScalar u, btScalar v);

    // Warm-started solve from a previous (u, v). Returns false when the
    // result is not finite.
    bool solveContinuation(const btVector3& position,
                           const btVector3& side_hint,
                           btScalar start_u, btScalar start_v, Query* query);

    // Global 16-seed solve. Returns false when no finite solution exists.
    bool solveGlobal(const btVector3& position, const btVector3& side_hint,
                     Query* query);
}

#endif
