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

#include "graphics/camera/camera_fps.hpp"

#include "config/stk_config.hpp"
#include "config/user_config.hpp"
#include "karts/abstract_kart.hpp"
#include "karts/skidding.hpp"
#include "physics/triangle_mesh.hpp"
#include "tracks/track.hpp"
#include "tracks/track_object_manager.hpp"

#include "vector3d.h"

using namespace irr;

namespace
{

const float OBSERVER_SURFACE_CLEARANCE = 0.10f;
const float OBSERVER_SEGMENT_CLEARANCE = 0.08f;
const float OBSERVER_SURFACE_PROBE_UP = 0.35f;
const float OBSERVER_SURFACE_PROBE_DOWN = 1.50f;
const float OBSERVER_DEFAULT_NEAR_PLANE = 0.05f;
const float OBSERVER_MIN_NEAR_PLANE = 0.01f;
const float OBSERVER_NEAR_PLANE_PROBE_DISTANCE = 0.40f;

core::vector3df toIrr(const btVector3& v)
{
    return core::vector3df(v.getX(), v.getY(), v.getZ());
}   // toIrr

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

core::vector3df constrainObserverPositionToSurface(
    const AbstractKart* kart, const core::vector3df& desired_position,
    const core::vector3df& kart_origin, float observer_height)
{
    if (!kart)
        return desired_position;

    btVector3 constrained = toBt(desired_position);
    const btVector3 kart_anchor =
        toBt(kart_origin) +
        btVector3(0.0f, std::max(0.16f, observer_height * 0.45f), 0.0f);

    btVector3 hit_point;
    btVector3 hit_normal;
    if (castDriveableSurfaceRay(kart_anchor, constrained, &hit_point,
                                &hit_normal))
    {
        constrained = hit_point + hit_normal * OBSERVER_SEGMENT_CLEARANCE;
    }

    const btVector3 probe_from =
        constrained + btVector3(0.0f, OBSERVER_SURFACE_PROBE_UP, 0.0f);
    const btVector3 probe_to =
        constrained - btVector3(0.0f, OBSERVER_SURFACE_PROBE_DOWN, 0.0f);
    if (castDriveableSurfaceRay(probe_from, probe_to, &hit_point, &hit_normal))
    {
        const btScalar clearance = (constrained - hit_point).dot(hit_normal);
        if (clearance < OBSERVER_SURFACE_CLEARANCE)
        {
            constrained += hit_normal * (OBSERVER_SURFACE_CLEARANCE - clearance);
        }
    }

    return toIrr(constrained);
}   // constrainObserverPositionToSurface

core::vector3df getObserverForward(const AbstractKart* kart)
{
    if (!kart)
        return core::vector3df(0.0f, 0.0f, 1.0f);

    const btVector3 smoothed_forward =
        kart->getSmoothedTrans().getBasis().getColumn(2);
    core::vector3df forward(smoothed_forward.getX(), smoothed_forward.getY(),
                            smoothed_forward.getZ());
    if (forward.getLengthSQ() <= 1.0e-6f)
    {
        const float heading = kart->getHeading();
        const float pitch = kart->getTerrainPitch(heading);
        forward = core::vector3df(sinf(heading) * cosf(pitch),
                                  sinf(pitch),
                                  cosf(heading) * cosf(pitch));
    }
    if (forward.getLengthSQ() <= 1.0e-6f)
        return core::vector3df(0.0f, 0.0f, 1.0f);

    forward.Y = std::max(-0.55f, std::min(0.55f, forward.Y));
    forward.normalize();
    return forward;
}   // getObserverForward

core::vector3df smoothObserverForward(const core::vector3df& previous_forward,
                                      const core::vector3df& desired_forward,
                                      float dt)
{
    if (desired_forward.getLengthSQ() <= 1.0e-6f)
        return core::vector3df(0.0f, 0.0f, 1.0f);

    core::vector3df previous(previous_forward);
    if (previous.getLengthSQ() <= 1.0e-6f)
        return desired_forward;

    previous.normalize();
    const float alignment = previous.dotProduct(desired_forward);
    if (alignment < -0.35f)
        return desired_forward;

    const float blend = std::min(1.0f, std::max(0.12f, dt * 7.5f));
    core::vector3df blended = previous * (1.0f - blend) +
                              desired_forward * blend;
    if (blended.getLengthSQ() <= 1.0e-6f)
        return desired_forward;

    blended.normalize();
    return blended;
}   // smoothObserverForward

void buildStabilizedObserverBasis(const core::vector3df& forward_hint,
                                  core::vector3df* right,
                                  core::vector3df* up,
                                  core::vector3df* forward)
{
    const core::vector3df world_up(0.0f, 1.0f, 0.0f);
    core::vector3df fwd = normalizedOrDefault(forward_hint,
                                              core::vector3df(0.0f, 0.0f, 1.0f));
    core::vector3df basis_right = world_up.crossProduct(fwd);
    if (basis_right.getLengthSQ() <= 1.0e-6f)
        basis_right = core::vector3df(1.0f, 0.0f, 0.0f);
    else
        basis_right.normalize();

    core::vector3df basis_up = fwd.crossProduct(basis_right);
    basis_up = normalizedOrDefault(basis_up, world_up);

    if (right)
        *right = basis_right;
    if (up)
        *up = basis_up;
    if (forward)
        *forward = fwd;
}   // buildStabilizedObserverBasis

float getSurfaceAwareNearPlane(const core::vector3df& eye_position,
                               const core::vector3df& view_direction)
{
    const btVector3 from = toBt(eye_position);
    const btVector3 to = from + toBt(normalizedOrDefault(
        view_direction, core::vector3df(0.0f, 0.0f, 1.0f))) *
        OBSERVER_NEAR_PLANE_PROBE_DISTANCE;

    btVector3 hit_point;
    btVector3 hit_normal;
    if (!castDriveableSurfaceRay(from, to, &hit_point, &hit_normal))
        return OBSERVER_DEFAULT_NEAR_PLANE;

    const float hit_distance = (hit_point - from).length();
    return std::max(OBSERVER_MIN_NEAR_PLANE,
                    std::min(OBSERVER_DEFAULT_NEAR_PLANE,
                             hit_distance * 0.5f));
}   // getSurfaceAwareNearPlane

}   // anonymous namespace

