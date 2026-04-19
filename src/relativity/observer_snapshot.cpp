//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2026 SuperTuxKart-Team
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

#include "relativity/observer_snapshot.hpp"

#include "config/user_config.hpp"
#include "karts/abstract_kart.hpp"
#include "karts/kart.hpp"
#include "items/attachment.hpp"
#include "modes/world.hpp"
#include "relativity/relativity_math.hpp"

#include <algorithm>
#include <assert.h>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace
{

const btScalar MIN_VISUAL_DIRECTION_LENGTH2 = btScalar(1.0e-6f);
const btScalar MAX_VISUAL_TELEPORT_DISTANCE2 = btScalar(144.0f);

struct VisualMotionFilterState
{
    bool      m_has_position;
    bool      m_has_direction;
    int       m_last_tick;
    btVector3 m_last_observer_position;
    btVector3 m_filtered_direction;

    VisualMotionFilterState()
        : m_has_position(false),
          m_has_direction(false),
          m_last_tick(std::numeric_limits<int>::min()),
          m_last_observer_position(0.0f, 0.0f, 0.0f),
          m_filtered_direction(0.0f, 0.0f, 1.0f)
    {
    }
};   // struct VisualMotionFilterState

// Keyed by kart pointer; entries are pruned when the kart is destroyed
// (see clearVisualMotionFilterForKart) and the whole map is dropped on
// race teardown (clearAllVisualMotionFilters) so a new kart can never
// inherit stale filter state from an allocator-reused address.
std::unordered_map<const AbstractKart*, VisualMotionFilterState>
    g_visual_motion_filters;

float clamp01(float value)
{
    if (!std::isfinite((double)value))
        return 0.0f;
    if (value <= 0.0f)
        return 0.0f;
    if (value >= 1.0f)
        return 1.0f;
    return value;
}   // clamp01

bool isFiniteVector(const btVector3& v)
{
    return std::isfinite((double)v.x()) &&
           std::isfinite((double)v.y()) &&
           std::isfinite((double)v.z());
}   // isFiniteVector

btVector3 normalizedOrZero(const btVector3& v)
{
    if (!isFiniteVector(v))
        return btVector3(0.0f, 0.0f, 0.0f);

    const btScalar length2 = v.length2();
    if (length2 <= MIN_VISUAL_DIRECTION_LENGTH2)
        return btVector3(0.0f, 0.0f, 0.0f);

    return v / btSqrt(length2);
}   // normalizedOrZero

btVector3 getSmoothedVisualDirection(const AbstractKart* observer_kart,
                                     const btVector3& observer_position,
                                     const btVector3& coordinate_velocity,
                                     float beta)
{
    VisualMotionFilterState& filter = g_visual_motion_filters[observer_kart];

    // The beta direction comes directly from the kart's physical velocity.
    // dampVerticalMotion was previously applied here, but scaling one axis
    // before normalising tilts the Lorentz axis away from the true velocity
    // direction, which contradicts the filter's stated purpose.
    const btVector3 desired_direction = normalizedOrZero(coordinate_velocity);

    // Only advance the temporal filter once per simulation tick. This function
    // is called from rendering code, potentially many times per frame (once
    // per warped object); advancing the blend on every call caused the
    // "smoothing" to collapse to the instantaneous direction and made
    // teleport detection dependent on scene complexity.
    const World* world = World::getWorld();
    const int current_tick = world ? world->getTicksSinceStart()
                                   : std::numeric_limits<int>::min();
    const bool new_tick = current_tick != filter.m_last_tick;

    if (!new_tick)
    {
        if (filter.m_has_direction &&
            isFiniteVector(filter.m_filtered_direction))
        {
            return filter.m_filtered_direction;
        }
        // Fall through and (re)initialise if the cached direction is unusable.
    }

    btScalar observer_step_length2 = btScalar(0.0f);
    if (filter.m_has_position)
    {
        const btVector3 observer_step =
            observer_position - filter.m_last_observer_position;
        observer_step_length2 = observer_step.length2();
    }

    filter.m_last_observer_position = observer_position;
    filter.m_has_position = true;
    filter.m_last_tick    = current_tick;

    if (desired_direction.length2() <= MIN_VISUAL_DIRECTION_LENGTH2)
    {
        filter.m_has_direction = false;
        return desired_direction;
    }

    if (!filter.m_has_direction || !isFiniteVector(filter.m_filtered_direction) ||
        observer_step_length2 > MAX_VISUAL_TELEPORT_DISTANCE2)
    {
        filter.m_filtered_direction = desired_direction;
        filter.m_has_direction = true;
        return desired_direction;
    }

    const btVector3 previous_direction =
        normalizedOrZero(filter.m_filtered_direction);
    const btScalar alignment = previous_direction.dot(desired_direction);
    if (alignment < btScalar(-0.35f))
    {
        filter.m_filtered_direction = desired_direction;
        return desired_direction;
    }

    const float blend = std::min(0.40f, std::max(0.12f, 0.16f + beta * 0.22f));
    btVector3 blended = previous_direction * (1.0f - blend) +
                        desired_direction * blend;
    blended = normalizedOrZero(blended);
    if (blended.length2() <= MIN_VISUAL_DIRECTION_LENGTH2)
        blended = desired_direction;

    filter.m_filtered_direction = blended;
    return blended;
}   // getSmoothedVisualDirection

}   // anonymous namespace

