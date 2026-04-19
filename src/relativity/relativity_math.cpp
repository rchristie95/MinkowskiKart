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

#include "relativity/relativity_math.hpp"

#include "config/stk_config.hpp"
#include "guiengine/engine.hpp"
#include "karts/abstract_kart.hpp"
#include "karts/kart_properties.hpp"
#include "modes/world.hpp"
#include "network/network_config.hpp"
#include "physics/triangle_mesh.hpp"
#include "race/race_manager.hpp"
#include "relativity/observer_snapshot.hpp"
#include "relativity/relativistic_state.hpp"
#include "tracks/track.hpp"
#include "tracks/track_object_manager.hpp"
#include "utils/log.hpp"

#include <assert.h>
#include <cmath>
#include <limits>

namespace
{
const double MIN_LIGHT_SPEED = 0.001;
const double MAX_BETA_EPSILON = 1.0e-9;
const double MIN_GAMMA_RESPONSE_DELTA = 1.0e-6;
const float DEFAULT_REFERENCE_BOOST_SPEED = 40.0f;
const float MIN_ADJUSTABLE_LIGHT_SPEED_BETA = 0.95f;
const float MAX_ADJUSTABLE_LIGHT_SPEED_MULTIPLIER = 100.0f;
const float DEFAULT_WARP_BUBBLE_RADIUS = 3.5f;
const float VISUAL_STABILITY_RADIUS = 0.45f;
const float VISUAL_STABILITY_FADE_WIDTH = 0.65f;
const float APPARENT_NORMAL_SAMPLE_DISTANCE = 0.20f;

unsigned int g_velocity_clamp_count = 0;
unsigned int g_response_scale_count = 0;

bool isFiniteVector(const btVector3& v)
{
    return std::isfinite((double)v.x()) &&
           std::isfinite((double)v.y()) &&
           std::isfinite((double)v.z());
}   // isFiniteVector

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

float smoothstep01(float value)
{
    const float t = clamp01(value);
    return t * t * (3.0f - 2.0f * t);
}   // smoothstep01

btVector3 normalizedOrDefault(const btVector3& v, const btVector3& fallback)
{
    if (!isFiniteVector(v))
        return fallback;

    const btScalar length2 = v.length2();
    if (length2 <= btScalar(1.0e-8f))
        return fallback;

    return v / btSqrt(length2);
}   // normalizedOrDefault

btVector3 worldDirectionToObserverDirection(const btVector3& world_direction,
                                            const btVector3& beta_vector,
                                            float gamma)
{
    if (!isFiniteVector(world_direction) || !isFiniteVector(beta_vector))
        return world_direction;

    const btScalar beta2 = beta_vector.length2();
    if (beta2 <= btScalar(1.0e-6f) || gamma <= 1.0f)
        return normalizedOrDefault(world_direction, btVector3(0.0f, 0.0f, 1.0f));

    const btVector3 direction =
        normalizedOrDefault(world_direction, btVector3(0.0f, 0.0f, 1.0f));
    const btScalar beta_dot = beta_vector.dot(direction);
    const btScalar denominator = btScalar(1.0f) + beta_dot;
    if (fabsf((float)denominator) < 1.0e-5f)
        return direction;

    btVector3 observer_direction =
        direction / gamma +
        ((((gamma / (gamma + 1.0f)) * beta_dot) + btScalar(1.0f))
         * beta_vector);
    observer_direction /= denominator;
    return normalizedOrDefault(observer_direction, direction);
}   // worldDirectionToObserverDirection

btVector3 getRelativisticEmissionRelativePosition(
    const btVector3& relative, const btVector3& object_velocity,
    float speed_of_light)
{
    if (!isFiniteVector(relative) || !isFiniteVector(object_velocity) ||
        !std::isfinite((double)speed_of_light) || speed_of_light <= 1.0e-6f)
    {
        return relative;
    }

    const btScalar speed2 = object_velocity.length2();
    if (speed2 <= btScalar(1.0e-8f))
        return relative;

    const btScalar c2 = speed_of_light * speed_of_light;
    const btScalar a = speed2 - c2;
    if (fabsf((float)a) < 1.0e-6f)
        return relative;

    const btScalar b = relative.dot(object_velocity);
    const btScalar c = relative.dot(relative);
    const btScalar discriminant = b * b - a * c;
    if (discriminant < btScalar(0.0f))
        return relative;

    const btScalar emission_dt = (-b + btSqrt(discriminant)) / a;
    if (emission_dt > btScalar(0.0f) || emission_dt < btScalar(-1000.0f))
        return relative;

    return relative + object_velocity * emission_dt;
}   // getRelativisticEmissionRelativePosition

double clampAbsBeta(double beta)
{
    if (!std::isfinite(beta) || beta < 0.0)
        return 0.0;
    if (beta >= 1.0)
        return 1.0 - MAX_BETA_EPSILON;
    return beta;
}   // clampAbsBeta

float getReferenceBoostSpeed()
{
    const KartProperties* kart_properties = NULL;
    World* world = World::getWorld();
    if (world)
    {
        AbstractKart* kart = NULL;
        if (RaceManager::get() && RaceManager::get()->getNumLocalPlayers() > 0)
            kart = world->getLocalPlayerKart(0);
        if (!kart && world->getNumKarts() > 0)
            kart = world->getKart(0);
        if (kart)
            kart_properties = kart->getKartProperties();
    }

    if (!kart_properties)
        return DEFAULT_REFERENCE_BOOST_SPEED;

    const float base_speed = kart_properties->getEngineMaxSpeed();
    const float strongest_boost_increase = std::max(
        kart_properties->getZipperMaxSpeedIncrease(),
        std::max(kart_properties->getNitroMaxSpeedIncrease(),
                 kart_properties->getSlipstreamMaxSpeedIncrease()));
    const float reference_speed =
        base_speed + std::max(0.0f, strongest_boost_increase);
    if (!std::isfinite((double)reference_speed) || reference_speed <= 0.0f)
        return DEFAULT_REFERENCE_BOOST_SPEED;

    return reference_speed;
}   // getReferenceBoostSpeed

void getAdjustableSpeedOfLightBounds(float* min_speed_of_light,
                                     float* max_speed_of_light)
{
    const float reference_boost_speed = getReferenceBoostSpeed();
    float min_speed = reference_boost_speed / MIN_ADJUSTABLE_LIGHT_SPEED_BETA;
    float max_speed = reference_boost_speed *
                      MAX_ADJUSTABLE_LIGHT_SPEED_MULTIPLIER;
    if (!std::isfinite((double)min_speed) || min_speed <= 0.0f)
        min_speed = DEFAULT_REFERENCE_BOOST_SPEED /
                    MIN_ADJUSTABLE_LIGHT_SPEED_BETA;
    if (!std::isfinite((double)max_speed) || max_speed <= min_speed)
        max_speed = std::max(min_speed * 2.0f,
                             DEFAULT_REFERENCE_BOOST_SPEED *
                                 MAX_ADJUSTABLE_LIGHT_SPEED_MULTIPLIER);

    if (min_speed_of_light)
        *min_speed_of_light = min_speed;
    if (max_speed_of_light)
        *max_speed_of_light = max_speed;
}   // getAdjustableSpeedOfLightBounds

}   // anonymous namespace