// ============================================================================
CameraFPS::CameraFPS(int camera_index, AbstractKart* kart)
         : Camera(Camera::CM_TYPE_FPS, camera_index, kart)
{
    m_attached      = false;
    m_smooth        = false;
    m_hide_attached_kart_body = false;
    m_mouse_look_enabled = true;
    m_stabilize_attached_camera = false;

    // TODO: Put these values into a config file
    //       Global or per split screen zone?
    //       Either global or per user (for instance, some users may not like
    //       the extra camera rotation so they could set m_rotation_range to
    //       zero to disable it for themselves).
    m_position_speed = 8.0f;
    m_target_speed   = 10.0f;
    m_rotation_range = 0.4f;
    m_rotation_range = 0.0f;
    m_lin_velocity = core::vector3df(0, 0, 0);
    m_target_velocity = core::vector3df(0, 0, 0);
    m_target_direction = core::vector3df(0, 0, 1);
    m_target_up_vector = core::vector3df(0, 1, 0);
    m_direction_velocity = core::vector3df(0, 0, 0);

    m_local_position = core::vector3df(0, 0, 0);
    m_local_direction = core::vector3df(0, 0, 1);
    m_local_up = core::vector3df(0, 1, 0);
    m_default_local_position = m_local_position;
    m_default_local_direction = m_local_direction;
    m_default_local_up = m_local_up;
    m_attached_forward = core::vector3df(0, 0, 1);

    m_angular_velocity = 0;
    m_target_angular_velocity = 0;
    m_max_velocity = 15;
    reset();
}   // Camera

// ----------------------------------------------------------------------------
/** Removes the camera scene node from the scene.
 */
CameraFPS::~CameraFPS()
{
    if (m_hide_attached_kart_body && m_kart && m_kart->getNode())
        m_kart->getNode()->setVisible(true);
}   // ~Camera

//-----------------------------------------------------------------------------
/** Applies mouse movement to the first person camera.
 *  \param x The horizontal difference of the mouse position.
 *  \param y The vertical difference of the mouse position.
 */
