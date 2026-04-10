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

#ifndef HEADER_RELATIVISTIC_STATE_HPP
#define HEADER_RELATIVISTIC_STATE_HPP

#include "LinearMath/btVector3.h"

namespace Relativity
{

/** Coordinate-frame relativistic diagnostics for one simulated body.
 *  The race still uses WorldStatus coordinate time; this state is kept
 *  separately so proper time does not leak into arcade/gameplay timers.
 */
struct RelativisticState
{
    double    m_coordinate_time_s;
    double    m_proper_time_s;
    double    m_speed;
    double    m_beta;
    double    m_gamma;
    btVector3 m_coordinate_velocity;

    RelativisticState()
    {
        reset();
    }

    void reset()
    {
        m_coordinate_time_s = 0.0;
        m_proper_time_s = 0.0;
        m_speed = 0.0;
        m_beta = 0.0;
        m_gamma = 1.0;
        m_coordinate_velocity = btVector3(0.0f, 0.0f, 0.0f);
    }
};   // struct RelativisticState

}   // namespace Relativity

#endif
