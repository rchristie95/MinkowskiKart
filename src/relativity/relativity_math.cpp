//
//  MinkowskiKart - a fun racing game with go-kart
//  Copyright (C) 2026 MinkowskiKart-Team
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
#include "config/user_config.hpp"
#include "guiengine/engine.hpp"
#include "karts/abstract_kart.hpp"
#include "karts/kart_properties.hpp"
#include "modes/world.hpp"
#include "physics/triangle_mesh.hpp"
#include "race/race_manager.hpp"
#include "relativity/observer_snapshot.hpp"
#include "relativity/relativistic_state.hpp"
#include "tracks/track.hpp"
#include "tracks/track_object_manager.hpp"
#include "utils/log.hpp"
#include "utils/time.hpp"

#include <cmath>
#include <limits>

namespace
{
const double MIN_C_LIGHT = 0.001;
const double MAX_BETA_EPSILON = 1.0e-9;
const float DEFAULT_NORMAL_C_LIGHT = 35.0f;
const float MIN_ADJUSTABLE_C_LIGHT = 15.0f;
const float MAX_ADJUSTABLE_C_LIGHT = 1000.0f;
// Must match C_LIGHT_STEP in options_screen_relativity.cpp so that
// setCurrentCLight stores values that align with what the slider can express.
const int C_LIGHT_SNAP_STEP = 5;
const float DEFAULT_WARP_BUBBLE_RADIUS = 3.5f;
const float APPARENT_NORMAL_SAMPLE_DISTANCE = 0.20f;

unsigned int g_velocity_clamp_count = 0;

// C-light ramp state. Kept at file scope so that resetCurrentCLight() can
// clear it between races without the stale mid-ramp artefact described in
// the bug report.
bool   g_clight_initialized    = false;
float  g_clight_ramp_start     = 0.0f;
float  g_clight_last_target    = 0.0f;
double g_clight_ramp_start_time = 0.0;

// Per-tick cache: avoids recomputing the ramp for every warped object in the
// same render frame. Keyed by World tick; -1 forces recompute on first use.
static int   g_clight_cache_tick  = -1;
static float g_clight_cache_value = DEFAULT_NORMAL_C_LIGHT;

bool  g_network_rules_active = false;
float g_network_normal_c_light = DEFAULT_NORMAL_C_LIGHT;
float g_network_max_beta = 0.98f;
float g_network_powerup_multiplier = 10.0f;

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

btVector3 applyVisualContraction(
    const btVector3& world_position,
    const Relativity::ObserverVisualState& observer_state)
{
    if (!observer_state.m_valid || !isFiniteVector(world_position) ||
        !isFiniteVector(observer_state.m_beta_vector) ||
        observer_state.m_gamma <= 1.0f)
    {
        return world_position;
    }

    const btScalar beta2 = observer_state.m_beta_vector.length2();
    if (beta2 <= btScalar(1.0e-6f))
        return world_position;

    const btVector3 relative =
        world_position - observer_state.m_observer_position;
    const btVector3 beta_direction =
        observer_state.m_beta_vector / btSqrt(beta2);
    const btVector3 parallel =
        beta_direction * relative.dot(beta_direction);
    const btVector3 perpendicular = relative - parallel;
    return observer_state.m_observer_position + perpendicular +
        parallel * observer_state.m_inverse_gamma;
}   // applyVisualContraction

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
    float c_light)
{
    if (!isFiniteVector(relative) || !isFiniteVector(object_velocity) ||
        !std::isfinite((double)c_light) || c_light <= 1.0e-6f)
    {
        return relative;
    }

    const btScalar speed2 = object_velocity.length2();
    if (speed2 <= btScalar(1.0e-8f))
        return relative;

    const btScalar c2 = c_light * c_light;
    const btScalar a = speed2 - c2;
    if (fabsf((float)a) < 1.0e-6f)
        return relative;

    const btScalar b = relative.dot(object_velocity);
    const btScalar c = relative.dot(relative);
    const btScalar discriminant = b * b - a * c;
    if (discriminant < btScalar(0.0f))
        return relative;

    // The equation a*t^2 + 2*b*t + c = 0 has two roots; one retarded (past
    // emission) and one advanced (future). The physical retarded time is the
    // largest negative root — the most recent past emission event. Picking
    // whichever root appears first in the array is wrong when both roots are
    // negative: the wrong (older) emission is selected.
    const btScalar sqrt_disc = btSqrt(discriminant);
    const btScalar root0 = (-b + sqrt_disc) / a;
    const btScalar root1 = (-b - sqrt_disc) / a;

    // Candidate: most-recent past root (largest value that is still <= 0).
    btScalar emission_dt = btScalar(0.0f);
    bool found = false;
    for (int i = 0; i < 2; i++)
    {
        const btScalar r = (i == 0) ? root0 : root1;
        if (r > btScalar(0.0f) || r < btScalar(-1000.0f))
            continue;
        if (!found || r > emission_dt)
        {
            emission_dt = r;
            found = true;
        }
    }
    if (!found)
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

float clampFiniteCLight(float c_light, float fallback)
{
    if (!std::isfinite((double)c_light) || c_light <= 0.0f)
        c_light = fallback;
    return std::max(MIN_ADJUSTABLE_C_LIGHT,
                    std::min(MAX_ADJUSTABLE_C_LIGHT, c_light));
}   // clampFiniteCLight

float getConfiguredNormalCLightValue()
{
    if (g_network_rules_active)
        return g_network_normal_c_light;
    return clampFiniteCLight(
        (float)UserConfigParams::m_relativity_normal_c_light,
        DEFAULT_NORMAL_C_LIGHT);
}   // getConfiguredNormalCLightValue

float getConfiguredPowerupCLightValue()
{
    // Warp-bubble c_light is always 10× the normal c_light; not user-adjustable.
    return g_network_powerup_multiplier * getConfiguredNormalCLightValue();
}   // getConfiguredPowerupCLightValue

void getAdjustableCLightBounds(float* min_c_light,
                               float* max_c_light)
{
    if (min_c_light)
        *min_c_light = MIN_ADJUSTABLE_C_LIGHT;
    if (max_c_light)
        *max_c_light = MAX_ADJUSTABLE_C_LIGHT;
}   // getAdjustableCLightBounds

struct ActiveCLightTarget
{
    bool                          m_active;
    float                         m_target_c_light;
    AbstractKart::CLightTargetKind m_kind;

    ActiveCLightTarget()
        : m_active(false),
          m_target_c_light(0.0f),
          m_kind(AbstractKart::C_LIGHT_TARGET_NONE)
    {
    }
};   // struct ActiveCLightTarget

ActiveCLightTarget getActiveLocalPlayerCLightTarget()
{
    ActiveCLightTarget result;
    if (!World::getWorld() || !RaceManager::get())
        return result;

    const unsigned int num_local_players =
        RaceManager::get()->getNumLocalPlayers();
    for (unsigned int i = 0; i < num_local_players; i++)
    {
        AbstractKart* kart = World::getWorld()->getLocalPlayerKart(i);
        if (!kart)
            continue;

        AbstractKart::CLightTargetKind kind = AbstractKart::C_LIGHT_TARGET_NONE;
        const float target = kart->getCLightTarget(&kind);
        if (target <= 0.0f)
            continue;

        if (!result.m_active || target < result.m_target_c_light)
        {
            result.m_active = true;
            result.m_target_c_light = target;
            result.m_kind = kind;
        }
    }

    return result;
}   // getActiveLocalPlayerCLightTarget

}   // anonymous namespace

