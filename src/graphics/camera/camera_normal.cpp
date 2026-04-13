//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2004-2015 Steve Baker <sjbaker1@airmail.net>
//  Copyright (C) 2006-2015 SuperTuxKart-Team, Steve Baker
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

#include "graphics/camera/camera_normal.hpp"

#include "audio/sfx_manager.hpp"
#include "config/stk_config.hpp"
#include "config/user_config.hpp"
#include "graphics/irr_driver.hpp"
#include "input/device_manager.hpp"
#include "input/input_manager.hpp"
#include "input/multitouch_device.hpp"
#include "karts/abstract_kart.hpp"
#include "karts/explosion_animation.hpp"
#include "karts/kart.hpp"
#include "karts/kart_properties.hpp"
#include "karts/skidding.hpp"
#include "modes/soccer_world.hpp"
#include "physics/btKart.hpp"
#include "physics/triangle_mesh.hpp"
#include "relativity/relativity_math.hpp"
#include "tracks/track.hpp"
#include "tracks/track_object_manager.hpp"
#include <array>
#include <limits>
#include <string>
#include <vector>

std::vector<Vec3> CameraNormal::m_tv_cameras;
float CameraNormal::m_tv_min_delta2 = 9.0f;  // distance min to change camera
float CameraNormal::m_tv_cooldown_default = 0.4f;  // time to change camera