void CameraFPS::applyMouseMovement (float x, float y)
{
    core::vector3df direction(m_target_direction);
    core::vector3df up(m_camera->getUpVector());

    // Set local values if the camera is attached to the kart
    if (m_attached)
        up = m_local_up;

    direction.normalize();
    up.normalize();

    core::vector3df side(direction.crossProduct(up));
    side.normalize();
    core::quaternion quat;
    quat.fromAngleAxis(y, side);

    core::quaternion quat_x;
    quat_x.fromAngleAxis(x, up);
    quat *= quat_x;

    direction = quat * direction;
    // Try to prevent toppling over
    // If the camera would topple over with the next movement, the vertical
    // movement gets reset close to the up vector
    if ((direction - up).getLengthSQ() + (m_target_direction - up).getLengthSQ()
        <= (direction - m_target_direction).getLengthSQ())
        direction = quat_x * ((m_target_direction - up).setLength(0.02f) + up);
    // Prevent toppling under
    else if ((direction + up).getLengthSQ() + (m_target_direction + up).getLengthSQ()
        <= (direction - m_target_direction).getLengthSQ())
        direction = quat_x * ((m_target_direction + up).setLength(0.02f) - up);
    m_target_direction = direction;

    // Don't do that because it looks ugly and is bad to handle ;)
    /*side = direction.crossProduct(up);
    // Compute new up vector
    up = side.crossProduct(direction);
    up.normalize();
    cam->setUpVector(up);*/
}   // applyMouseMovement

//-----------------------------------------------------------------------------
/** Called once per time frame to move the camera to the right position.
 *  \param dt Time step.
 */
