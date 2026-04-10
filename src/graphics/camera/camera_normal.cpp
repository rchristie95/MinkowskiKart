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

const float RELATIVITY_CLOSE_CHASE_DEFAULT_NEAR_PLANE = 0.05f;
const float RELATIVITY_CLOSE_CHASE_MIN_NEAR_PLANE = 0.02f;
const float RELATIVITY_CLOSE_CHASE_NEAR_PLANE_PROBE_DISTANCE = 0.75f;
const float RELATIVITY_UPHILL_FULL_PITCH = 14.0f * DEGREE_TO_RAD;
const float RELATIVITY_UPHILL_MAX_EXTRA_HEIGHT = 0.32f;
const float RELATIVITY_UPHILL_MAX_EXTRA_PULLBACK = 0.42f;
const float RELATIVITY_UPHILL_MAX_EXTRA_ANGLE = 5.0f * DEGREE_TO_RAD;

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

float getRelativityUphillCorrection(const Kart* kart)
{
    if (!kart || !kart->isOnGround())
        return 0.0f;

    const float uphill_pitch =
        std::max(0.0f, kart->getTerrainPitch(kart->getHeading()));
    if (uphill_pitch <= 0.0f)
        return 0.0f;

    const float beta = clamp01(
        static_cast<float>(kart->getRelativisticState().m_beta));
    if (beta <= 0.01f)
        return 0.0f;

    const float max_speed = std::max(1.0f,
        kart->getKartProperties()->getEngineMaxSpeed());
    const float speed_factor =
        clamp01(fabsf(kart->getSpeed()) / max_speed);
    const float pitch_factor =
        clamp01(uphill_pitch / RELATIVITY_UPHILL_FULL_PITCH);

    return pitch_factor * std::max(beta, speed_factor * beta);
}   // getRelativityUphillCorrection

void applyRelativityCloseChaseSlopeAdjustment(const Kart* kart,
                                              float* above_kart,
                                              float* cam_angle,
                                              float* distance)
{
    const float effect = getRelativityUphillCorrection(kart);
    if (effect <= 0.0f)
        return;

    if (above_kart)
        *above_kart += RELATIVITY_UPHILL_MAX_EXTRA_HEIGHT * effect;
    if (cam_angle)
        *cam_angle += RELATIVITY_UPHILL_MAX_EXTRA_ANGLE * effect;
    if (distance)
        *distance -= RELATIVITY_UPHILL_MAX_EXTRA_PULLBACK * effect;
}   // applyRelativityCloseChaseSlopeAdjustment

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

float getRelativityCloseChaseDistance(float base_distance)
{
    const float scaled_distance = base_distance * 0.06f;
    return -std::max(0.14f, std::min(scaled_distance, 0.22f));
}   // getRelativityCloseChaseDistance

float getRelativityCloseChaseHeight()
{
    return 0.76f;
}   // getRelativityCloseChaseHeight

float getRelativityCloseChaseAngle()
{
    return 3.0f * DEGREE_TO_RAD;
}   // getRelativityCloseChaseAngle

Vec3 getCameraTargetOffset(Camera::Mode mode, float above_kart)
{
    if (useRelativityCloseChase(mode))
        return Vec3(0.0f, above_kart - 0.10f, 2.75f);

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
    m_kart_position = btVector3(0, 0, 0);
    m_kart_rotation = btQuaternion(0, 0, 0, 1);
    m_last_smooth_mode = Mode::CM_NORMAL;
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
    float ratio = current_speed / max_speed_without_zipper;

    ratio = ratio > -0.12f ? ratio : -0.12f;
    if (Relativity::isEnabled())
        ratio = std::min(ratio, 0.0f);

    // distance of camera from kart in x and z plane
    float camera_distance = -2.8f - 5.6f * ratio;
    if (useRelativityCloseChase(getMode()))
    {
        camera_distance = distance;
    }
    else
    {
        camera_distance *= sqrtf(UserConfigParams::m_camera_forward_smooth_position);
    }
    float min_distance = (distance * 2.0f);
    if (distance > 0) camera_distance += distance + 1; // note that distance < 0
    if (camera_distance > min_distance) camera_distance = min_distance; // don't get too close to the kart

    float tan_up = 0;
    if (cam_angle > 0) tan_up = tanf(cam_angle) * distance;

    // Defines how far camera should be from player kart.
    float vertical_offset = (0.85f + ratio / 2.5f) - tan_up;
    if (useRelativityCloseChase(getMode()))
        vertical_offset = fabsf(camera_distance) * tan_up + above_kart;

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
    Vec3 current_target = btt(getCameraTargetOffset(getMode(), above_kart));
    // new required position of camera
    current_position = kart_camera_position_with_offset.toIrrVector();

    //Log::info("CAM_DEBUG", "OFFSET: %f %f %f TRANSFORMED %f %f %f TARGET %f %f %f",
    //    wanted_camera_offset.x(), wanted_camera_offset.y(), wanted_camera_offset.z(),
    //    kart_camera_position_with_offset.x(), kart_camera_position_with_offset.y(),
    //    kart_camera_position_with_offset.z(), current_target.x(), current_target.y(),
    //    current_target.z());

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
            float above_kart = getRelativityCloseChaseHeight();
            float cam_angle = getRelativityCloseChaseAngle();
            float distance = getRelativityCloseChaseDistance(m_distance);
            applyRelativityCloseChaseSlopeAdjustment(
                dynamic_cast<Kart*>(m_kart), &above_kart, &cam_angle,
                &distance);
            const float vertical_offset =
                fabsf(distance) * tanf(cam_angle) + above_kart;
            m_camera_offset = irr::core::vector3df(0.0f, vertical_offset,
                                                   distance);
            m_camera_offset = clampOffsetToRelativityBubble(m_camera_offset);
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
            if (useRelativityCloseChase(mode))
            {
                *above_kart = getRelativityCloseChaseHeight();
                *cam_angle = getRelativityCloseChaseAngle();
                *distance = getRelativityCloseChaseDistance(m_distance);
                applyRelativityCloseChaseSlopeAdjustment(
                    dynamic_cast<Kart*>(m_kart), above_kart, cam_angle,
                    distance);
            }
            float steering = m_kart->getSteerPercent()
                           * (1.0f + (m_kart->getSkidding()->getSkidFactor()
                                      - 1.0f)/2.3f );
            // quadratically to dampen small variations (but keep sign)
            float dampened_steer = fabsf(steering) * steering;
            *sideway             = -m_rotation_range*dampened_steer*0.5f;
            if (useRelativityCloseChase(mode))
                *sideway = 0.0f;
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
            m_kart->getSmoothedTrans()(getCameraTargetOffset(getMode(),
                                                             above_kart));
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
    float near_value = 1.0f;
    if (useRelativityCloseChase(getMode()))
    {
        near_value = getSurfaceAwareNearPlane(
            m_camera->getPosition(),
            m_camera->getTarget() - m_camera->getPosition());
    }
    m_camera->setNearValue(near_value);
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
        (getCameraTargetOffset(getMode(), above_kart));

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