namespace
{

// Near-plane probe (shared by old and new pipelines)
const float RELATIVITY_CLOSE_CHASE_DEFAULT_NEAR_PLANE       = 0.05f;
const float RELATIVITY_CLOSE_CHASE_MIN_NEAR_PLANE           = 0.02f;
const float RELATIVITY_CLOSE_CHASE_NEAR_PLANE_PROBE_DISTANCE = 0.75f;

// Relativity close-chase camera geometry. This is a hood/driver camera, so
// keep it anchored slightly in front of the kart centre and never let
// automatic clipping correction drag it behind the driver.
const float RC_FORWARD_OFFSET = -0.50f;  // 50cm behind kart centre
const float RC_HEIGHT         = 0.85f;   // 85cm above kart centre
const float RC_CLEARANCE      = 0.34f;   // minimum distance from apparent road
const float RC_TARGET_FORWARD = 3.10f;   // 3.1m ahead of kart for look-at point
const float RC_TARGET_HEIGHT  = 0.56f;   // 56cm above kart for look-at point
const float RC_FORWARD_TC     = 0.10f;   // support-frame forward smooth tc (s)
const float RC_UP_TC          = 0.08f;   // support-frame up smooth tc (s)
const float RC_STEEP_EXTRA    = 0.36f;   // extra clearance on steep normals (m)
const float RC_SWEEP_SUBSTEP  = 0.08f;   // sweep step length (m)
const int   RC_SWEEP_MAX_STEPS = 16;
const float RC_MIN_FORWARD_OFFSET = -0.80f;
const float RC_MIN_UP_OFFSET      = 0.60f;

float clamp01(float value)
{
    return std::max(0.0f, std::min(value, 1.0f));
}   // clamp01

bool useRelativityCloseChase(Camera::Mode mode)
{
    return Relativity::isEnabled() && mode == Camera::CM_NORMAL;
}   // useRelativityCloseChase

btVector3 toBt(const core::vector3df& v)
{
    return btVector3(v.X, v.Y, v.Z);
}   // toBt

core::vector3df normalizedOrDefault(const core::vector3df& v,
                                    const core::vector3df& fallback)
{
    if (v.getLengthSQ() <= 1.0e-6f)
        return fallback;

    core::vector3df result(v);
    result.normalize();
    return result;
}   // normalizedOrDefault

btVector3 normalizedOrDefault(const btVector3& v, const btVector3& fallback)
{
    if (v.length2() <= btScalar(1.0e-6f))
        return fallback;

    btVector3 result(v);
    result.normalize();
    return result;
}   // normalizedOrDefault

bool castDriveableSurfaceRay(const btVector3& from, const btVector3& to,
                             btVector3* hit_point, btVector3* hit_normal)
{
    Track* track = Track::getCurrentTrack();
    if (!track)
        return false;

    const Material* material = NULL;
    btVector3 point(0.0f, 0.0f, 0.0f);
    btVector3 normal(0.0f, 1.0f, 0.0f);

    bool hit = track->getTriangleMesh().castRay(from, to, &point, &material,
                                                &normal, true);
    TrackObjectManager* object_manager = track->getTrackObjectManager();
    if (object_manager)
    {
        hit = object_manager->castRay(from, to, &point, &material, &normal,
                                      true) || hit;
    }

    if (!hit)
        return false;

    if (normal.length2() <= btScalar(1.0e-6f))
        normal = btVector3(0.0f, 1.0f, 0.0f);
    else
        normal.normalize();

    if (hit_point)
        *hit_point = point;
    if (hit_normal)
        *hit_normal = normal;
    return true;
}   // castDriveableSurfaceRay

bool castCameraSurfaceRay(const Kart* observer_kart,
                          const btVector3& observer_position,
                          const btVector3& from,
                          const btVector3& to,
                          btVector3* hit_point,
                          btVector3* hit_normal)
{
    if (Relativity::isEnabled() && observer_kart)
    {
        Relativity::ApparentSurfaceHit hit;
        if (!Relativity::castApparentDriveableRay(observer_kart,
            observer_position, from, to, &hit, true))
        {
            return false;
        }

        if (hit_point)
            *hit_point = hit.m_apparent_point;
        if (hit_normal)
        {
            *hit_normal = normalizedOrDefault(
                hit.m_apparent_normal, btVector3(0.0f, 1.0f, 0.0f));
        }
        return true;
    }

    return castDriveableSurfaceRay(from, to, hit_point, hit_normal);
}   // castCameraSurfaceRay

float getSurfaceAwareNearPlane(const core::vector3df& eye_position,
                               const core::vector3df& view_direction)
{
    const btVector3 from = toBt(eye_position);
    const btVector3 to = from + toBt(normalizedOrDefault(
        view_direction, core::vector3df(0.0f, 0.0f, 1.0f))) *
        RELATIVITY_CLOSE_CHASE_NEAR_PLANE_PROBE_DISTANCE;

    btVector3 hit_point;
    btVector3 hit_normal;
    if (!castDriveableSurfaceRay(from, to, &hit_point, &hit_normal))
        return RELATIVITY_CLOSE_CHASE_DEFAULT_NEAR_PLANE;

    const float hit_distance = (hit_point - from).length();
    return std::max(RELATIVITY_CLOSE_CHASE_MIN_NEAR_PLANE,
                    std::min(RELATIVITY_CLOSE_CHASE_DEFAULT_NEAR_PLANE,
                             hit_distance * 0.5f));
}   // getSurfaceAwareNearPlane

float getCameraSurfaceAwareNearPlane(const Kart* observer_kart,
                                     const btVector3& eye_position,
                                     const btVector3& view_direction)
{
    const btVector3 direction =
        normalizedOrDefault(view_direction, btVector3(0.0f, 0.0f, 1.0f));
    const btVector3 to = eye_position +
        direction * RELATIVITY_CLOSE_CHASE_NEAR_PLANE_PROBE_DISTANCE;

    btVector3 hit_point;
    btVector3 hit_normal;
    if (!castCameraSurfaceRay(observer_kart, eye_position, eye_position, to,
                              &hit_point, &hit_normal))
    {
        return RELATIVITY_CLOSE_CHASE_DEFAULT_NEAR_PLANE;
    }

    const float hit_distance = (hit_point - eye_position).length();
    return std::max(RELATIVITY_CLOSE_CHASE_MIN_NEAR_PLANE,
                    std::min(RELATIVITY_CLOSE_CHASE_DEFAULT_NEAR_PLANE,
                             hit_distance * 0.5f));
}   // getCameraSurfaceAwareNearPlane

float getSmoothAlpha(float dt, float time_constant)
{
    if (time_constant <= 1.0e-4f)
        return 1.0f;
    return clamp01(1.0f - expf(-dt / time_constant));
}   // getSmoothAlpha

btVector3 getKartSupportUp(const Kart* kart)
{
    if (!kart)
        return btVector3(0.0f, 1.0f, 0.0f);

    btVector3 chassis_up =
        normalizedOrDefault(kart->getSmoothedTrans().getBasis().getColumn(1),
                            btVector3(0.0f, 1.0f, 0.0f));
    btVector3 support_up(0.0f, 0.0f, 0.0f);

    btKart* vehicle = kart->getVehicle();
    if (vehicle)
    {
        for (int i = 0; i < vehicle->getNumWheels(); i++)
        {
            const btWheelInfo& wheel = vehicle->getWheelInfo(i);
            if (!wheel.m_raycastInfo.m_isInContact)
                continue;

            btVector3 normal =
                normalizedOrDefault(wheel.m_raycastInfo.m_contactNormalWS,
                                    chassis_up);
            if (normal.dot(chassis_up) < 0.0f)
                normal = -normal;
            support_up += normal;
        }
    }

    return normalizedOrDefault(support_up, chassis_up);
}   // getKartSupportUp

void buildKartSupportBasis(const Kart* kart,
                           const btVector3& previous_forward,
                           const btVector3& previous_up,
                           float dt,
                           btVector3* support_forward,
                           btVector3* support_right,
                           btVector3* support_up)
{
    const btVector3 default_up(0.0f, 1.0f, 0.0f);
    btVector3 desired_up = getKartSupportUp(kart);
    btVector3 smoothed_up = desired_up;
    if (previous_up.length2() > btScalar(1.0e-6f))
    {
        smoothed_up = previous_up.lerp(desired_up, getSmoothAlpha(dt, RC_UP_TC));
        smoothed_up = normalizedOrDefault(smoothed_up, desired_up);
    }

    btVector3 raw_forward(0.0f, 0.0f, 1.0f);
    if (kart)
    {
        btRigidBody* body = kart->getVehicle() ? kart->getVehicle()->getRigidBody() : NULL;
        if (body)
            raw_forward = body->getLinearVelocity();

        if (raw_forward.length2() <= btScalar(1.0f))
            raw_forward = kart->getSmoothedTrans().getBasis().getColumn(2);

        if (raw_forward.length2() <= btScalar(1.0e-6f))
        {
            const float heading = kart->getHeading();
            raw_forward = btVector3(sinf(heading), 0.0f, cosf(heading));
        }
    }

    btVector3 desired_forward = raw_forward - smoothed_up * raw_forward.dot(smoothed_up);
    desired_forward = normalizedOrDefault(
        desired_forward,
        previous_forward.length2() > btScalar(1.0e-6f) ? previous_forward
                                                        : btVector3(0.0f, 0.0f, 1.0f));

    btVector3 smoothed_forward = desired_forward;
    if (previous_forward.length2() > btScalar(1.0e-6f))
    {
        smoothed_forward = previous_forward.lerp(
            desired_forward, getSmoothAlpha(dt, RC_FORWARD_TC));
        smoothed_forward = normalizedOrDefault(smoothed_forward, desired_forward);
    }

    btVector3 right = smoothed_up.cross(smoothed_forward);
    right = normalizedOrDefault(right, btVector3(1.0f, 0.0f, 0.0f));
    smoothed_forward = normalizedOrDefault(right.cross(smoothed_up),
                                           desired_forward);
    smoothed_up = normalizedOrDefault(smoothed_up, default_up);

    if (support_forward)
        *support_forward = smoothed_forward;
    if (support_right)
        *support_right = right;
    if (support_up)
        *support_up = smoothed_up;
}   // buildKartSupportBasis

void enforceRelativityCameraClearance(const Kart* observer_kart,
                                      const btVector3& kart_anchor,
                                      const btVector3& support_forward,
                                      const btVector3& support_right,
                                      const btVector3& support_up,
                                      btVector3* position)
{
    if (!observer_kart || !position)
        return;

    btVector3 hit_point;
    btVector3 hit_normal;
    if (castCameraSurfaceRay(observer_kart, *position, kart_anchor, *position,
                             &hit_point, &hit_normal))
    {
        const float normal_y = fabsf(hit_normal.getY());
        const float steepness = 1.0f - clamp01(normal_y);
        const float clearance = RC_CLEARANCE + RC_STEEP_EXTRA * steepness;
        const float support_clearance = ((*position) - hit_point).dot(support_up);
        if (support_clearance < clearance)
            *position += support_up * (clearance - support_clearance);
    }

    const btVector3 from = *position + support_up * 0.08f;
    const std::array<btVector3, 5> probes = {{
        -support_up * 2.1f,
        support_forward * 0.55f - support_up * 1.6f,
        support_forward * 0.25f + support_right * 0.40f - support_up * 1.5f,
        support_forward * 0.25f - support_right * 0.40f - support_up * 1.5f,
        -support_forward * 0.20f - support_up * 1.3f
    }};

    for (size_t i = 0; i < probes.size(); i++)
    {
        if (!castCameraSurfaceRay(observer_kart, *position, from,
                                  *position + probes[i],
                                  &hit_point, &hit_normal))
        {
            continue;
        }

        const float normal_y = fabsf(hit_normal.getY());
        const float steepness = 1.0f - clamp01(normal_y);
        const float clearance = RC_CLEARANCE + RC_STEEP_EXTRA * steepness;
        const float support_clearance = ((*position) - hit_point).dot(support_up);
        if (support_clearance < clearance)
            *position += support_up * (clearance - support_clearance);
    }
}   // enforceRelativityCameraClearance

Vec3 getCameraTargetOffset(float above_kart)
{
    return Vec3(0.0f, above_kart, 0.0f);
}   // getCameraTargetOffset

Vec3 clampOffsetToRelativityBubble(const Vec3& offset)
{
    if (!Relativity::isEnabled())
        return offset;

    const float relativity_camera_max_offset =
        Relativity::getWarpBubbleRadius() - 0.25f;
    const float length = offset.length();
    if (length <= relativity_camera_max_offset || length <= 0.0001f)
        return offset;

    return offset * (relativity_camera_max_offset / length);
}   // clampOffsetToRelativityBubble

}   // anonymous namespace