void CameraFPS::update(float dt)
{
    Camera::update(dt);

    core::vector3df direction(m_camera->getTarget() - m_camera->getPosition());
    core::vector3df up(m_camera->getUpVector());
    core::vector3df side(direction.crossProduct(up));
    core::vector3df pos = m_camera->getPosition();

    // Set local values if the camera is attached to the kart
    if (m_attached)
    {
        direction = m_local_direction;
        up = m_local_up;
        pos = m_local_position;
    }

    // Update smooth movement
    if (m_smooth)
    {
        // Angular velocity
        if (m_angular_velocity < m_target_angular_velocity)
        {
            m_angular_velocity += UserConfigParams::m_fpscam_angular_velocity;
            if (m_angular_velocity > m_target_angular_velocity)
                m_angular_velocity = m_target_angular_velocity;
        }
        else if (m_angular_velocity > m_target_angular_velocity)
        {
            m_angular_velocity -= UserConfigParams::m_fpscam_angular_velocity;
            if (m_angular_velocity < m_target_angular_velocity)
                m_angular_velocity = m_target_angular_velocity;
        }

        // Linear velocity
        core::vector3df diff(m_target_velocity - m_lin_velocity);
        if (diff.X != 0 || diff.Y != 0 || diff.Z != 0)
        {
            if (diff.getLengthSQ() > 1) diff.setLength(1);
            m_lin_velocity += diff;
        }

        // Camera direction
        diff = m_target_direction - direction;
        if (diff.X != 0 || diff.Y != 0 || diff.Z != 0)
        {
            diff.setLength(UserConfigParams::m_fpscam_direction_speed);
            m_direction_velocity += diff;
            if (m_direction_velocity.getLengthSQ() >
                UserConfigParams::m_fpscam_smooth_direction_max_speed *
                UserConfigParams::m_fpscam_smooth_direction_max_speed)
            {
                m_direction_velocity.setLength(
                    UserConfigParams::m_fpscam_smooth_direction_max_speed);
            }
            direction += m_direction_velocity;
            m_target_direction = direction;
        }   // if diff is no 0

        // Camera rotation
        diff = m_target_up_vector - up;
        if (diff.X != 0 || diff.Y != 0 || diff.Z != 0)
        {
            if (diff.getLengthSQ() >
                UserConfigParams::m_fpscam_angular_velocity *
                UserConfigParams::m_fpscam_angular_velocity)
            {
                diff.setLength(UserConfigParams::m_fpscam_angular_velocity);
            }
            up += diff;
        }
    }
    else
    {
        direction = m_target_direction;
        up = m_target_up_vector;
        side = direction.crossProduct(up);
    }

    // Rotate camera
    core::quaternion quat;
    quat.fromAngleAxis(m_angular_velocity * dt, direction);
    up = quat * up;
    m_target_up_vector = quat * up;
    direction.normalize();
    up.normalize();
    side.normalize();

    // Top vector is the real up vector, not the one used by the camera
    core::vector3df top(side.crossProduct(direction));

    // Move camera
    core::vector3df movement(direction * m_lin_velocity.Z +
        top * m_lin_velocity.Y + side * m_lin_velocity.X);
    pos = pos + movement * dt;

    if (m_attached)
    {
        if (m_hide_attached_kart_body && m_kart && m_kart->getNode())
            m_kart->getNode()->setVisible(false);

        // Save current values
        m_local_position = pos;
        m_local_direction = direction;
        m_local_up = up;

        // The relativistic observer camera follows the kart heading, but it
        // should not inherit full track pitch/roll or it gets shoved into the
        // ground over hills and compressions.
        if (m_stabilize_attached_camera)
        {
            const core::vector3df kart_origin =
                m_kart->getSmoothedXYZ().toIrrVector();
            const core::vector3df desired_forward = getObserverForward(m_kart);
            core::vector3df forward = smoothObserverForward(
                m_attached_forward, desired_forward, dt);
            core::vector3df right;
            core::vector3df basis_up;
            buildStabilizedObserverBasis(forward, &right, &basis_up, &forward);
            m_attached_forward = forward;

            pos = kart_origin
                + right * m_local_position.X
                + basis_up * m_local_position.Y
                + forward * m_local_position.Z;

            direction = right * m_local_direction.X +
                        basis_up * m_local_direction.Y +
                        forward * m_local_direction.Z;
            if (direction.getLengthSQ() <= 1.0e-6f)
                direction = forward;
            direction.normalize();
            up = right * m_local_up.X +
                 basis_up * m_local_up.Y +
                 forward * m_local_up.Z;
            up = normalizedOrDefault(up, basis_up);
            pos = constrainObserverPositionToSurface(
                m_kart, pos, kart_origin, m_local_position.Y);
        }
        else
        {
            // Move the camera with the kart
            btTransform t = m_kart->getSmoothedTrans();
            if (stk_config->m_camera_follow_skid &&
                m_kart->getSkidding()->getVisualSkidRotation() != 0)
            {
                // If the camera should follow the graphical skid, add the
                // visual rotation to the relative vector:
                btQuaternion q(m_kart->getSkidding()->getVisualSkidRotation(), 0, 0);
                t.setBasis(t.getBasis() * btMatrix3x3(q));
            }
            pos = Vec3(t(Vec3(pos))).toIrrVector();

            btQuaternion q = t.getRotation();
            btMatrix3x3 mat(q);
            direction = Vec3(mat * Vec3(direction)).toIrrVector();
            up = Vec3(mat * Vec3(up)).toIrrVector();
        }
    }

    // Set camera attributes
    m_camera->setPosition(pos);
    m_camera->setTarget(pos + direction);
    m_camera->setUpVector(up);
    m_camera->setNearValue(m_attached
        ? getSurfaceAwareNearPlane(pos, direction)
        : 1.0f);
}   // update