namespace Relativity
{

float getConfiguredNormalCLight()
{
    return getConfiguredNormalCLightValue();
}

// ----------------------------------------------------------------------------
float getConfiguredPowerupCLight()
{
    return getConfiguredPowerupCLightValue();
}

// ----------------------------------------------------------------------------
void setNetworkRules(float normal_c_light, float max_beta,
                     float powerup_multiplier)
{
    g_network_normal_c_light = clampFiniteCLight(normal_c_light,
        DEFAULT_NORMAL_C_LIGHT);
    if (!std::isfinite((double)max_beta))
        max_beta = 0.98f;
    g_network_max_beta = std::max(0.10f, std::min(0.99f, max_beta));
    if (!std::isfinite((double)powerup_multiplier) ||
        powerup_multiplier <= 0.0f)
    {
        powerup_multiplier = 10.0f;
    }
    g_network_powerup_multiplier = powerup_multiplier;
    g_network_rules_active = true;
    resetCurrentCLight();
}   // setNetworkRules

// ----------------------------------------------------------------------------
void clearNetworkRules()
{
    g_network_rules_active = false;
    g_network_normal_c_light = DEFAULT_NORMAL_C_LIGHT;
    g_network_max_beta = 0.98f;
    g_network_powerup_multiplier = 10.0f;
    resetCurrentCLight();
}   // clearNetworkRules

// ----------------------------------------------------------------------------
bool hasNetworkRules()
{
    return g_network_rules_active;
}   // hasNetworkRules

// ----------------------------------------------------------------------------
float getPhysicsCLightForKart(const AbstractKart* kart)
{
    if (kart)
    {
        const float target_c_light = kart->getCLightTarget();
        if (std::isfinite((double)target_c_light) &&
            target_c_light > (float)MIN_C_LIGHT)
        {
            return target_c_light;
        }
    }
    return getConfiguredNormalCLightValue();
}   // getPhysicsCLightForKart

ApparentSurfaceHit::ApparentSurfaceHit()
    : m_hit(false),
      m_world_point(0.0f, 0.0f, 0.0f),
      m_world_normal(0.0f, 1.0f, 0.0f),
      m_apparent_point(0.0f, 0.0f, 0.0f),
      m_apparent_normal(0.0f, 1.0f, 0.0f),
      m_material(nullptr)
{
}   // ApparentSurfaceHit

// ----------------------------------------------------------------------------
bool isEnabled()
{
    return true;
}   // isEnabled

// ----------------------------------------------------------------------------
bool shouldUseFirstPersonObserverCamera()
{
    return false;
}   // shouldUseFirstPersonObserverCamera

// ----------------------------------------------------------------------------
bool isPowerupCLightActive()
{
    if (!isEnabled())
        return false;

    return getActiveLocalPlayerCLightTarget().m_active;
}   // isPowerupCLightActive

// ----------------------------------------------------------------------------
UserConfigParams::RelativityTrackClippingMode getTrackClippingMode()
{
    const int mode = UserConfigParams::m_relativity_track_clipping_mode;
    if (mode == (int)UserConfigParams::RelativityTrackClippingMode::
            DYNAMIC_TESSELLATION)
    {
        return UserConfigParams::RelativityTrackClippingMode::
            DYNAMIC_TESSELLATION;
    }
    if (mode == (int)UserConfigParams::RelativityTrackClippingMode::
            HEIGHT_CORRECTION)
    {
        return UserConfigParams::RelativityTrackClippingMode::
            HEIGHT_CORRECTION;
    }
    if (mode == (int)UserConfigParams::RelativityTrackClippingMode::
            TANGENT_VELOCITY_PROJECTION)
    {
        return UserConfigParams::RelativityTrackClippingMode::
            TANGENT_VELOCITY_PROJECTION;
    }
    if (mode == (int)UserConfigParams::RelativityTrackClippingMode::
            WARPED_COLLISION_PHYSICS)
    {
        return UserConfigParams::RelativityTrackClippingMode::
            WARPED_COLLISION_PHYSICS;
    }
    if (mode == (int)UserConfigParams::RelativityTrackClippingMode::
            DISABLED)
    {
        return UserConfigParams::RelativityTrackClippingMode::DISABLED;
    }
    return UserConfigParams::RelativityTrackClippingMode::
        WARPED_COLLISION_PHYSICS;
}   // getTrackClippingMode

// ----------------------------------------------------------------------------
bool useDynamicTrackTessellation()
{
    const UserConfigParams::RelativityTrackClippingMode mode =
        getTrackClippingMode();
    return mode == UserConfigParams::RelativityTrackClippingMode::
            DYNAMIC_TESSELLATION ||
        mode == UserConfigParams::RelativityTrackClippingMode::
            WARPED_COLLISION_PHYSICS ||
        mode == UserConfigParams::RelativityTrackClippingMode::
            TESSELLATION_AND_HEIGHT_CORRECTION;
}   // useDynamicTrackTessellation

// ----------------------------------------------------------------------------
bool useTrackHeightCorrection()
{
    const UserConfigParams::RelativityTrackClippingMode mode =
        getTrackClippingMode();
    return mode == UserConfigParams::RelativityTrackClippingMode::
            HEIGHT_CORRECTION ||
        mode == UserConfigParams::RelativityTrackClippingMode::
            TESSELLATION_AND_HEIGHT_CORRECTION;
}   // useTrackHeightCorrection

// ----------------------------------------------------------------------------
bool useTrackTangentVelocityProjection()
{
    const UserConfigParams::RelativityTrackClippingMode mode =
        getTrackClippingMode();
    return mode == UserConfigParams::RelativityTrackClippingMode::
        TANGENT_VELOCITY_PROJECTION;
}   // useTrackTangentVelocityProjection

// ----------------------------------------------------------------------------
bool useWarpedTrackCollisionPhysics()
{
    const UserConfigParams::RelativityTrackClippingMode mode =
        getTrackClippingMode();
    return mode == UserConfigParams::RelativityTrackClippingMode::
        WARPED_COLLISION_PHYSICS;
}   // useWarpedTrackCollisionPhysics

// ----------------------------------------------------------------------------
// Compute the ramped c_light value without any caching or side effects.
static float computeCurrentCLight()
{
    // Relativity never turns "off" here: normal driving always uses the
    // configured baseline c_light, and active on-kart powerup effects only
    // override that baseline temporarily.
    //
    // The transition between the two configured values is ramped over
    // kCLightRampSeconds of real time so that the Doppler / contraction visuals
    // don't snap abruptly when a powerup activates or ends.
    constexpr double kCLightRampSeconds = 1.0;

    const ActiveCLightTarget active_target = getActiveLocalPlayerCLightTarget();
    const float target_c_light = active_target.m_active
        ? active_target.m_target_c_light
        : getConfiguredNormalCLightValue();

    const double now = StkTime::getRealTime();
    if (!g_clight_initialized)
    {
        g_clight_initialized    = true;
        g_clight_ramp_start     = target_c_light;
        g_clight_last_target    = target_c_light;
        g_clight_ramp_start_time = now - kCLightRampSeconds;
    }
    else if (target_c_light != g_clight_last_target)
    {
        // Begin a new ramp from wherever we currently are toward the new
        // target. Computing the current interpolated value first means a
        // mid-transition reversal doesn't pop.
        const double prev_elapsed = now - g_clight_ramp_start_time;
        const double prev_t = prev_elapsed >= kCLightRampSeconds ? 1.0
            : std::max(0.0, prev_elapsed / kCLightRampSeconds);
        const float current = (float)((1.0 - prev_t) * g_clight_ramp_start
            + prev_t * g_clight_last_target);
        g_clight_ramp_start      = current;
        g_clight_last_target     = target_c_light;
        g_clight_ramp_start_time = now;
    }

    const double elapsed = now - g_clight_ramp_start_time;
    const double t = elapsed >= kCLightRampSeconds ? 1.0
        : std::max(0.0, elapsed / kCLightRampSeconds);
    // Smoothstep for a gentler ease-in/ease-out.
    const double smooth_t = t * t * (3.0 - 2.0 * t);
    return (float)((1.0 - smooth_t) * g_clight_ramp_start
        + smooth_t * g_clight_last_target);
}   // computeCurrentCLight

// ----------------------------------------------------------------------------
float getCurrentCLight()
{
    // Cache the ramp result once per World tick so that rendering code calling
    // this for every warped object in the scene doesn't recompute the ramp or
    // poll kart targets on each call.
    const World* world = World::getWorld();
    const int tick = world ? world->getTicksSinceStart() : -1;
    if (tick != g_clight_cache_tick || !g_clight_initialized)
    {
        const float c_light = computeCurrentCLight();
        g_clight_cache_value = c_light;
        g_clight_cache_tick  = tick;
    }
    return g_clight_cache_value;
}   // getCurrentCLight

// ----------------------------------------------------------------------------
void resetCurrentCLight()
{
    // Force re-initialisation on the next call to getCurrentCLight() so that
    // the new race always starts from the configured baseline rather than from
    // wherever the previous race's ramp left off. Also invalidate the per-tick
    // cache so the first call after reset never serves a stale value.
    g_clight_initialized = false;
    g_clight_cache_tick  = -1;
}   // resetCurrentCLight

// ----------------------------------------------------------------------------
float getMinimumAdjustableCLight()
{
    float min_c_light = MIN_ADJUSTABLE_C_LIGHT;
    getAdjustableCLightBounds(&min_c_light, NULL);
    return min_c_light;
}   // getMinimumAdjustableCLight

// ----------------------------------------------------------------------------
float getMaximumAdjustableCLight()
{
    float max_c_light = MAX_ADJUSTABLE_C_LIGHT;
    getAdjustableCLightBounds(NULL, &max_c_light);
    return max_c_light;
}   // getMaximumAdjustableCLight

// ----------------------------------------------------------------------------
float getCLightSliderFraction(float c_light)
{
    const float min_c_light = getMinimumAdjustableCLight();
    const float max_c_light = getMaximumAdjustableCLight();
    const double clamped_speed = std::max((double)min_c_light,
        std::min((double)max_c_light, (double)c_light));
    const double min_log = std::log((double)min_c_light);
    const double max_log = std::log((double)max_c_light);
    if (!(max_log > min_log))
        return 0.0f;

    const double speed_log = std::log(clamped_speed);
    const double fraction = (speed_log - min_log) / (max_log - min_log);
    if (!std::isfinite(fraction))
        return 0.0f;

    return (float)std::max(0.0, std::min(1.0, fraction));
}   // getCLightSliderFraction

// ----------------------------------------------------------------------------
float getWarpBubbleRadius()
{
    return DEFAULT_WARP_BUBBLE_RADIUS;
}   // getWarpBubbleRadius

// ----------------------------------------------------------------------------
bool setCurrentCLight(float c_light,
                      float* applied_c_light)
{
    if (!std::isfinite((double)c_light))
        return false;

    const float min_c_light = getMinimumAdjustableCLight();
    const float max_c_light = getMaximumAdjustableCLight();
    const float clamped_c_light = std::max(min_c_light,
        std::min(max_c_light, c_light));

    // Round to the slider's step granularity so that stored values are always
    // representable by the options-screen spinner.
    auto snapToStep = [&](float v) -> int {
        const int raw = (int)std::lround((double)v / C_LIGHT_SNAP_STEP)
                        * C_LIGHT_SNAP_STEP;
        return std::max((int)min_c_light,
                        std::min((int)max_c_light, raw));
    };

    const ActiveCLightTarget active_target = getActiveLocalPlayerCLightTarget();
    if (active_target.m_active &&
             active_target.m_kind == AbstractKart::C_LIGHT_TARGET_HALF_NORMAL)
    {
        const float normal_c_light = std::max(min_c_light,
            std::min(max_c_light, clamped_c_light * 2.0f));
        UserConfigParams::m_relativity_normal_c_light = snapToStep(normal_c_light);
    }
    else
    {
        UserConfigParams::m_relativity_normal_c_light = snapToStep(clamped_c_light);
    }

    resetCurrentCLight();
    const float current_c_light = getCurrentCLight();
    if (applied_c_light)
        *applied_c_light = current_c_light;
    return true;
}   // setCurrentCLight

// ----------------------------------------------------------------------------
bool scaleCurrentCLight(float factor,
                        float* applied_c_light)
{
    if (!std::isfinite((double)factor) || factor <= 0.0f)
        return false;

    return setCurrentCLight(getCurrentCLight() * factor,
                                     applied_c_light);
}   // scaleCurrentCLight

// ----------------------------------------------------------------------------
float getConfiguredMaxBeta()
{
    if (g_network_rules_active)
        return g_network_max_beta;
    const float beta = (float)UserConfigParams::m_relativity_max_beta;
    if (!std::isfinite((double)beta) || beta <= 0.0f)
        return 0.95f;
    if (beta >= 1.0f)
        return 1.0f - 1.0e-6f;
    return beta;
}   // getConfiguredMaxBeta

// ----------------------------------------------------------------------------
float getMaxCoordinateSpeed()
{
    return getCurrentCLight() * getConfiguredMaxBeta();
}   // getMaxCoordinateSpeed

// ----------------------------------------------------------------------------
float getMaxCoordinateSpeedForKart(const AbstractKart* kart)
{
    return getPhysicsCLightForKart(kart) * getConfiguredMaxBeta();
}   // getMaxCoordinateSpeedForKart

// ----------------------------------------------------------------------------
double betaForSpeed(double speed, double c_light)
{
    if (!std::isfinite(speed) || c_light < MIN_C_LIGHT ||
        !std::isfinite(c_light))
    {
        return 0.0;
    }

    return clampAbsBeta(std::fabs(speed) / c_light);
}   // betaForSpeed

// ----------------------------------------------------------------------------
double gammaForSpeed(double speed, double c_light)
{
    const double beta = betaForSpeed(speed, c_light);
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
                 double c_light)
{
    if (!state)
        return;

    const double abs_speed = std::fabs(signed_speed);
    state->m_coordinate_velocity = coordinate_velocity;
    state->m_speed = signed_speed;
    state->m_beta = betaForSpeed(abs_speed, c_light);
    state->m_gamma = gammaForSpeed(abs_speed, c_light);

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
                             float c_light)
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

