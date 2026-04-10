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

#ifndef HEADER_RELATIVITY_OBSERVER_SNAPSHOT_HPP
#define HEADER_RELATIVITY_OBSERVER_SNAPSHOT_HPP

#include "LinearMath/btVector3.h"

class AbstractKart;

namespace Relativity
{

struct ObserverSnapshot
{
    bool  m_valid;
    float m_beta;
    float m_gamma;
    float m_view_alignment;
    float m_forward_intensity;
    float m_fov_scale;
    float m_camera_distance_scale;
    bool  m_trigger_motion_blur;

    ObserverSnapshot();
};   // struct ObserverSnapshot

struct ObserverVisualState
{
    bool      m_valid;
    float     m_beta;
    float     m_gamma;
    float     m_inverse_gamma;
    btVector3 m_beta_vector;
    btVector3 m_observer_position;

    ObserverVisualState();
};   // struct ObserverVisualState

ObserverSnapshot buildObserverSnapshot(const AbstractKart* observer_kart,
                                       const btVector3& view_direction);

ObserverVisualState buildObserverVisualState(
    const AbstractKart* observer_kart,
    const btVector3& observer_position);

void observerSnapshotUnitTesting();

}   // namespace Relativity

#endif