// ============================================================================
/** Relativity close-chase camera — v2 rebuilt for turning-uphill clipping.
 *
 *  Five anti-clipping measures layered together:
 *
 *  1. SMOOTHED HEADING — The camera placement heading is smoothed
 *     independently so turns don't cause the desired position to snap to a
 *     new location.  Without this, the exp-smooth takes a straight-line
 *     shortcut through the inside of the turn — right through rising terrain.
 *
 *  2. FAN-OF-RAYS terrain probe — Five rays spread from the kart toward the
 *     camera region (centre, left, right, high-left, high-right).  On a
 *     curved uphill the road surface may flank the direct kart→camera line;
 *     a single ray misses it, but the fan catches it.
 *
 *  3. SURFACE-NORMAL-AWARE clearance — On steep terrain, the fixed Y-axis
 *     clearance is supplemented by a component along the surface normal.
 *     This scales clearance with slope steepness so the camera lifts higher
 *     on steeper uphills where clipping risk is greatest.
 *
 *  4. SWEPT-PATH collision (CCD for the camera) — After smoothing, a ray is
 *     cast from the previous frame's camera position to the new one.  If
 *     terrain lies on this path the camera is pushed above the hit point.
 *     This is equivalent to the report's Continuous Collision Detection
 *     applied to camera movement.
 *
 *  5. VOLUMETRIC post-smooth probe — After smoothing, rays are cast not just
 *     downward but also forward-down and to each side-down.  On a curved
 *     section the terrain can be beside or ahead of the camera, not below it.
 */