    const double gamma = gammaForSpeed(signed_speed, c_light);
    const double scale = 1.0 / (gamma * gamma * gamma);
    return (float)(force * scale);
}   // scaleLongitudinalForce

unsigned int getVelocityClampCount()
{
    return g_velocity_clamp_count;
}   // getVelocityClampCount

// ----------------------------------------------------------------------------
void resetDebugCounters()
{
    g_velocity_clamp_count = 0;
}   // resetDebugCounters

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

    const btVector3 normal = normalizedOrDefault(
        world_normal, btVector3(0.0f, 1.0f, 0.0f));
    const btVector3 apparent_point = applyVisualPosition(
        world_position, observer_state, object_velocity);
    const float shell = (float)((apparent_point - world_position).dot(normal));
    if (!std::isfinite((double)shell) || shell <= 0.0f)
        return 0.0f;
    return shell;
}   // getVisualShellOffset

// ----------------------------------------------------------------------------
btVector3 applyVisualPosition(const btVector3& world_position,
                              const ObserverVisualState& observer_state,
                              const btVector3& object_velocity)
{
    if (!Relativity::isEnabled() || !observer_state.m_valid ||
        !isFiniteVector(world_position))
    {
        return world_position;
    }

    const btVector3 contracted_position =
        applyVisualContraction(world_position, observer_state);
    btVector3 relative =
        contracted_position - observer_state.m_observer_position;
    if (relative.length2() <= btScalar(1.0e-6f))
        return observer_state.m_observer_position;

    relative = getRelativisticEmissionRelativePosition(
        relative, object_velocity, observer_state.m_c_light);
    if (relative.length2() <= btScalar(1.0e-6f))
        return observer_state.m_observer_position;

    const btScalar distance = relative.length();
    const btVector3 world_direction = relative / distance;
    const btVector3 observer_direction =
        worldDirectionToObserverDirection(world_direction,
                                          observer_state.m_beta_vector,
                                          observer_state.m_gamma);
    return observer_state.m_observer_position + observer_direction * distance;
}   // applyVisualPosition

