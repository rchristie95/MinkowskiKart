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
#include "network/network_config.hpp"
#include "relativity/relativistic_state.hpp"
#include "utils/log.hpp"

#include <assert.h>
#include <cmath>
#include <limits>

namespace
{
const double MIN_LIGHT_SPEED = 0.001;
const double MAX_BETA_EPSILON = 1.0e-9;

bool isFiniteVector(const btVector3& v)
{
    return std::isfinite((double)v.x()) &&
           std::isfinite((double)v.y()) &&
           std::isfinite((double)v.z());
}   // isFiniteVector

double clampAbsBeta(double beta)
{
    if (!std::isfinite(beta) || beta < 0.0)
        return 0.0;
    if (beta >= 1.0)
        return 1.0 - MAX_BETA_EPSILON;
    return beta;
}   // clampAbsBeta

}   // anonymous namespace

namespace Relativity
{

bool isEnabled()
{
    if (!stk_config || !stk_config->m_relativity_enabled)
        return false;

    if (NetworkConfig::get()->isNetworking())
    {
        static bool warned = false;
        if (!warned)
        {
            Log::warn("Relativity",
                      "Relativistic mechanics are disabled for network games.");
            warned = true;
        }
        return false;
    }

    return true;
}   // isEnabled

// ----------------------------------------------------------------------------
bool isPropulsionLimited()
{
    return isEnabled() &&
           stk_config->m_relativity_mode == "propulsion-limited";
}   // isPropulsionLimited

// ----------------------------------------------------------------------------
float getConfiguredSpeedOfLight()
{
    if (!stk_config ||
        !std::isfinite((double)stk_config->m_relativity_speed_of_light) ||
        stk_config->m_relativity_speed_of_light <= 0.0f)
    {
        return 80.0f;
    }
    return stk_config->m_relativity_speed_of_light;
}   // getConfiguredSpeedOfLight

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
}   // unitTesting

}   // namespace Relativity