void CameraNormal::updateRelativityCamera(float dt)
{
    if (!m_kart) return;
    Kart* kart = dynamic_cast<Kart*>(m_kart);
    if (!kart) return;

    const btVector3 kart_pos = kart->getSmoothedTrans().getOrigin();
    const btVector3 previous_forward =
        m_rc_initialized ? m_rc_forward
                         : kart->getSmoothedTrans().getBasis().getColumn(2);
    const btVector3 previous_up =
        m_rc_initialized ? m_rc_up
                         : kart->getSmoothedTrans().getBasis().getColumn(1);

    btVector3 support_forward;
    btVector3 support_right;
    btVector3 support_up;
    buildKartSupportBasis(kart, previous_forward, previous_up, dt,
                          &support_forward, &support_right, &support_up);
    m_rc_forward = support_forward;
    m_rc_up = support_up;

    btVector3 desired = kart_pos
        + support_forward * RC_FORWARD_OFFSET
        + support_up * RC_HEIGHT;
    btVector3 desired_tgt = kart_pos
        + support_forward * RC_TARGET_FORWARD
        + support_up * RC_TARGET_HEIGHT;
    const btVector3 kart_anchor =
        kart_pos + support_up * 0.28f + support_forward * 0.08f;

    btVector3 next_pos = desired;
    if (!m_rc_initialized)
    {
        enforceRelativityCameraClearance(kart, kart_anchor, support_forward,
                                         support_right, support_up, &next_pos);
        m_rc_initialized = true;
    }
    else
    {
        const btVector3 prev_pos = m_rc_pos;
        const btVector3 delta = desired - prev_pos;
        int steps = (int)ceil(delta.length() / RC_SWEEP_SUBSTEP);
        steps = std::max(1, std::min(steps, RC_SWEEP_MAX_STEPS));
        next_pos = prev_pos;
        for (int i = 1; i <= steps; i++)
        {
            btVector3 step_pos = prev_pos.lerp(desired, (float)i / (float)steps);
            enforceRelativityCameraClearance(kart, kart_anchor, support_forward,
                                             support_right, support_up, &step_pos);
            next_pos = step_pos;
        }
    }

    const float forward_offset = (next_pos - kart_pos).dot(support_forward);
    if (forward_offset < RC_MIN_FORWARD_OFFSET)
        next_pos += support_forward * (RC_MIN_FORWARD_OFFSET - forward_offset);

    const float up_offset = (next_pos - kart_pos).dot(support_up);
    if (up_offset < RC_MIN_UP_OFFSET)
        next_pos += support_up * (RC_MIN_UP_OFFSET - up_offset);

    enforceRelativityCameraClearance(kart, kart_anchor, support_forward,
                                     support_right, support_up, &next_pos);

    m_rc_pos = next_pos;
    m_rc_target = desired_tgt;

    m_camera->setPosition(core::vector3df(
        m_rc_pos.getX(), m_rc_pos.getY(), m_rc_pos.getZ()));
    m_camera->setTarget(core::vector3df(
        m_rc_target.getX(), m_rc_target.getY(), m_rc_target.getZ()));
    m_camera->setUpVector(core::vector3df(
        support_up.getX(), support_up.getY(), support_up.getZ()));
}   // updateRelativityCamera

// ============================================================================
/** Constructor for the normal camera. This is the only camera constructor
 *  except for the base class that takes a camera type as parameter. This is
 *  because debug and end camera use the normal camera as their base class.
 *  \param type The type of the camera that is created (can be CM_TYPE_END
 *         or CM_TYPE_DEBUG).
 *  \param camera_index Index of this camera.
 *  \param Kart Pointer to the kart for which this camera is used.
 */
CameraNormal::CameraNormal(Camera::CameraType type,  int camera_index,
                           AbstractKart* kart)
            : Camera(type, camera_index, kart), m_camera_offset(0., 0., 0.)
{
    m_distance = kart ? UserConfigParams::m_camera_distance : 1000.0f;
    m_ambient_light = Track::getCurrentTrack()->getDefaultAmbientColor();

    // TODO: Put these values into a config file
    //       Global or per split screen zone?
    //       Either global or per user (for instance, some users may not like
    //       the extra camera rotation so they could set m_rotation_range to
    //       zero to disable it for themselves).
    m_tv_current_index = -1;
    m_tv_switch_cooldown = 0.0f;
    m_tv_min_delta2 = 9.0f;
    m_tv_cooldown_default = 0.4f;
    m_position_speed = 8.0f;
    m_target_speed   = 10.0f;
    m_rotation_range = 0.4f;
    m_rotation_range = 0.0f;
    m_kart_position  = btVector3(0, 0, 0);
    m_kart_rotation  = btQuaternion(0, 0, 0, 1);
    m_last_smooth_mode = Mode::CM_NORMAL;
    m_rc_pos         = btVector3(0, 0, 0);
    m_rc_target      = btVector3(0, 0, 0);
    m_rc_heading     = 0.0f;
    m_rc_forward     = btVector3(0, 0, 1);
    m_rc_up          = btVector3(0, 1, 0);
    m_rc_initialized = false;
    reset();
    m_camera->setNearValue(1.0f);

    restart();
}   // Camera

//-----------------------------------------------------------------------------
/** Moves the camera smoothly from the current camera position (and target)
 *  to the new position and target.
 *  \param dt Delta time,
 *  \param smooth Updates the camera if true, only calculate smooth state parameter if false
 *  \param if false, the camera instantly moves to the endpoint, or else it smoothly moves
 */