// ----------------------------------------------------------------------------
btVector3 applyVisualNormal(const btVector3& /*world_position*/,
                            const btVector3& world_normal,
                            const ObserverVisualState& observer_state)
{
    if (!Relativity::isEnabled() || !observer_state.m_valid ||
        !isFiniteVector(world_normal))
    {
        return normalizedOrDefault(world_normal, btVector3(0.0f, 1.0f, 0.0f));
    }

    const btVector3 beta_vector = observer_state.m_beta_vector;
    const btScalar beta2 = beta_vector.length2();
    if (beta2 < btScalar(1.0e-6f))
        return normalizedOrDefault(world_normal, btVector3(0.0f, 1.0f, 0.0f));

    const btVector3 beta_direction = beta_vector / btSqrt(beta2);
    const btVector3 parallel = beta_direction * world_normal.dot(beta_direction);
    const btVector3 perpendicular = world_normal - parallel;
    return normalizedOrDefault(perpendicular + parallel * observer_state.m_gamma,
                               world_normal);
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
        return true;
    }

    hit->m_apparent_point = applyVisualPosition(
        world_hit_point, observer_state, btVector3(0.0f, 0.0f, 0.0f));
    hit->m_apparent_normal = applyVisualNormal(
        world_hit_point, hit->m_world_normal, observer_state);
    return true;
}   // castApparentDriveableRay