namespace Relativity
{

ApparentSurfaceHit::ApparentSurfaceHit()
    : m_hit(false),
      m_world_point(0.0f, 0.0f, 0.0f),
      m_world_normal(0.0f, 1.0f, 0.0f),
      m_apparent_point(0.0f, 0.0f, 0.0f),
      m_apparent_normal(0.0f, 1.0f, 0.0f),
      m_visual_fade(0.0f),
      m_material(NULL)
{
}   // ApparentSurfaceHit

bool isEnabled()
{
    if (!stk_config || !stk_config->m_relativity_enabled)
        return false;

    const bool networking = NetworkConfig::get()->isNetworking();
    // Track the previously-observed networking state so the warning re-fires
    // on every off->on transition rather than only once per process.
    static bool s_last_networking_state = false;
    if (networking && !s_last_networking_state)
    {
        Log::warn("Relativity",
                  "Relativistic mechanics are disabled for network games.");
    }
    s_last_networking_state = networking;

    if (networking)
        return false;

    return true;
}   // isEnabled

// ----------------------------------------------------------------------------
bool isPropulsionLimited()
{
    return isEnabled() &&
           stk_config->m_relativity_mode == "propulsion-limited";
}   // isPropulsionLimited

// ----------------------------------------------------------------------------
bool isPreferredFrameDynamics()
{
    return isEnabled() &&
           stk_config->m_relativity_mode == "preferred-frame-dynamics";
}   // isPreferredFrameDynamics

// ----------------------------------------------------------------------------
bool shouldUseFirstPersonObserverCamera()
{
    return false;
}   // shouldUseFirstPersonObserverCamera

// ----------------------------------------------------------------------------
float getConfiguredSpeedOfLight()
{
    if (!stk_config ||
        !std::isfinite((double)stk_config->m_relativity_speed_of_light) ||
        stk_config->m_relativity_speed_of_light <= 0.0f)
    {
        return 1000.0f;
    }
    return stk_config->m_relativity_speed_of_light;
}   // getConfiguredSpeedOfLight

// ----------------------------------------------------------------------------
float getMinimumAdjustableSpeedOfLight()
{
    float min_speed_of_light = DEFAULT_REFERENCE_BOOST_SPEED /
                               MIN_ADJUSTABLE_LIGHT_SPEED_BETA;
    getAdjustableSpeedOfLightBounds(&min_speed_of_light, NULL);
    return min_speed_of_light;
}   // getMinimumAdjustableSpeedOfLight

// ----------------------------------------------------------------------------
float getMaximumAdjustableSpeedOfLight()
{
    float max_speed_of_light = DEFAULT_REFERENCE_BOOST_SPEED *
                               MAX_ADJUSTABLE_LIGHT_SPEED_MULTIPLIER;
    getAdjustableSpeedOfLightBounds(NULL, &max_speed_of_light);
    return max_speed_of_light;
}   // getMaximumAdjustableSpeedOfLight

// ----------------------------------------------------------------------------
float getSpeedOfLightSliderFraction(float speed_of_light)
{
    const float min_speed_of_light = getMinimumAdjustableSpeedOfLight();
    const float max_speed_of_light = getMaximumAdjustableSpeedOfLight();
    const double clamped_speed = std::max((double)min_speed_of_light,
        std::min((double)max_speed_of_light, (double)speed_of_light));
    const double min_log = std::log((double)min_speed_of_light);
    const double max_log = std::log((double)max_speed_of_light);
    if (!(max_log > min_log))
        return 0.0f;

    const double speed_log = std::log(clamped_speed);
    const double fraction = (speed_log - min_log) / (max_log - min_log);
    if (!std::isfinite(fraction))
        return 0.0f;

    return (float)std::max(0.0, std::min(1.0, fraction));
}   // getSpeedOfLightSliderFraction

// ----------------------------------------------------------------------------
float getWarpBubbleRadius()
{
    return DEFAULT_WARP_BUBBLE_RADIUS;
}   // getWarpBubbleRadius

// ----------------------------------------------------------------------------
bool setConfiguredSpeedOfLight(float speed_of_light,
                               float* applied_speed_of_light)
{
    if (!stk_config || !std::isfinite((double)speed_of_light))
        return false;

    const float min_speed_of_light = getMinimumAdjustableSpeedOfLight();
    const float max_speed_of_light = getMaximumAdjustableSpeedOfLight();
    const float clamped_speed = std::max(min_speed_of_light,
        std::min(max_speed_of_light, speed_of_light));
    stk_config->m_relativity_speed_of_light = clamped_speed;
    if (applied_speed_of_light)
        *applied_speed_of_light = clamped_speed;
    return true;
}   // setConfiguredSpeedOfLight

// ----------------------------------------------------------------------------
bool scaleConfiguredSpeedOfLight(float factor,
                                 float* applied_speed_of_light)
{
    if (!std::isfinite((double)factor) || factor <= 0.0f)
        return false;

    return setConfiguredSpeedOfLight(getConfiguredSpeedOfLight() * factor,
                                     applied_speed_of_light);
}   // scaleConfiguredSpeedOfLight

// ----------------------------------------------------------------------------
float getConfiguredMaxBeta()
{
    if (!stk_config ||
        !std::isfinite((double)stk_config->m_relativity_max_beta) ||
        stk_config->m_relativity_max_beta <= 0.0f)
    {
        return 0.98f;
    }
    if (stk_config->m_relativity_max_beta >= 1.0f)
        return 1.0f - 1.0e-6f;
    return stk_config->m_relativity_max_beta;
}   // getConfiguredMaxBeta

// ----------------------------------------------------------------------------
float getMaxCoordinateSpeed()
{
    return getConfiguredSpeedOfLight() * getConfiguredMaxBeta();
}   // getMaxCoordinateSpeed

// ----------------------------------------------------------------------------
int getRecommendedPhysicsSubsteps(float max_beta)
{
    if (!std::isfinite((double)max_beta) || max_beta <= 0.25f)
        return 1;
    if (max_beta <= 0.50f)
        return 2;
    if (max_beta <= 0.70f)
        return 3;
    if (max_beta <= 0.82f)
        return 4;
    if (max_beta <= 0.90f)
        return 5;
    return 6;
}   // getRecommendedPhysicsSubsteps

// ----------------------------------------------------------------------------
double betaForSpeed(double speed, double speed_of_light)
{
    if (!std::isfinite(speed) || speed_of_light < MIN_LIGHT_SPEED ||
        !std::isfinite(speed_of_light))
    {
        return 0.0;
    }

    return clampAbsBeta(std::fabs(speed) / speed_of_light);
}   // betaForSpeed

// ----------------------------------------------------------------------------
double gammaForSpeed(double speed, double speed_of_light)
{
    const double beta = betaForSpeed(speed, speed_of_light);
    const double beta2 = beta * beta;
    return 1.0 / std::sqrt(1.0 - beta2);
}   // gammaForSpeed

// ----------------------------------------------------------------------------
double properDt(double coordinate_dt, double gamma)
{
    if (!std::isfinite(coordinate_dt) || coordinate_dt <= 0.0)
        return 0.0;
    if (!std::isfinite(gamma) || gamma < 1.0)
        return coordinate_dt;
    return coordinate_dt / gamma;
}   // properDt

// ----------------------------------------------------------------------------
void updateState(RelativisticState *state,
                 const btVector3& coordinate_velocity,
                 double signed_speed,
                 double coordinate_dt,
                 double speed_of_light)
{
    if (!state)
        return;

    const double abs_speed = std::fabs(signed_speed);
    state->m_coordinate_velocity = coordinate_velocity;
    state->m_speed = signed_speed;
    state->m_beta = betaForSpeed(abs_speed, speed_of_light);
    state->m_gamma = gammaForSpeed(abs_speed, speed_of_light);

    if (coordinate_dt > 0.0 && std::isfinite(coordinate_dt))
    {
        state->m_coordinate_time_s += coordinate_dt;
        state->m_proper_time_s += properDt(coordinate_dt, state->m_gamma);
    }
}   // updateState

// ----------------------------------------------------------------------------
btVector3 clampVelocityToC(const btVector3& velocity,
                           float max_coordinate_speed,
                           bool *was_clamped)
{
    if (was_clamped)
        *was_clamped = false;

    if (!isFiniteVector(velocity))
    {
        if (was_clamped)
            *was_clamped = true;
        return btVector3(0.0f, 0.0f, 0.0f);
    }

    if (!std::isfinite((double)max_coordinate_speed) ||
        max_coordinate_speed <= 0.0f)
    {
        return velocity;
    }

    const btScalar speed = velocity.length();
    if (speed <= max_coordinate_speed || speed <= 0.0f)
        return velocity;

    if (was_clamped)
        *was_clamped = true;
    ++g_velocity_clamp_count;
    return velocity * (max_coordinate_speed / speed);
}   // clampVelocityToC

// ----------------------------------------------------------------------------
float scaleLongitudinalForce(float force, float signed_speed,
                             float speed_of_light)
{
    if (force == 0.0f || !std::isfinite((double)force) ||
        !std::isfinite((double)signed_speed))
    {
        return force;
    }

    const bool force_increases_forward_speed =
        signed_speed > 0.0f && force > 0.0f;
    const bool force_increases_reverse_speed =
        signed_speed < 0.0f && force < 0.0f;

    if (!force_increases_forward_speed && !force_increases_reverse_speed)
        return force;

    const double gamma = gammaForSpeed(signed_speed, speed_of_light);
    const double scale = 1.0 / (gamma * gamma * gamma);
    return (float)(force * scale);
}   // scaleLongitudinalForce

// ----------------------------------------------------------------------------
btVector3 scalePreferredFrameResponse(const btVector3& response_vector,
                                      const btVector3& coordinate_velocity,
                                      float speed_of_light)
{
    if (!isFiniteVector(response_vector) || !isFiniteVector(coordinate_velocity))
        return response_vector;

    if (response_vector.fuzzyZero())
        return response_vector;

    const btScalar speed = coordinate_velocity.length();
    if (speed <= btScalar(0.0))
        return response_vector;

    const double gamma = gammaForSpeed((double)speed, speed_of_light);
    if (!std::isfinite(gamma) || gamma <= 1.0 + MIN_GAMMA_RESPONSE_DELTA)
        return response_vector;

    const btVector3 direction = coordinate_velocity / speed;
    const btScalar parallel_magnitude = response_vector.dot(direction);
    const btVector3 parallel = direction * parallel_magnitude;
    const btVector3 perpendicular = response_vector - parallel;
    const btScalar parallel_scale = (btScalar)(1.0 / (gamma * gamma * gamma));
    const btScalar perpendicular_scale = (btScalar)(1.0 / gamma);
    const btVector3 scaled = parallel * parallel_scale +
                             perpendicular * perpendicular_scale;

    if (!isFiniteVector(scaled))
        return response_vector;

    if ((scaled - response_vector).length2() > btScalar(1.0e-8f))
        ++g_response_scale_count;

    return scaled;
}   // scalePreferredFrameResponse

// ----------------------------------------------------------------------------
float getDirectionalEffectiveMass(float rest_mass,
                                  const btVector3& coordinate_velocity,
                                  const btVector3& response_direction,
                                  float speed_of_light)
{
    if (!std::isfinite((double)rest_mass) || rest_mass <= 0.0f)
        return 0.0f;

    const btVector3 direction = normalizedOrDefault(
        response_direction, btVector3(1.0f, 0.0f, 0.0f));
    const btScalar speed = coordinate_velocity.length();
    if (speed <= btScalar(1.0e-6f))
        return rest_mass;

    const btVector3 velocity_direction = coordinate_velocity / speed;
    const btScalar parallel = direction.dot(velocity_direction);
    const btScalar parallel2 = parallel * parallel;
    const btScalar perpendicular2 = btScalar(1.0f) - parallel2;
    const double gamma = gammaForSpeed((double)speed, speed_of_light);
    if (!std::isfinite(gamma) || gamma <= 1.0 + MIN_GAMMA_RESPONSE_DELTA)
        return rest_mass;

    const double inv_effective_mass =
        (parallel2 / (gamma * gamma * gamma * rest_mass)) +
        (perpendicular2 / (gamma * rest_mass));
    if (!std::isfinite(inv_effective_mass) || inv_effective_mass <= 0.0)
        return rest_mass;

    return (float)(1.0 / inv_effective_mass);
}   // getDirectionalEffectiveMass

// ----------------------------------------------------------------------------
float computeCollisionImpulseMagnitude(const btVector3& collision_normal,
                                       const btVector3& velocity_a,
                                       float mass_a,
                                       const btVector3& velocity_b,
                                       float mass_b,
                                       float restitution,
                                       float speed_of_light)
{
    if (mass_a <= 0.0f && mass_b <= 0.0f)
        return 0.0f;

    const btVector3 normal = normalizedOrDefault(
        collision_normal, btVector3(1.0f, 0.0f, 0.0f));
    const btVector3 relative_velocity = velocity_b - velocity_a;
    const btScalar relative_normal_speed = relative_velocity.dot(normal);
    if (relative_normal_speed >= btScalar(-1.0e-4f))
        return 0.0f;

    const float effective_mass_a = getDirectionalEffectiveMass(
        mass_a, velocity_a, normal, speed_of_light);
    const float effective_mass_b = getDirectionalEffectiveMass(
        mass_b, velocity_b, normal, speed_of_light);

    double effective_inverse_mass = 0.0;
    if (effective_mass_a > 0.0f)
        effective_inverse_mass += 1.0 / effective_mass_a;
    if (effective_mass_b > 0.0f)
        effective_inverse_mass += 1.0 / effective_mass_b;
    if (effective_inverse_mass <= 0.0)
        return 0.0f;

    const double clamped_restitution =
        std::max(0.0, std::min(1.0, (double)restitution));
    const double impulse =
        -(1.0 + clamped_restitution) *
        (double)relative_normal_speed / effective_inverse_mass;
    if (!std::isfinite(impulse) || impulse <= 0.0)
        return 0.0f;
    return (float)impulse;
}   // computeCollisionImpulseMagnitude

// ----------------------------------------------------------------------------
unsigned int getVelocityClampCount()
{
    return g_velocity_clamp_count;
}   // getVelocityClampCount

// ----------------------------------------------------------------------------
unsigned int getResponseScaleCount()
{
    return g_response_scale_count;
}   // getResponseScaleCount

// ----------------------------------------------------------------------------
void resetDebugCounters()
{
    g_velocity_clamp_count = 0;
    g_response_scale_count = 0;
}   // resetDebugCounters

// ----------------------------------------------------------------------------
float getVisualFadeForWorldPosition(const btVector3& world_position,
                                    const btVector3& observer_position)
{
    if (!Relativity::isEnabled() || !isFiniteVector(world_position) ||
        !isFiniteVector(observer_position))
    {
        return 0.0f;
    }

    const btScalar observer_distance =
        (world_position - observer_position).length();
    const float fade_t = ((float)observer_distance - VISUAL_STABILITY_RADIUS) /
                         VISUAL_STABILITY_FADE_WIDTH;
    return smoothstep01(fade_t);
}   // getVisualFadeForWorldPosition

// ----------------------------------------------------------------------------
float getVisualShellOffset(const AbstractKart* observer_kart,
                           const btVector3& observer_position,
                           const btVector3& world_position,
                           const btVector3& world_normal,
                           const btVector3& object_velocity)
{
    if (!Relativity::isEnabled() || !isFiniteVector(world_position) ||
        !isFiniteVector(world_normal))
    {
        return 0.0f;
    }

    const ObserverVisualState observer_state =
        buildObserverVisualState(observer_kart, observer_position);
    if (!observer_state.m_valid)
        return 0.0f;

    const float visual_fade = getVisualFadeForWorldPosition(
        world_position, observer_state.m_observer_position);
    if (visual_fade <= 1.0e-4f)
        return 0.0f;

    const btVector3 normal = normalizedOrDefault(
        world_normal, btVector3(0.0f, 1.0f, 0.0f));
    const btVector3 apparent_point = applyVisualPosition(
        world_position, observer_state, object_velocity, visual_fade);
    const float shell = (float)((apparent_point - world_position).dot(normal));
    if (!std::isfinite((double)shell) || shell <= 0.0f)
        return 0.0f;
    return shell;
}   // getVisualShellOffset

// ----------------------------------------------------------------------------
btVector3 applyVisualPosition(const btVector3& world_position,
                              const ObserverVisualState& observer_state,
                              const btVector3& object_velocity,
                              float visual_fade)
{
    if (!Relativity::isEnabled() || !observer_state.m_valid ||
        !isFiniteVector(world_position))
    {
        return world_position;
    }

    if (visual_fade < 0.0f)
    {
        visual_fade = getVisualFadeForWorldPosition(world_position,
                                                    observer_state.m_observer_position);
    }
    if (visual_fade <= 1.0e-4f)
        return world_position;

    btVector3 relative = world_position - observer_state.m_observer_position;
    if (relative.length2() <= btScalar(1.0e-6f))
        return observer_state.m_observer_position;

    relative = getRelativisticEmissionRelativePosition(
        relative, object_velocity, getConfiguredSpeedOfLight());
    if (relative.length2() <= btScalar(1.0e-6f))
        return observer_state.m_observer_position;

    const btScalar distance = relative.length();
    const btVector3 world_direction = relative / distance;
    const btVector3 observer_direction =
        worldDirectionToObserverDirection(world_direction,
                                          observer_state.m_beta_vector,
                                          observer_state.m_gamma);
    const btVector3 observer_relative = observer_direction * distance;
    const btVector3 blended_relative =
        relative.lerp(observer_relative, clamp01(visual_fade));
    return observer_state.m_observer_position + blended_relative;
}   // applyVisualPosition

// ----------------------------------------------------------------------------
btVector3 applyVisualNormal(const btVector3& world_position,
                            const btVector3& world_normal,
                            const ObserverVisualState& observer_state,
                            float visual_fade)
{
    if (!Relativity::isEnabled() || !observer_state.m_valid ||
        !isFiniteVector(world_position) || !isFiniteVector(world_normal))
    {
        return normalizedOrDefault(world_normal, btVector3(0.0f, 1.0f, 0.0f));
    }

    if (visual_fade < 0.0f)
    {
        visual_fade = getVisualFadeForWorldPosition(world_position,
                                                    observer_state.m_observer_position);
    }
    if (visual_fade <= 1.0e-4f)
        return normalizedOrDefault(world_normal, btVector3(0.0f, 1.0f, 0.0f));

    const btVector3 base = applyVisualPosition(world_position, observer_state,
                                               btVector3(0.0f, 0.0f, 0.0f),
                                               visual_fade);
    const btVector3 tip = applyVisualPosition(
        world_position +
            normalizedOrDefault(world_normal, btVector3(0.0f, 1.0f, 0.0f)) *
                APPARENT_NORMAL_SAMPLE_DISTANCE,
        observer_state, btVector3(0.0f, 0.0f, 0.0f), visual_fade);
    return normalizedOrDefault(tip - base, btVector3(0.0f, 1.0f, 0.0f));
}   // applyVisualNormal

// ----------------------------------------------------------------------------
bool castApparentDriveableRay(const AbstractKart* observer_kart,
                              const btVector3& observer_position,
                              const btVector3& from,
                              const btVector3& to,
                              ApparentSurfaceHit* hit,
                              bool interpolate_normal)
{
    if (hit)
        *hit = ApparentSurfaceHit();

    Track* track = Track::getCurrentTrack();
    if (!track)
        return false;

    btVector3 world_hit_point(0.0f, 0.0f, 0.0f);
    btVector3 world_normal(0.0f, 1.0f, 0.0f);
    const Material* material = NULL;

    const TriangleMesh& triangle_mesh = track->getTriangleMesh();
    const bool mesh_hit = triangle_mesh.castRay(from, to, &world_hit_point,
                                                &material, &world_normal,
                                                interpolate_normal);
    bool found = mesh_hit;

    // Ask the object manager separately with its own output storage and pick
    // the closer of the two. Chaining through a shared buffer relied on
    // TrackObjectManager::castRay reading the prior distance from a non-null
    // *material, which was unreliable when the triangle-mesh hit had no
    // material assigned (in that case any farther object hit would overwrite
    // the nearer mesh hit).
    TrackObjectManager* object_manager = track->getTrackObjectManager();
    if (object_manager)
    {
        btVector3 object_hit_point(0.0f, 0.0f, 0.0f);
        btVector3 object_normal(0.0f, 1.0f, 0.0f);
        const Material* object_material = NULL;
        if (object_manager->castRay(from, to, &object_hit_point,
                                    &object_material, &object_normal,
                                    interpolate_normal))
        {
            const btScalar object_distance2 =
                (object_hit_point - from).length2();
            const btScalar mesh_distance2 = mesh_hit
                ? (world_hit_point - from).length2()
                : std::numeric_limits<btScalar>::max();
            if (!mesh_hit || object_distance2 < mesh_distance2)
            {
                world_hit_point = object_hit_point;
                world_normal    = object_normal;
                material        = object_material;
                found           = true;
            }
        }
    }

    if (!found)
        return false;

    if (!hit)
        return true;

    hit->m_hit = true;
    hit->m_world_point = world_hit_point;
    hit->m_world_normal =
        normalizedOrDefault(world_normal, btVector3(0.0f, 1.0f, 0.0f));
    hit->m_material = material;

    const ObserverVisualState observer_state =
        buildObserverVisualState(observer_kart, observer_position);
    if (!observer_state.m_valid)
    {
        hit->m_apparent_point = world_hit_point;
        hit->m_apparent_normal = hit->m_world_normal;
        hit->m_visual_fade = 0.0f;
        return true;
    }

    hit->m_visual_fade = getVisualFadeForWorldPosition(
        world_hit_point, observer_state.m_observer_position);
    hit->m_apparent_point = applyVisualPosition(
        world_hit_point, observer_state, btVector3(0.0f, 0.0f, 0.0f),
        hit->m_visual_fade);
    hit->m_apparent_normal = applyVisualNormal(
        world_hit_point, hit->m_world_normal, observer_state,
        hit->m_visual_fade);
    return true;
}   // castApparentDriveableRay

namespace KartAdapter
{

float scalePropulsiveForce(float force, float signed_speed)
{
    if (!Relativity::isPropulsionLimited())
        return force;
    return Relativity::scaleLongitudinalForce(
        force, signed_speed, Relativity::getConfiguredSpeedOfLight());
}   // scalePropulsiveForce

btVector3 clampVelocity(const btVector3& velocity, bool *was_clamped)
{
    return Relativity::clampVelocityToC(
        velocity, Relativity::getMaxCoordinateSpeed(), was_clamped);
}   // clampVelocity

// ----------------------------------------------------------------------------
btVector3 scaleResponse(const btVector3& response_vector,
                        const btVector3& coordinate_velocity)
{
    if (!Relativity::isPreferredFrameDynamics())
        return response_vector;

    return Relativity::scalePreferredFrameResponse(
        response_vector, coordinate_velocity,
        Relativity::getConfiguredSpeedOfLight());
}   // scaleResponse

}   // namespace KartAdapter

// ----------------------------------------------------------------------------
void unitTesting()
{
    const double c = 80.0;
    const double gamma_06c = gammaForSpeed(0.6 * c, c);
    (void)gamma_06c;
    assert(std::fabs(gammaForSpeed(0.0, c) - 1.0) < 0.000001);
    assert(std::fabs(gamma_06c - 1.25) < 0.000001);
    assert(std::fabs(properDt(1.0, 2.0) - 0.5) < 0.000001);

    bool was_clamped = false;
    btVector3 v = clampVelocityToC(btVector3(100.0f, 0.0f, 0.0f),
                                   78.4f, &was_clamped);
    (void)v;
    assert(was_clamped);
    assert(std::fabs((double)v.length() - 78.4) < 0.001);

    const float scaled_force =
        scaleLongitudinalForce(100.0f, 0.6f * (float)c, (float)c);
    (void)scaled_force;
    assert(std::fabs((double)scaled_force - 51.2) < 0.001);
    assert(scaleLongitudinalForce(-100.0f, 0.6f * (float)c, (float)c)
           == -100.0f);

    if (stk_config)
    {
        const float min_speed_of_light = getMinimumAdjustableSpeedOfLight();
        const float max_speed_of_light = getMaximumAdjustableSpeedOfLight();
        const float original_speed_of_light = getConfiguredSpeedOfLight();
        float adjusted_speed = 0.0f;
        (void)min_speed_of_light;
        (void)max_speed_of_light;
        (void)original_speed_of_light;
        (void)adjusted_speed;
        assert(setConfiguredSpeedOfLight(0.5f, &adjusted_speed));
        assert(std::fabs((double)adjusted_speed - min_speed_of_light)
               < 0.0001);
        assert(std::fabs((double)getSpeedOfLightSliderFraction(
            min_speed_of_light)) < 0.0001);
        assert(std::fabs((double)getSpeedOfLightSliderFraction(
            max_speed_of_light) - 1.0) < 0.0001);
        assert(setConfiguredSpeedOfLight(original_speed_of_light));
    }

    btVector3 scaled_response =
        scalePreferredFrameResponse(btVector3(80.0f, 40.0f, 0.0f),
                                    btVector3(48.0f, 0.0f, 0.0f), (float)c);
    (void)scaled_response;
    assert(std::fabs((double)scaled_response.getX() - 40.96) < 0.01);
    assert(std::fabs((double)scaled_response.getY() - 32.0) < 0.01);

    assert(getRecommendedPhysicsSubsteps(0.10f) == 1);
    assert(getRecommendedPhysicsSubsteps(0.65f) == 3);
    assert(getRecommendedPhysicsSubsteps(0.95f) == 6);

    const float effective_mass_parallel = getDirectionalEffectiveMass(
        200.0f, btVector3(48.0f, 0.0f, 0.0f), btVector3(1.0f, 0.0f, 0.0f),
        (float)c);
    const float effective_mass_perpendicular = getDirectionalEffectiveMass(
        200.0f, btVector3(48.0f, 0.0f, 0.0f), btVector3(0.0f, 1.0f, 0.0f),
        (float)c);
    (void)effective_mass_parallel;
    (void)effective_mass_perpendicular;
    assert(effective_mass_parallel > effective_mass_perpendicular);

    const float collision_impulse = computeCollisionImpulseMagnitude(
        btVector3(1.0f, 0.0f, 0.0f), btVector3(20.0f, 0.0f, 0.0f), 200.0f,
        btVector3(0.0f, 0.0f, 0.0f), 200.0f, 0.1f, (float)c);
    (void)collision_impulse;
    assert(collision_impulse > 0.0f);

    ObserverVisualState test_observer;
    test_observer.m_valid = true;
    test_observer.m_observer_position = btVector3(0.0f, 0.0f, 0.0f);
    test_observer.m_beta_vector = btVector3(0.6f, 0.0f, 0.0f);
    test_observer.m_gamma = (float)gammaForSpeed(0.6f * c, c);
    test_observer.m_inverse_gamma = 1.0f / test_observer.m_gamma;
    const btVector3 warped = applyVisualPosition(
        btVector3(5.0f, 1.0f, 0.0f), test_observer, btVector3(0.0f, 0.0f, 0.0f),
        1.0f);
    assert(warped.x() > 5.0f);

    // betaForSpeed direct tests
    assert(std::fabs(betaForSpeed(0.0, c)) < 0.000001);
    assert(std::fabs(betaForSpeed(0.6 * c, c) - 0.6) < 0.000001);
    {
        // Speed at exactly c must be clamped to just below 1.0
        const double beta_at_c = betaForSpeed(c, c);
        assert(beta_at_c > 0.9999 && beta_at_c < 1.0);
        // Speed beyond c must also be clamped to just below 1.0
        const double beta_twice_c = betaForSpeed(2.0 * c, c);
        assert(beta_twice_c > 0.9999 && beta_twice_c < 1.0);
        // speed_of_light below the minimum threshold must return 0
        assert(std::fabs(betaForSpeed(0.5 * c, 0.0)) < 0.000001);
    }

    // properDt edge cases
    assert(std::fabs(properDt(0.0, 2.0)) < 0.000001);     // zero dt -> 0
    assert(std::fabs(properDt(-1.0, 2.0)) < 0.000001);    // negative dt -> 0
    // gamma < 1.0 is physically invalid; returns coordinate_dt unchanged
    assert(std::fabs(properDt(1.0, 0.5) - 1.0) < 0.000001);

    // updateState
    {
        RelativisticState state;
        // Null state pointer must not crash
        updateState(NULL, btVector3(0.0f, 0.0f, 0.0f), 0.0, 1.0, c);

        updateState(&state,
                    btVector3((float)(0.6 * c), 0.0f, 0.0f),
                    0.6 * c, 1.0, c);
        assert(std::fabs(state.m_beta - 0.6) < 0.000001);
        assert(std::fabs(state.m_gamma - 1.25) < 0.00001);
        assert(std::fabs(state.m_coordinate_time_s - 1.0) < 0.000001);
        // proper_time = coordinate_dt / gamma = 1.0 / 1.25 = 0.8
        assert(std::fabs(state.m_proper_time_s - 0.8) < 0.00001);
        assert(std::fabs((double)state.m_coordinate_velocity.getX() - 0.6 * c)
               < 0.01);

        // Negative dt must not advance either time accumulator
        const double prev_coord_t = state.m_coordinate_time_s;
        const double prev_proper_t = state.m_proper_time_s;
        updateState(&state, btVector3(0.0f, 0.0f, 0.0f), 0.0, -1.0, c);
        assert(std::fabs(state.m_coordinate_time_s - prev_coord_t) < 0.000001);
        assert(std::fabs(state.m_proper_time_s - prev_proper_t) < 0.000001);
    }

    // clampVelocityToC additional tests
    {
        bool clamp_flag = false;
        // Under-limit velocity must not be clamped
        const btVector3 slow = clampVelocityToC(
            btVector3(10.0f, 0.0f, 0.0f), 78.4f, &clamp_flag);
        assert(!clamp_flag);
        assert(std::fabs((double)slow.length() - 10.0) < 0.001);

        // Zero velocity must not be clamped
        const btVector3 zero_vel = clampVelocityToC(
            btVector3(0.0f, 0.0f, 0.0f), 78.4f, &clamp_flag);
        assert(!clamp_flag);
        assert((double)zero_vel.length2() < 0.000001);

        // Non-positive max speed returns velocity unclamped
        const btVector3 neg_max = clampVelocityToC(
            btVector3(100.0f, 0.0f, 0.0f), -1.0f, &clamp_flag);
        assert(!clamp_flag);
        assert(std::fabs((double)neg_max.length() - 100.0) < 0.001);
    }

    // scaleLongitudinalForce additional tests
    // Zero force returns zero regardless of speed
    assert(scaleLongitudinalForce(0.0f, 0.6f * (float)c, (float)c) == 0.0f);
    // Decelerating force (positive speed, negative force) must not be scaled
    assert(scaleLongitudinalForce(-100.0f, 0.6f * (float)c, (float)c) == -100.0f);
    // Reverse decelerating force (negative speed, positive force) must not be scaled
    assert(scaleLongitudinalForce(100.0f, -0.6f * (float)c, (float)c) == 100.0f);

    // getRecommendedPhysicsSubsteps boundary values
    assert(getRecommendedPhysicsSubsteps(-0.1f) == 1);  // negative -> 1
    assert(getRecommendedPhysicsSubsteps(0.25f) == 1);  // at boundary -> 1
    assert(getRecommendedPhysicsSubsteps(0.26f) == 2);  // just above -> 2
    assert(getRecommendedPhysicsSubsteps(0.50f) == 2);  // at boundary -> 2
    assert(getRecommendedPhysicsSubsteps(0.51f) == 3);  // just above -> 3
    assert(getRecommendedPhysicsSubsteps(0.70f) == 3);  // at boundary -> 3
    assert(getRecommendedPhysicsSubsteps(0.71f) == 4);  // just above -> 4
    assert(getRecommendedPhysicsSubsteps(0.82f) == 4);  // at boundary -> 4
    assert(getRecommendedPhysicsSubsteps(0.83f) == 5);  // just above -> 5
    assert(getRecommendedPhysicsSubsteps(0.90f) == 5);  // at boundary -> 5
    assert(getRecommendedPhysicsSubsteps(0.91f) == 6);  // just above -> 6

    // getDirectionalEffectiveMass numerical verification
    // At rest the effective mass equals the rest mass
    assert(std::fabs((double)getDirectionalEffectiveMass(
        200.0f, btVector3(0.0f, 0.0f, 0.0f),
        btVector3(1.0f, 0.0f, 0.0f), (float)c) - 200.0) < 0.001);
    // v = 0.6c -> gamma = 1.25; longitudinal mass: m*gamma^3 = 200*1.953125 ~= 390.625
    assert(std::fabs((double)effective_mass_parallel - 390.625) < 0.5);
    // transverse mass: m*gamma = 200*1.25 = 250.0
    assert(std::fabs((double)effective_mass_perpendicular - 250.0) < 0.5);

    // computeCollisionImpulseMagnitude additional tests
    {
        // Objects separating along the normal produce zero impulse
        const float separating_impulse = computeCollisionImpulseMagnitude(
            btVector3(1.0f, 0.0f, 0.0f),
            btVector3(0.0f, 0.0f, 0.0f), 200.0f,
            btVector3(20.0f, 0.0f, 0.0f), 200.0f,
            0.0f, (float)c);
        assert(std::fabs((double)separating_impulse) < 0.000001);

        // Elastic restitution must give larger impulse than perfectly inelastic
        const float elastic_impulse = computeCollisionImpulseMagnitude(
            btVector3(1.0f, 0.0f, 0.0f),
            btVector3(20.0f, 0.0f, 0.0f), 200.0f,
            btVector3(0.0f, 0.0f, 0.0f), 200.0f,
            1.0f, (float)c);
        const float inelastic_impulse = computeCollisionImpulseMagnitude(
            btVector3(1.0f, 0.0f, 0.0f),
            btVector3(20.0f, 0.0f, 0.0f), 200.0f,
            btVector3(0.0f, 0.0f, 0.0f), 200.0f,
            0.0f, (float)c);
        assert(elastic_impulse > inelastic_impulse);
        assert(inelastic_impulse > 0.0f);
    }

    // Debug counter tests
    {
        resetDebugCounters();
        assert(getVelocityClampCount() == 0u);
        assert(getResponseScaleCount() == 0u);

        bool dummy = false;
        clampVelocityToC(btVector3(200.0f, 0.0f, 0.0f), 78.4f, &dummy);
        assert(getVelocityClampCount() == 1u);

        scalePreferredFrameResponse(btVector3(80.0f, 40.0f, 0.0f),
                                    btVector3(48.0f, 0.0f, 0.0f), (float)c);
        assert(getResponseScaleCount() == 1u);

        resetDebugCounters();
        assert(getVelocityClampCount() == 0u);
        assert(getResponseScaleCount() == 0u);
    }

    // applyVisualNormal: returned vector must have unit length
    {
        ObserverVisualState normal_test_observer;
        normal_test_observer.m_valid = true;
        normal_test_observer.m_observer_position = btVector3(0.0f, 0.0f, 0.0f);
        normal_test_observer.m_beta_vector = btVector3(0.6f, 0.0f, 0.0f);
        normal_test_observer.m_gamma = (float)gammaForSpeed(0.6 * c, c);
        normal_test_observer.m_inverse_gamma =
            1.0f / normal_test_observer.m_gamma;
        normal_test_observer.m_speed_of_light = (float)c;
        const btVector3 apparent_normal = applyVisualNormal(
            btVector3(5.0f, 1.0f, 0.0f), btVector3(0.0f, 1.0f, 0.0f),
            normal_test_observer, 1.0f);
        assert(std::fabs((double)apparent_normal.length() - 1.0) < 0.001);
    }
}   // unitTesting

}   // namespace Relativity