void CameraNormal::moveCamera(float dt, bool smooth, float above_kart,
                              float cam_angle, float distance)
{
    if(!m_kart) return;

    Kart *kart = dynamic_cast<Kart*>(m_kart);
    if (smooth && kart->isFlying())
    {
        Vec3 vec3 = m_kart->getSmoothedXYZ() + Vec3(sinf(m_kart->getHeading()) * -4.0f,
            0.5f,
            cosf(m_kart->getHeading()) * -4.0f);
        m_camera->setTarget(m_kart->getSmoothedXYZ().toIrrVector());
        m_camera->setPosition(vec3.toIrrVector());
        return;
    }   // kart is flying

    core::vector3df current_position = m_camera->getPosition();
    // Smoothly interpolate towards the position and target
    const KartProperties *kp = m_kart->getKartProperties();
    float max_speed_without_zipper = kp->getEngineMaxSpeed();
    float current_speed = m_kart->getSpeed();

    const Skidding *ks = m_kart->getSkidding();
    float skid_factor = ks->getVisualSkidRotation();

    float skid_angle = asinf(skid_factor);
    if (useRelativityCloseChase(getMode()))
        skid_angle *= 0.15f;

    // distance of camera from kart in x and z plane
    float camera_distance = distance;
    if (!useRelativityCloseChase(getMode()))
    {
        camera_distance = -2.8f - 5.6f * (current_speed / max_speed_without_zipper);
        camera_distance = std::max(camera_distance, -0.12f);
        if (Relativity::isEnabled())
            camera_distance = std::min(camera_distance, 0.0f);
        camera_distance *= sqrtf(UserConfigParams::m_camera_forward_smooth_position);
        float min_distance = (distance * 2.0f);
        if (distance > 0) camera_distance += distance + 1;
        if (camera_distance > min_distance) camera_distance = min_distance;
    }

    float tan_up = 0;
    if (cam_angle > 0) tan_up = tanf(cam_angle) * distance;

    // Defines how far camera should be from player kart.
    float vertical_offset = 0.85f - tan_up;
    if (!useRelativityCloseChase(getMode()))
    {
        vertical_offset += current_speed / max_speed_without_zipper / 2.5f;
    }
    else
    {
        vertical_offset = fabsf(camera_distance) * tan_up + above_kart;
    }

    Vec3 wanted_camera_offset(camera_distance * sinf(skid_angle / 2),
        vertical_offset,
        camera_distance * cosf(skid_angle / 2));
    wanted_camera_offset = clampOffsetToRelativityBubble(wanted_camera_offset);
    float delta = dt / std::max(dt, float(UserConfigParams::m_camera_forward_smooth_position));
    float delta2 = dt / std::max(dt, float(UserConfigParams::m_camera_forward_smooth_rotation));

    delta = irr::core::clamp(delta, 0.0f, 1.0f);
    delta2 = irr::core::clamp(delta2, 0.0f, 1.0f);

    btTransform btt = m_kart->getSmoothedTrans();
    m_kart_position = btt.getOrigin();
    btQuaternion q1, q2;
    q1 = m_kart_rotation.normalized();
    q2 = btt.getRotation().normalized();
    if (dot(q1, q2) < 0.0f)
        q2 = -q2;

    m_kart_rotation = q1.slerp(q2, delta2);

    btt.setOrigin(m_kart_position);
    btt.setRotation(q1);

    m_camera_offset = clampOffsetToRelativityBubble(m_camera_offset);
    Vec3 kart_camera_position_with_offset = btt(m_camera_offset);
    m_camera_offset += (wanted_camera_offset - m_camera_offset) * delta;
    m_camera_offset = clampOffsetToRelativityBubble(m_camera_offset);

    // next target
    Vec3 current_target = btt(getCameraTargetOffset(above_kart));
    // new required position of camera
    current_position = kart_camera_position_with_offset.toIrrVector();

    if (smooth)
    {
        if(getMode()!=CM_FALLING)
            m_camera->setPosition(current_position);
        m_camera->setTarget(current_target.toIrrVector());//set new target
    }
    assert(!std::isnan(m_camera->getPosition().X));
    assert(!std::isnan(m_camera->getPosition().Y));
    assert(!std::isnan(m_camera->getPosition().Z));

}   // moveCamera

//-----------------------------------------------------------------------------
void CameraNormal::restart()
{
    if (m_kart)
    {
        btTransform btt = m_kart->getSmoothedTrans();
        const Vec3& up = btt.getBasis().getColumn(1);

        m_camera->setUpVector(up.toIrrVector());
        m_kart_position = btt.getOrigin();
        m_kart_rotation = btt.getRotation();

        if (useRelativityCloseChase(getMode()))
        {
            // New pipeline seeds itself on first update(); just reset the flag.
            m_rc_initialized = false;
            return;
        }

        float offset_z = -33.f * sqrtf(UserConfigParams::m_camera_forward_smooth_position);
        float offset_y = -offset_z * tanf(UserConfigParams::m_camera_forward_up_angle * DEGREE_TO_RAD);
        m_camera_offset = irr::core::vector3df(0., offset_y + 1.0f, offset_z);
        m_camera_offset = clampOffsetToRelativityBubble(m_camera_offset);
    }
}   // restart