// ----------------------------------------------------------------------------
void CameraFPS::setInitialTransform()
{
    if (!m_kart || !m_attached)
    {
        Camera::setInitialTransform();
        return;
    }

    core::vector3df pos;
    core::vector3df direction;
    core::vector3df up;

    if (m_stabilize_attached_camera)
    {
        const core::vector3df kart_origin = m_kart->getSmoothedXYZ().toIrrVector();
        core::vector3df right;
        core::vector3df basis_up;
        core::vector3df forward = getObserverForward(m_kart);
        buildStabilizedObserverBasis(forward, &right, &basis_up, &forward);
        m_attached_forward = forward;

        pos = kart_origin
            + right * m_local_position.X
            + basis_up * m_local_position.Y
            + forward * m_local_position.Z;
        direction = right * m_local_direction.X +
                    basis_up * m_local_direction.Y +
                    forward * m_local_direction.Z;
        if (direction.getLengthSQ() <= 1.0e-6f)
            direction = forward;
        direction.normalize();
        up = right * m_local_up.X +
             basis_up * m_local_up.Y +
             forward * m_local_up.Z;
        up = normalizedOrDefault(up, basis_up);
        pos = constrainObserverPositionToSurface(
            m_kart, pos, kart_origin, m_local_position.Y);
    }
    else
    {
        btTransform t = m_kart->getSmoothedTrans();
        Vec3 local_position(m_local_position.X, m_local_position.Y,
                            m_local_position.Z);
        pos = Vec3(t(local_position)).toIrrVector();

        btQuaternion q = t.getRotation();
        btMatrix3x3 mat(q);
        Vec3 local_direction(m_local_direction.X, m_local_direction.Y,
                             m_local_direction.Z);
        Vec3 local_up(m_local_up.X, m_local_up.Y, m_local_up.Z);
        direction = Vec3(mat * local_direction).toIrrVector();
        up = Vec3(mat * local_up).toIrrVector();
    }

    m_camera->setPosition(pos);
    m_camera->setTarget(pos + direction);
    m_camera->setUpVector(up);
    m_camera->setNearValue(m_attached
        ? getSurfaceAwareNearPlane(pos, direction)
        : 1.0f);
    setFoV();
}   // setInitialTransform

// ----------------------------------------------------------------------------
void CameraFPS::setLocalObserverTransform(const core::vector3df& position,
                                          const core::vector3df& direction,
                                          const core::vector3df& up)
{
    m_default_local_position = position;
    m_default_local_direction = direction;
    m_default_local_up = up;
    m_local_position = position;
    m_local_direction = direction;
    m_local_up = up;
    m_target_direction = direction;
    m_target_up_vector = up;
}   // setLocalObserverTransform

// ----------------------------------------------------------------------------
void CameraFPS::recenterView()
{
    m_local_position = m_default_local_position;
    m_local_direction = m_default_local_direction;
    m_local_up = m_default_local_up;
    m_target_direction = m_default_local_direction;
    m_target_up_vector = m_default_local_up;
    m_direction_velocity = core::vector3df(0.0f, 0.0f, 0.0f);
    m_target_velocity = core::vector3df(0.0f, 0.0f, 0.0f);
    m_lin_velocity = core::vector3df(0.0f, 0.0f, 0.0f);
    m_angular_velocity = 0.0f;
    m_target_angular_velocity = 0.0f;
    if (m_stabilize_attached_camera)
        m_attached_forward = getObserverForward(m_kart);
    setInitialTransform();
}   // recenterView

// ----------------------------------------------------------------------------
void CameraFPS::setHideAttachedKartBody(bool value)
{
    if (m_hide_attached_kart_body == value)
        return;

    m_hide_attached_kart_body = value;
    if (!value && m_kart && m_kart->getNode())
        m_kart->getNode()->setVisible(true);
}   // setHideAttachedKartBody

// ----------------------------------------------------------------------------
/** Sets the angular velocity for this camera. */
void CameraFPS::setAngularVelocity(float vel)
{
    if (m_smooth)
        m_target_angular_velocity = vel;
    else
        m_angular_velocity = vel;
}   // setAngularVelocity

// ----------------------------------------------------------------------------
/** Returns the current target angular velocity. */
float CameraFPS::getAngularVelocity()
{
    if (m_smooth)
        return m_target_angular_velocity;
    else
        return m_angular_velocity;
}   // getAngularVelocity

// ----------------------------------------------------------------------------
/** Sets the linear velocity for this camera. */
void CameraFPS::setLinearVelocity(core::vector3df vel)
{
    if (m_smooth)
        m_target_velocity = vel;
    else
        m_lin_velocity = vel;
}   // setLinearVelocity

// ----------------------------------------------------------------------------
/** Returns the current linear velocity. */
const core::vector3df &CameraFPS::getLinearVelocity()
{
    if (m_smooth)
        return m_target_velocity;
    else
        return m_lin_velocity;
}   // getLinearVelocity
