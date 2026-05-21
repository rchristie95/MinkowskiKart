//
//  MinkowskiKart - a fun racing game with go-kart
//  Copyright (C) 2007-2015 Joerg Henrichs
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 3
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#include "tracks/terrain_info.hpp"

#include "graphics/material.hpp"
#include "physics/triangle_mesh.hpp"
#include "race/race_manager.hpp"
#include "tracks/track.hpp"
#include "tracks/track_manager.hpp"
#include "tracks/track_object_manager.hpp"
#include "utils/constants.hpp"

#include <math.h>

namespace
{
bool isMatchingGhostZipperHit(const Vec3 &hit_point,
                              const Vec3 &surface_normal,
                              const Vec3 &gfx_hit_point,
                              const Material *gfx_material,
                              const Vec3 &gfx_normal)
{
    if (!gfx_material || !gfx_material->isZipper())
        return false;

    Vec3 normal = surface_normal;
    if (normal.length2() < 0.0001f)
        normal = Vec3(0.0f, 1.0f, 0.0f);
    else
        normal.normalize();

    Vec3 zipper_normal = gfx_normal;
    if (zipper_normal.length2() < 0.0001f)
        return false;
    zipper_normal.normalize();

    const float normal_alignment = zipper_normal.dot(normal);
    const float surface_offset = (gfx_hit_point - hit_point).dot(normal);
    return normal_alignment > 0.65f &&
           surface_offset > -0.05f &&
           surface_offset < 0.35f;
}

bool applyGhostZipperMaterialAtSurface(const Vec3 &hit_point,
                                       const Vec3 &surface_normal,
                                       const Material **material)
{
    if (!material || !*material || (*material)->isZipper())
        return false;

    Vec3 normal = surface_normal;
    if (normal.length2() < 0.0001f)
        normal = Vec3(0.0f, 1.0f, 0.0f);
    else
        normal.normalize();

    const TriangleMesh &gfx_tm = Track::getCurrentTrack()->getGFXEffectMesh();
    Vec3 gfx_hit_point;
    const Material *gfx_material = NULL;
    Vec3 gfx_normal;

    const Vec3 probe_from = hit_point + normal * 0.75f;
    const Vec3 probe_to   = hit_point - normal * 0.35f;
    if (gfx_tm.castRay(probe_from, probe_to, &gfx_hit_point, &gfx_material,
                       &gfx_normal, /*interpolate*/true) &&
        isMatchingGhostZipperHit(hit_point, normal, gfx_hit_point,
                                 gfx_material, gfx_normal))
    {
        *material = gfx_material;
        return true;
    }

    if (gfx_tm.castRay(probe_to, probe_from, &gfx_hit_point, &gfx_material,
                       &gfx_normal, /*interpolate*/true) &&
        isMatchingGhostZipperHit(hit_point, normal, gfx_hit_point,
                                 gfx_material, gfx_normal))
    {
        *material = gfx_material;
        return true;
    }

    return false;
}
}

/** Constructor to initialise terrain data.
 */
TerrainInfo::TerrainInfo()
{
    m_last_material = NULL;
    m_material      = NULL;
}   // TerrainInfo

//-----------------------------------------------------------------------------
/** Constructor to initialise terrain data at a given position
 *  \param pos The position to get the data from.
 */
TerrainInfo::TerrainInfo(const Vec3 &pos)
{
    // initialise HoT
    m_last_material = NULL;
    m_material = NULL;
    update(pos);
}   // TerrainInfo

//-----------------------------------------------------------------------------
/** Update the terrain information based on the latest position.
 *  \param Position from which to start the raycast from.
 */
void TerrainInfo::update(const Vec3 &from)
{
    m_last_material = m_material;
    btVector3 to(from);
    to.setY(-10000.0f);

    const TriangleMesh &tm = Track::getCurrentTrack()->getTriangleMesh();
    tm.castRay(from, to, &m_hit_point, &m_material, &m_normal,
               /*interpolate*/false);
    // Now also raycast against all track objects (that are driveable).
    Track::getCurrentTrack()->getTrackObjectManager()
                     ->castRay(from, to, &m_hit_point, &m_material,
                               &m_normal, /*interpolate*/false);
    applyGhostZipperMaterialAtSurface(m_hit_point, m_normal, &m_material);
}   // update

//-----------------------------------------------------------------------------
/** Update the terrain information based on the latest position.
 *  \param tran The transform ov the kart
 *  \param from World coordinates from which to start the raycast.
 */
void TerrainInfo::update(const btMatrix3x3 &rotation, const Vec3 &from)
{
    m_last_material = m_material;
    // Save the origin for debug drawing
    m_origin_ray    = from;

    // Compute the 'to' vector by rotating a long 'down' vectory by the
    // kart rotation, and adding the start point to it.
    btVector3 to(0, -10000.0f, 0);
    to = from + rotation*to;

    const TriangleMesh &tm = Track::getCurrentTrack()->getTriangleMesh();
    tm.castRay(from, to, &m_hit_point, &m_material, &m_normal,
               /*interpolate*/true);
    // Now also raycast against all track objects (that are driveable). If
    // there should be a closer result (than the one against the main track
    // mesh), its data will be returned.
    Track::getCurrentTrack()->getTrackObjectManager()
                            ->castRay(from, to, &m_hit_point, &m_material,
                                      &m_normal, /*interpolate*/true);

    applyGhostZipperMaterialAtSurface(m_hit_point, m_normal, &m_material);
}   // update
//-----------------------------------------------------------------------------
/** Update the terrain information based on the latest position.
*  \param Position from which to start the rayast from.
*/
void TerrainInfo::update(const Vec3 &from, const Vec3 &towards)
{
    m_last_material = m_material;
    Vec3 direction = towards.normalized();
    btVector3 to = from + 10000.0f*direction;

    const TriangleMesh &tm = Track::getCurrentTrack()->getTriangleMesh();
    tm.castRay(from, to, &m_hit_point, &m_material, &m_normal);
}   // update

// -----------------------------------------------------------------------------
/** Does a raycast upwards from the given position
If the raycast indicated that the kart is 'under something' (i.e. a
 *  specially marked terrain), to another raycast up to detect under whic
 *  mesh the kart is. This is using the special gfx effect mesh only.
 *  This is used e.g. to detect if a kart is under water, and then to
 *  get the proper position for water effects. Note that the TerrainInfo
 *  objects keeps track of the previous raycast position.
 */
bool TerrainInfo::getSurfaceInfo(const Vec3 &from, Vec3 *position,
                                 const Material **material)
{
    Vec3 to=from+Vec3(0, 10000, 0);
    const TriangleMesh &tm = Track::getCurrentTrack()->getGFXEffectMesh();
    return tm.castRay(from, to, position, material);
}   // getSurfaceInfo

// -----------------------------------------------------------------------------
/** Returns the pitch of the terrain depending on the heading
*/
float TerrainInfo::getTerrainPitch(float heading) const {
    if(!m_material) return 0.0f;

    const float X = sinf(heading);
    const float Z = cosf(heading);
    // Compute the angle between the normal of the plane and the line to
    // (x,0,z).  (x,0,z) is normalised, so are the coordinates of the plane,
    // simplifying the computation of the scalar product.
    float pitch = ( m_normal.getX()*X + m_normal.getZ()*Z );  // use (x, 0, z)

    // The actual angle computed above is between the normal and the (x, 0, z)
    // line, so to compute the actual angles 90 degrees must be subtracted.
    pitch = -acosf(pitch) + NINETY_DEGREE_RAD;
    return pitch;
}   // getTerrainPitch
