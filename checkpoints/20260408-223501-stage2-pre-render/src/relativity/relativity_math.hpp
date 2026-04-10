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

#ifndef HEADER_RELATIVITY_MATH_HPP
#define HEADER_RELATIVITY_MATH_HPP

#include "LinearMath/btVector3.h"

namespace Relativity
{

struct RelativisticState;

bool isEnabled();
bool isPropulsionLimited();
bool isPreferredFrameDynamics();

float getConfiguredSpeedOfLight();
float getConfiguredMaxBeta();
float getMaxCoordinateSpeed();

double betaForSpeed(double speed, double speed_of_light);
double gammaForSpeed(double speed, double speed_of_light);
double properDt(double coordinate_dt, double gamma);

void updateState(RelativisticState *state,
                 const btVector3& coordinate_velocity,
                 double signed_speed,
                 double coordinate_dt,
                 double speed_of_light);

btVector3 clampVelocityToC(const btVector3& velocity,
                           float max_coordinate_speed,
                           bool *was_clamped = 0);

float scaleLongitudinalForce(float force, float signed_speed,
                             float speed_of_light);
btVector3 scalePreferredFrameResponse(const btVector3& response_vector,
                                      const btVector3& coordinate_velocity,
                                      float speed_of_light);

unsigned int getVelocityClampCount();
unsigned int getResponseScaleCount();
void resetDebugCounters();

namespace KartAdapter
{
float scalePropulsiveForce(float force, float signed_speed);
btVector3 clampVelocity(const btVector3& velocity, bool *was_clamped = 0);
btVector3 scaleResponse(const btVector3& response_vector,
                        const btVector3& coordinate_velocity);
}

void unitTesting();

}   // namespace Relativity

#endif