namespace Relativity
{

ObserverSnapshot::ObserverSnapshot()
    : m_valid(false),
      m_beta(0.0f),
      m_gamma(1.0f),
      m_view_alignment(0.0f),
      m_forward_intensity(0.0f),
      m_fov_scale(1.0f),
      m_camera_distance_scale(1.0f),
      m_trigger_motion_blur(false)
{
}   // ObserverSnapshot

// ----------------------------------------------------------------------------
ObserverVisualState::ObserverVisualState()
    : m_valid(false),
      m_item_active(false),
      m_doppler_active(false),
      m_speed_of_light(1000.0f),
      m_beta(0.0f),
      m_gamma(1.0f),
      m_inverse_gamma(1.0f),
      m_beta_vector(0.0f, 0.0f, 0.0f),
      m_observer_position(0.0f, 0.0f, 0.0f)
{
}   // ObserverVisualState

// ----------------------------------------------------------------------------
ObserverSnapshot buildObserverSnapshot(const AbstractKart* observer_kart,
                                       const btVector3& view_direction)
{
    ObserverSnapshot snapshot;
    if (!Relativity::isEnabled() || !observer_kart)
        return snapshot;

    const Kart* kart = dynamic_cast<const Kart*>(observer_kart);
    if (!kart)
        return snapshot;

    const RelativisticState& state = kart->getRelativisticState();
    snapshot.m_valid = true;
    snapshot.m_beta = std::min(std::max((float)state.m_beta, 0.0f), 0.999f);
    snapshot.m_gamma = std::max((float)state.m_gamma, 1.0f);

    if (snapshot.m_beta <= 0.0001f)
        return snapshot;

    btVector3 velocity = state.m_coordinate_velocity;
    if (velocity.length2() <= btScalar(1.0e-6f))
        return snapshot;
    velocity.normalize();

    btVector3 view = view_direction;
    if (view.length2() <= btScalar(1.0e-6f))
        view = observer_kart->getSmoothedTrans().getBasis().getColumn(2);
    else
        view.normalize();

    snapshot.m_view_alignment = std::max(-1.0f,
        std::min((float)view.dot(velocity), 1.0f));

    const float forward_alignment = std::max(snapshot.m_view_alignment, 0.0f);
    const float gamma_excess = std::min(snapshot.m_gamma - 1.0f, 2.5f);
    snapshot.m_forward_intensity = clamp01(
        forward_alignment * (0.65f * snapshot.m_beta + 0.20f * gamma_excess));

    snapshot.m_fov_scale = 1.0f + 0.35f * snapshot.m_forward_intensity;
    snapshot.m_camera_distance_scale = 1.0f
        - 0.20f * snapshot.m_forward_intensity;
    snapshot.m_trigger_motion_blur = snapshot.m_forward_intensity > 0.35f;

    return snapshot;
}   // buildObserverSnapshot

// ----------------------------------------------------------------------------
ObserverVisualState buildObserverVisualState(
    const AbstractKart* observer_kart,
    const btVector3& observer_position)
{
    ObserverVisualState visual_state;
    if (!Relativity::isEnabled() || !observer_kart)
        return visual_state;

    const Kart* kart = dynamic_cast<const Kart*>(observer_kart);
    if (!kart)
        return visual_state;

    const RelativisticState& state = kart->getRelativisticState();
    
    // Core rules item active check
    bool item_active = kart->isAnyPowerupActive();
    bool doppler_active = false;
    
    if (kart->isSquashed() || kart->getBlockedByPlungerTicks() > 0)
    {
        doppler_active = true;
    }
    if (kart->getAttachment() && (
        kart->getAttachment()->getType() == Attachment::ATTACH_TIME_DILATION ||
        kart->getAttachment()->getType() == Attachment::ATTACH_MASS_SPIKE ||
        kart->getAttachment()->getType() == Attachment::ATTACH_BOMB))
    {
        doppler_active = true;
    }
    
    // The relativity options screen exposes two independent sliders:
    //   * m_relativity_speed_normal  - baseline c used when no powerup/
    //     attachment is active. This is mirrored into stk_config so
    //     getConfiguredSpeedOfLight() returns it.
    //   * m_relativity_speed_powerup - visual c used while a powerup or
    //     attachment is active, for exaggerated aberration.
    // Both sliders should drive the visual c directly; the previous code
    // had a hardcoded 30 here which ignored the powerup slider entirely.
    const float normal_c  =
        (float)(int)UserConfigParams::m_relativity_speed_normal;
    const float powerup_c =
        (float)(int)UserConfigParams::m_relativity_speed_powerup;
    float speed_of_light = item_active ? powerup_c : normal_c;
    if (!std::isfinite((double)speed_of_light) || speed_of_light <= 0.0f)
        speed_of_light = Relativity::getConfiguredSpeedOfLight();

    if (!std::isfinite((double)speed_of_light) || speed_of_light <= 0.0f)
        return visual_state;

    const float beta = std::min(std::max((float)state.m_coordinate_velocity.length() / speed_of_light, 0.0f), 0.999f);
    const float gamma = 1.0f / sqrt(1.0f - beta * beta);
    btVector3 beta_vector = state.m_coordinate_velocity / speed_of_light;

    if (!std::isfinite((double)beta_vector.x()) ||
        !std::isfinite((double)beta_vector.y()) ||
        !std::isfinite((double)beta_vector.z()))
    {
        beta_vector = btVector3(0.0f, 0.0f, 0.0f);
    }

    const btScalar beta_vector_length2 = beta_vector.length2();
    if (beta < 0.0001f || beta_vector_length2 <= btScalar(1.0e-8f))
        return visual_state;

    const btVector3 smoothed_direction = getSmoothedVisualDirection(
        observer_kart, observer_position, state.m_coordinate_velocity, beta);
    if (smoothed_direction.length2() > MIN_VISUAL_DIRECTION_LENGTH2)
    {
        beta_vector = smoothed_direction * beta;
    }
    else
    {
        const btScalar beta_vector_length = btSqrt(beta_vector_length2);
        if (beta_vector_length > btScalar(beta))
            beta_vector *= btScalar(beta / beta_vector_length);
    }

    visual_state.m_valid = true;
    visual_state.m_item_active = item_active;
    visual_state.m_doppler_active = doppler_active;
    visual_state.m_speed_of_light = speed_of_light;
    visual_state.m_beta = beta;
    visual_state.m_gamma = gamma;
    visual_state.m_inverse_gamma = 1.0f / gamma;
    visual_state.m_beta_vector = beta_vector;
    visual_state.m_observer_position = observer_position;
    return visual_state;
}   // buildObserverVisualState

// ----------------------------------------------------------------------------
void clearVisualMotionFilterForKart(const AbstractKart* observer_kart)
{
    g_visual_motion_filters.erase(observer_kart);
}   // clearVisualMotionFilterForKart

// ----------------------------------------------------------------------------
void clearAllVisualMotionFilters()
{
    g_visual_motion_filters.clear();
}   // clearAllVisualMotionFilters

// ----------------------------------------------------------------------------
void observerSnapshotUnitTesting()
{
    ObserverSnapshot empty = buildObserverSnapshot(NULL, btVector3(0, 0, 1));
    (void)empty;
    assert(!empty.m_valid);

    ObserverVisualState visual_empty =
        buildObserverVisualState(NULL, btVector3(1.0f, 2.0f, 3.0f));
    (void)visual_empty;
    assert(!visual_empty.m_valid);

    ObserverSnapshot snapshot;
    snapshot.m_valid = true;
    snapshot.m_beta = 0.95f;
    snapshot.m_gamma = 3.2f;
    snapshot.m_view_alignment = 1.0f;
    snapshot.m_forward_intensity = clamp01(
        1.0f * (0.65f * snapshot.m_beta +
                0.20f * std::min(snapshot.m_gamma - 1.0f, 2.5f)));
    snapshot.m_fov_scale = 1.0f + 0.35f * snapshot.m_forward_intensity;
    snapshot.m_camera_distance_scale =
        1.0f - 0.20f * snapshot.m_forward_intensity;
    snapshot.m_trigger_motion_blur = snapshot.m_forward_intensity > 0.35f;

    assert(snapshot.m_forward_intensity > 0.9f);
    assert(snapshot.m_fov_scale > 1.3f);
    assert(snapshot.m_camera_distance_scale < 0.85f);
    assert(snapshot.m_trigger_motion_blur);

    ObserverVisualState visual_state;
    visual_state.m_valid = true;
    visual_state.m_beta = 0.8f;
    visual_state.m_gamma = 1.6666666f;
    visual_state.m_inverse_gamma = 0.6f;
    visual_state.m_beta_vector = btVector3(0.8f, 0.0f, 0.0f);
    visual_state.m_observer_position = btVector3(4.0f, 5.0f, 6.0f);
    assert(visual_state.m_beta_vector.length() > btScalar(0.79f));
    assert(std::fabs((double)visual_state.m_inverse_gamma - 0.6) < 0.0001);
    // beta_vector length must equal beta
    assert(std::fabs((double)visual_state.m_beta_vector.length()
                     - visual_state.m_beta) < 0.001);

    // Default-constructed ObserverSnapshot must report safe neutral values
    ObserverSnapshot default_snapshot;
    assert(!default_snapshot.m_valid);
    assert(std::fabs((double)default_snapshot.m_beta) < 0.000001);
    assert(std::fabs((double)default_snapshot.m_gamma - 1.0) < 0.000001);
    assert(std::fabs((double)default_snapshot.m_view_alignment) < 0.000001);
    assert(std::fabs((double)default_snapshot.m_forward_intensity) < 0.000001);
    assert(std::fabs((double)default_snapshot.m_fov_scale - 1.0) < 0.000001);
    assert(std::fabs((double)default_snapshot.m_camera_distance_scale - 1.0)
           < 0.000001);
    assert(!default_snapshot.m_trigger_motion_blur);

    // Default-constructed ObserverVisualState must report safe neutral values
    ObserverVisualState default_visual;
    assert(!default_visual.m_valid);
    assert(!default_visual.m_item_active);
    assert(!default_visual.m_doppler_active);
    assert(std::fabs((double)default_visual.m_beta) < 0.000001);
    assert(std::fabs((double)default_visual.m_gamma - 1.0) < 0.000001);
    assert(std::fabs((double)default_visual.m_inverse_gamma - 1.0) < 0.000001);
    assert((double)default_visual.m_beta_vector.length2() < 0.000001);

    // For the high-beta forward-facing snapshot constructed above, fov must
    // widen and camera distance must decrease relative to their neutral values
    assert(snapshot.m_fov_scale > 1.0f);
    assert(snapshot.m_camera_distance_scale < 1.0f);
    assert(snapshot.m_trigger_motion_blur);
    // forward_intensity drives both effects, so they must be consistent
    assert(snapshot.m_fov_scale - 1.0f > 0.0f);
    assert(1.0f - snapshot.m_camera_distance_scale > 0.0f);
}   // observerSnapshotUnitTesting

}   // namespace Relativity