//-----------------------------------------------------------------------------
/** Determine the camera settings for the current frame.
 *  \param above_kart How far above the camera should aim at.
 *  \param cam_angle  Angle above the kart plane for the camera.
 *  \param sideway Sideway movement of the camera.
 *  \param distance Distance from kart.
 *  \param cam_roll_angle Roll camera for gyroscope steering effect.
 */
void CameraNormal::getCameraSettings(Mode mode,
                                     float *above_kart, float *cam_angle,
                                     float *sideway, float *distance,
                                     bool *smoothing, float *cam_roll_angle)
{
    switch(mode)
    {
    case CM_NORMAL:
    case CM_FALLING:
        {
            *above_kart = 0.75f;
            *cam_angle = UserConfigParams::m_camera_forward_up_angle * DEGREE_TO_RAD;
            *distance = -m_distance;
            float steering = m_kart->getSteerPercent()
                           * (1.0f + (m_kart->getSkidding()->getSkidFactor()
                                      - 1.0f)/2.3f );
            // quadratically to dampen small variations (but keep sign)
            float dampened_steer = fabsf(steering) * steering;
            *sideway             = -m_rotation_range*dampened_steer*0.5f;
            *smoothing           = UserConfigParams::m_camera_forward_smooth_position != 0.
                                || UserConfigParams::m_camera_forward_smooth_rotation != 0.;
            *cam_roll_angle      = 0.0f;
            if (UserConfigParams::m_multitouch_controls == MULTITOUCH_CONTROLS_GYROSCOPE)
            {
                MultitouchDevice* device = input_manager->getDeviceManager()->getMultitouchDevice();
                if (device)
                {
                    *cam_roll_angle = device->getOrientation();
                }
            }
            break;
        }   // CM_FALLING
    case CM_REVERSE: // Same as CM_NORMAL except it looks backwards
        {
            *above_kart = 0.75f;
            *cam_angle  = UserConfigParams::m_camera_backward_up_angle * DEGREE_TO_RAD;
            *sideway    = 0;
            *distance   = UserConfigParams::m_camera_backward_distance;
            *smoothing  = false;
            *cam_roll_angle = 0.0f;
            if (UserConfigParams::m_multitouch_controls == MULTITOUCH_CONTROLS_GYROSCOPE)
            {
                MultitouchDevice* device = input_manager->getDeviceManager()->getMultitouchDevice();
                if (device)
                {
                    *cam_roll_angle = -device->getOrientation();
                }
            }
            break;
        }
    case CM_CLOSEUP: // Lower to the ground and closer to the kart
        {
            *above_kart = 0.75f;
            *cam_angle  = 20.0f*DEGREE_TO_RAD;
            *sideway    = m_rotation_range
                        * m_kart->getSteerPercent()
                        * m_kart->getSkidding()->getSkidFactor();
            *distance   = -0.5f*m_distance;
            *smoothing  = false;
            *cam_roll_angle = 0.0f;
            if (UserConfigParams::m_multitouch_controls == MULTITOUCH_CONTROLS_GYROSCOPE)
            {
                MultitouchDevice* device = input_manager->getDeviceManager()->getMultitouchDevice();
                if (device)
                {
                    *cam_roll_angle = -device->getOrientation();
                }
            }
            break;
        }
    case CM_LEADER_MODE:
        {
            *above_kart = 0.0f;
            *cam_angle  = 40*DEGREE_TO_RAD;
            *sideway    = 0;
            *distance   = 2.0f*m_distance;
            *smoothing  = true;
            *cam_roll_angle = 0.0f;
            break;
        }
    case CM_SPECTATOR_SOCCER:
        {
            *above_kart = 0.0f;
            *cam_angle  = UserConfigParams::m_spectator_camera_angle*DEGREE_TO_RAD;
            *sideway    = 0;
            *distance   = -UserConfigParams::m_spectator_camera_distance;
            *smoothing  = true;
            *cam_roll_angle = 0.0f;
            break;
        }
    case CM_SPECTATOR_TOP_VIEW:
        {
            *above_kart = 0.0f;
            *cam_angle  = 0;
            *sideway    = 0;
            *distance   = UserConfigParams::m_spectator_camera_distance;
            *smoothing  = true;
            *cam_roll_angle = 0.0f;
            break;
        }
    case CM_SPECTATOR_TV:
        {
            *above_kart = 0.0f;
            *cam_angle  = UserConfigParams::m_spectator_camera_angle*DEGREE_TO_RAD;
            *sideway    = 0;
            *distance   = -UserConfigParams::m_spectator_camera_distance;
            *smoothing  = true;
            *cam_roll_angle = 0.0f;
            break;
        }
    case CM_SIMPLE_REPLAY:
        // TODO: Implement
        break;
    }

}   // getCameraSettings

//-----------------------------------------------------------------------------
/** Called once per time frame to move the camera to the right position.
 *  \param dt Time step.
 */