namespace KartAdapter
{

float scalePropulsiveForce(float force, float /*signed_speed*/)
{
    // Engine force is not gamma-scaled. The hard speed cap in btKart::adjustSpeed
    // bounds kart velocity without creating a hidden cruise-speed equilibrium.
    return force;
}   // scalePropulsiveForce

btVector3 clampVelocity(const btVector3& velocity, bool *was_clamped)
{
    return Relativity::clampVelocityToC(
        velocity, Relativity::getMaxCoordinateSpeed(), was_clamped);
}   // clampVelocity

}   // namespace KartAdapter

// ----------------------------------------------------------------------------
void unitTesting()
{
// Use Log::fatal instead of assert() so tests fire in release builds too.
#define MK_CHECK(cond) \
    do { if (!(cond)) Log::fatal("Relativity::unitTesting", \
                                 "Test failed: %s (line %d)", #cond, __LINE__); \
    } while(0)

    const double c_light = 80.0;
    const double gamma_06c = gammaForSpeed(0.6 * c_light, c_light);
    (void)gamma_06c;
    MK_CHECK(std::fabs(gammaForSpeed(0.0, c_light) - 1.0) < 0.000001);
    MK_CHECK(std::fabs(gamma_06c - 1.25) < 0.000001);
    MK_CHECK(std::fabs(properDt(1.0, 2.0) - 0.5) < 0.000001);

    bool was_clamped = false;
    btVector3 v = clampVelocityToC(btVector3(100.0f, 0.0f, 0.0f),
                                   78.4f, &was_clamped);
    (void)v;
    MK_CHECK(was_clamped);
    MK_CHECK(std::fabs((double)v.length() - 78.4) < 0.001);

    const float scaled_force =
        scaleLongitudinalForce(100.0f, 0.6f * (float)c_light,
                               (float)c_light);
    (void)scaled_force;
    MK_CHECK(std::fabs((double)scaled_force - 51.2) < 0.001);
    MK_CHECK(scaleLongitudinalForce(-100.0f, 0.6f * (float)c_light,
                                    (float)c_light) == -100.0f);

    if (stk_config)
    {
        const float min_c_light = getMinimumAdjustableCLight();
        const float max_c_light = getMaximumAdjustableCLight();
        const float original_c_light = getCurrentCLight();
        float adjusted_speed = 0.0f;
        (void)min_c_light;
        (void)max_c_light;
        (void)original_c_light;
        (void)adjusted_speed;
        MK_CHECK(setCurrentCLight(0.5f, &adjusted_speed));
        MK_CHECK(std::fabs((double)adjusted_speed - min_c_light) < 0.0001);
        MK_CHECK(std::fabs((double)getCLightSliderFraction(min_c_light)) < 0.0001);
        MK_CHECK(std::fabs((double)getCLightSliderFraction(max_c_light) - 1.0)
                 < 0.0001);
        MK_CHECK(setCurrentCLight(original_c_light));
    }

    ObserverVisualState test_observer;
    test_observer.m_valid = true;
    test_observer.m_observer_position = btVector3(0.0f, 0.0f, 0.0f);
    test_observer.m_beta_vector = btVector3(0.6f, 0.0f, 0.0f);
    test_observer.m_gamma = (float)gammaForSpeed(0.6f * c_light, c_light);
    test_observer.m_inverse_gamma = 1.0f / test_observer.m_gamma;
    test_observer.m_c_light = (float)c_light;
    const btVector3 warped = applyVisualPosition(
        btVector3(5.0f, 1.0f, 0.0f), test_observer, btVector3(0.0f, 0.0f, 0.0f));
    MK_CHECK(warped.x() > 4.0f && warped.x() < 5.0f);

    MK_CHECK(std::fabs(betaForSpeed(0.0, c_light)) < 0.000001);
    MK_CHECK(std::fabs(betaForSpeed(0.6 * c_light, c_light) - 0.6) < 0.000001);
    {
        const double beta_at_c = betaForSpeed(c_light, c_light);
        MK_CHECK(beta_at_c > 0.9999 && beta_at_c < 1.0);
        const double beta_twice_c = betaForSpeed(2.0 * c_light, c_light);
        MK_CHECK(beta_twice_c > 0.9999 && beta_twice_c < 1.0);
        MK_CHECK(std::fabs(betaForSpeed(0.5 * c_light, 0.0)) < 0.000001);
    }

    MK_CHECK(std::fabs(properDt(0.0, 2.0)) < 0.000001);
    MK_CHECK(std::fabs(properDt(-1.0, 2.0)) < 0.000001);
    MK_CHECK(std::fabs(properDt(1.0, 0.5) - 1.0) < 0.000001);

    {
        RelativisticState state;
        updateState(nullptr, btVector3(0.0f, 0.0f, 0.0f), 0.0, 1.0, c_light);

        updateState(&state,
                    btVector3((float)(0.6 * c_light), 0.0f, 0.0f),
                    0.6 * c_light, 1.0, c_light);
        MK_CHECK(std::fabs(state.m_beta - 0.6) < 0.000001);
        MK_CHECK(std::fabs(state.m_gamma - 1.25) < 0.00001);
        MK_CHECK(std::fabs(state.m_coordinate_time_s - 1.0) < 0.000001);
        MK_CHECK(std::fabs(state.m_proper_time_s - 0.8) < 0.00001);
        MK_CHECK(std::fabs((double)state.m_coordinate_velocity.getX() -
                           0.6 * c_light) < 0.01);

        const double prev_coord_t = state.m_coordinate_time_s;
        const double prev_proper_t = state.m_proper_time_s;
        updateState(&state, btVector3(0.0f, 0.0f, 0.0f), 0.0, -1.0, c_light);
        MK_CHECK(std::fabs(state.m_coordinate_time_s - prev_coord_t) < 0.000001);
        MK_CHECK(std::fabs(state.m_proper_time_s - prev_proper_t) < 0.000001);
    }

    {
        bool clamp_flag = false;
        const btVector3 slow = clampVelocityToC(
            btVector3(10.0f, 0.0f, 0.0f), 78.4f, &clamp_flag);
        MK_CHECK(!clamp_flag);
        MK_CHECK(std::fabs((double)slow.length() - 10.0) < 0.001);

        const btVector3 zero_vel = clampVelocityToC(
            btVector3(0.0f, 0.0f, 0.0f), 78.4f, &clamp_flag);
        MK_CHECK(!clamp_flag);
        MK_CHECK((double)zero_vel.length2() < 0.000001);

        const btVector3 neg_max = clampVelocityToC(
            btVector3(100.0f, 0.0f, 0.0f), -1.0f, &clamp_flag);
        MK_CHECK(!clamp_flag);
        MK_CHECK(std::fabs((double)neg_max.length() - 100.0) < 0.001);
    }

    MK_CHECK(scaleLongitudinalForce(0.0f, 0.6f * (float)c_light,
                                    (float)c_light) == 0.0f);
    MK_CHECK(scaleLongitudinalForce(-100.0f, 0.6f * (float)c_light,
                                    (float)c_light) == -100.0f);
    MK_CHECK(scaleLongitudinalForce(100.0f, -0.6f * (float)c_light,
                                    (float)c_light) == 100.0f);

    {
        resetDebugCounters();
        MK_CHECK(getVelocityClampCount() == 0u);

        bool dummy = false;
        clampVelocityToC(btVector3(200.0f, 0.0f, 0.0f), 78.4f, &dummy);
        MK_CHECK(getVelocityClampCount() == 1u);

        resetDebugCounters();
        MK_CHECK(getVelocityClampCount() == 0u);
    }

    {
        ObserverVisualState normal_test_observer;
        normal_test_observer.m_valid = true;
        normal_test_observer.m_observer_position = btVector3(0.0f, 0.0f, 0.0f);
        normal_test_observer.m_beta_vector = btVector3(0.6f, 0.0f, 0.0f);
        normal_test_observer.m_gamma =
            (float)gammaForSpeed(0.6 * c_light, c_light);
        normal_test_observer.m_inverse_gamma =
            1.0f / normal_test_observer.m_gamma;
        normal_test_observer.m_c_light = (float)c_light;
        const btVector3 apparent_normal = applyVisualNormal(
            btVector3(5.0f, 1.0f, 0.0f), btVector3(0.0f, 1.0f, 0.0f),
            normal_test_observer);
        MK_CHECK(std::fabs((double)apparent_normal.length() - 1.0) < 0.001);
    }

#undef MK_CHECK
}   // unitTesting

}   // namespace Relativity