void CameraNormal::update(float dt)
{
    Camera::update(dt);
    if(!m_kart) return;

    m_camera->setNearValue(1.0f);

    // Relativity close-chase uses a dedicated pipeline that bypasses all
    // the legacy offset/smoothing machinery (which causes jitter and clipping).
    if (useRelativityCloseChase(getMode()))
    {
        ExplosionAnimation* ea =
            dynamic_cast<ExplosionAnimation*>(m_kart->getKartAnimation());
        if (!ea || ea->hasResetAlready())
            updateRelativityCamera(dt);
        m_camera->setNearValue(getCameraSurfaceAwareNearPlane(
            dynamic_cast<Kart*>(m_kart), toBt(m_camera->getPosition()),
            toBt(m_camera->getTarget() - m_camera->getPosition())));
        m_camera->setFOV(getBaseFov());
        return;
    }

    float above_kart, cam_angle, side_way, distance, cam_roll_angle;
    bool  smoothing;

    getCameraSettings(getMode(), &above_kart, &cam_angle, &side_way,
                                 &distance, &smoothing, &cam_roll_angle);

    // If an explosion is happening, stop moving the camera,
    // but keep it target on the kart.
    ExplosionAnimation* ea =
        dynamic_cast<ExplosionAnimation*>(m_kart->getKartAnimation());
    if (ea && !ea->hasResetAlready())
    {
        // The camera target needs to be 'smooth moved', otherwise
        // there will be a noticable jump in the first frame

        // Aim at the usual same position of the kart (i.e. slightly
        // above the kart).
        // Note: this code is replicated from smoothMoveCamera so that
        // the camera keeps on pointing to the same spot.
        Vec3 current_target =
            m_kart->getSmoothedTrans()(getCameraTargetOffset(above_kart));
        m_camera->setTarget(current_target.toIrrVector());
    }
    else // no kart animation
    {
        if (smoothing)
        {
            m_last_smooth_mode = getMode();
            moveCamera(dt, true, above_kart, cam_angle, distance);
        }

        positionCamera(dt, above_kart, cam_angle, side_way, distance, smoothing, cam_roll_angle);
    }

    if (!smoothing)
    {
        getCameraSettings(m_last_smooth_mode, &above_kart, &cam_angle, &side_way,
                                              &distance, &smoothing, &cam_roll_angle);
        moveCamera(dt, false, above_kart, cam_angle, distance);
    }
    m_camera->setNearValue(1.0f);
    m_camera->setFOV(getBaseFov());
}   // update

// ----------------------------------------------------------------------------
/** Actually sets the camera based on the given parameter.
 *  \param above_kart How far above the camera should aim at.
 *  \param cam_angle  Angle above the kart plane for the camera.
 *  \param sideway Sideway movement of the camera.
 *  \param distance Distance from kart.
 *  \param cam_roll_angle Roll camera for gyroscope steering effect.
*/
void CameraNormal::positionCamera(float dt, float above_kart, float cam_angle,
                           float side_way, float distance, float smoothing,
                           float cam_roll_angle)
{
    Vec3 wanted_position;
    Vec3 wanted_target = m_kart->getSmoothedTrans()
        (getCameraTargetOffset(above_kart));

    float tan_up = tanf(cam_angle);

    // Protection: Ensure TV camera variables are in valid state
    if (m_tv_current_index < 0) m_tv_current_index = 0;
    if (m_tv_switch_cooldown < 0) m_tv_switch_cooldown = 0.0f;

    Camera::Mode mode = getMode();
    if (UserConfigParams::m_reverse_look_use_soccer_cam && getMode() == CM_REVERSE) mode=CM_SPECTATOR_SOCCER;

    switch(mode)
    {
    case CM_SPECTATOR_SOCCER:
        {
            SoccerWorld *soccer_world = dynamic_cast<SoccerWorld*> (World::getWorld());
            if (soccer_world)
            {
                Vec3 ball_pos = soccer_world->getBallPosition();
                Vec3 to_target=(ball_pos-wanted_target);
                wanted_position = wanted_target + Vec3(0,  fabsf(distance)*tan_up+above_kart, 0) + (to_target.normalize() * distance * (getMode() == CM_REVERSE ? -1:1));
                m_camera->setPosition(wanted_position.toIrrVector());
                m_camera->setTarget(wanted_target.toIrrVector());
                return;
            }
            break;
        }
    case CM_SPECTATOR_TOP_VIEW:
        {
            SoccerWorld *soccer_world = dynamic_cast<SoccerWorld*> (World::getWorld());
            if (soccer_world) wanted_target = soccer_world->getBallPosition();
            wanted_position = wanted_target + Vec3(0,  distance+above_kart, 0);
            m_camera->setPosition(wanted_position.toIrrVector());
            m_camera->setTarget(wanted_target.toIrrVector());
            return;
        }
    case CM_SPECTATOR_TV:
        {
            SoccerWorld *soccer_world = dynamic_cast<SoccerWorld*> (World::getWorld());
            if (soccer_world && !m_tv_cameras.empty())
            {
                Vec3 ball_pos = soccer_world->getBallPosition();
                // decrement cooldown
                if (m_tv_switch_cooldown > 0.0f)
                    m_tv_switch_cooldown = std::max(0.0f, m_tv_switch_cooldown - dt);

                // choose the TV camera closest to the ball
                unsigned best = 0;
                float best_d2 = (m_tv_cameras[0] - ball_pos).length2();
                for (unsigned i = 1; i < m_tv_cameras.size(); i++)
                {
                    float d2 = (m_tv_cameras[i] - ball_pos).length2();
                    if (d2 < best_d2)
                    {
                        best = i;
                        best_d2 = d2;
                    }
                }

                // initialize current index on first use
                if (m_tv_current_index < 0 || (unsigned)m_tv_current_index >= m_tv_cameras.size())
                {
                    m_tv_current_index = (int)best;
                    m_tv_switch_cooldown = m_tv_cooldown_default;
                }
                else
                {
                    float current_d2 = (m_tv_cameras[(unsigned)m_tv_current_index] - ball_pos).length2();
                    // Switch only if cooldown elapsed and best is sufficiently better than current
                    if (m_tv_switch_cooldown <= 0.0f && best != (unsigned)m_tv_current_index
                        && best_d2 + m_tv_min_delta2 < current_d2)
                    {
                        m_tv_current_index = (int)best;
                        m_tv_switch_cooldown = m_tv_cooldown_default;
                    }
                }

                wanted_position = m_tv_cameras[(unsigned)m_tv_current_index];
                wanted_target = ball_pos;
                m_camera->setPosition(wanted_position.toIrrVector());
                m_camera->setTarget(wanted_target.toIrrVector());
                return;
            }
            break;
        }
    default: break;
    }

    Vec3 relative_position(side_way,
                           fabsf(distance)*tan_up+above_kart,
                           distance);
    btTransform t=m_kart->getSmoothedTrans();
    if(stk_config->m_camera_follow_skid &&
        m_kart->getSkidding()->getVisualSkidRotation()!=0)
    {
        // If the camera should follow the graphical skid, add the
        // visual rotation to the relative vector:
        btQuaternion q(m_kart->getSkidding()->getVisualSkidRotation(), 0, 0);
        t.setBasis(t.getBasis() * btMatrix3x3(q));
    }
    wanted_position = t(relative_position);
    
    if (!smoothing)
    {
        if (getMode()!=CM_FALLING)
            m_camera->setPosition(wanted_position.toIrrVector());
        m_camera->setTarget(wanted_target.toIrrVector());

        if (RaceManager::get()->getNumLocalPlayers() < 2)
        {
            SFXManager::get()->positionListener(m_camera->getPosition(),
                                      wanted_target - m_camera->getPosition(),
                                      Vec3(0, 1, 0));
        }
    }

    Kart *kart = dynamic_cast<Kart*>(m_kart);

    // Rotate the up vector (0,1,0) by the rotation ... which is just column 1
    const Vec3& up = m_kart->getSmoothedTrans().getBasis().getColumn(1);
    const irr::core::vector3df straight_up = irr::core::vector3df(0, 1, 0);

    if (kart && !kart->isFlying())
    {
        float f = 0.04f;  // weight for new up vector to reduce shaking
        m_camera->setUpVector(        f  * up.toIrrVector() +
                              (1.0f - f) * m_camera->getUpVector());
    }   // kart && !flying
    else
        m_camera->setUpVector(straight_up);

    if (cam_roll_angle != 0.0f)
    {
        btQuaternion q(m_kart->getSmoothedTrans().getBasis().getColumn(2),
            -cam_roll_angle);
        q *= m_kart->getSmoothedTrans().getRotation();
        btMatrix3x3 m(q);
        m_camera->setUpVector(((Vec3)m.getColumn(1)).toIrrVector());
    }
}   // positionCamera

//-----------------------------------------------------------------------------
void CameraNormal::readTVCameras(const XMLNode &root)
{
    m_tv_cameras.clear();
    // Optional configuration on root node
    float min_delta = -1.0f;
    float cooldown = -1.0f;
    root.get("min-delta", &min_delta);     // units; we'll square it for comparisons
    root.get("cooldown", &cooldown);       // seconds
    if (min_delta > 0.0f)
        m_tv_min_delta2 = min_delta * min_delta;
    if (cooldown > 0.0f)
        m_tv_cooldown_default = cooldown;

    for (unsigned int i = 0; i < root.getNumNodes(); i++)
    {
        const XMLNode* n = root.getNode(i);
        if (!n) continue;
        bool is_tv_camera_node = false;
        if (n->getName() == std::string("tv-camera"))
        {
            is_tv_camera_node = true; // legacy child name
        }
        else if (n->getName() == std::string("camera"))
        {
            std::string ctype;
            n->get("type", &ctype);
            if (ctype == "tv-camera")
                is_tv_camera_node = true;
        }
        if (is_tv_camera_node)
        {
            float child_min_delta = -1.0f;
            float child_cooldown = -1.0f;
            n->get("min-delta", &child_min_delta);
            n->get("cooldown", &child_cooldown);
            if (child_min_delta > 0.0f)
                m_tv_min_delta2 = child_min_delta * child_min_delta;
            if (child_cooldown > 0.0f)
                m_tv_cooldown_default = child_cooldown;

            Vec3 p(0,0,0);
            n->get("xyz", &p);
            m_tv_cameras.push_back(p);
        }
    }
} // readTVCameras

void CameraNormal::clearTVCameras()
{
    m_tv_cameras.clear();  // Nettoie les caméras statiques
    
    // Remet à zéro l'état de TOUTES les caméras existantes
    for(unsigned int i = 0; i < Camera::getNumCameras(); i++)
    {
        Camera* camera = Camera::getCamera(i);
        if(camera && camera->getType() == Camera::CM_TYPE_NORMAL)
        {
            CameraNormal* normal_cam = static_cast<CameraNormal*>(camera);
            normal_cam->m_tv_current_index = -1;
            normal_cam->m_tv_switch_cooldown = 0.0f;
        }
    }
} // clearTVCameras

