//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2011-2015 Joerg Henrichs
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

#ifndef HEADER_REFEREE_HPP
#define HEADER_REFEREE_HPP

namespace irr
{
    namespace scene
    {
        class IAnimatedMesh; class IAnimatedMeshSceneNode; class ISceneNode;
    }
}

using namespace irr;

#include "utils/vec3.hpp"

class AbstractKart;

/**
  * \ingroup graphics
  *  This implements the referee, a character that is displayed at the start
  *  of the race holding a 'ready-set-go' traffic light (or so). It contains
  *  various static functions and variables which are used to store the
  *  original mesh, offsets, rotation, ... of the referee.
  *  Each instance of Referee then has a scene node where the referee is
  *  shown. One instance of this object is used to display the referee
  *  for all(!) karts, i.e. the scene node is moved and rotated before
  *  rendering the view for each of the cameras. This reduces rendering
  *  effect somewhat, and helps making the startup less crowded.
  */
class Referee
{
private:
    /** Static meshes loaded once and shared between referee instances. */
    static scene::IAnimatedMesh *m_st_start_mesh;
    static scene::IAnimatedMesh *m_st_start_light_mesh;
    static scene::IAnimatedMesh *m_st_rescue_mesh;
    static scene::IAnimatedMesh *m_st_rescue_rotor_mesh;

    /** Which mesh buffer to use to show the traffic light texture. */
    static int m_st_traffic_buffer;

    /** Start frame of start animation. */
    static int m_st_first_start_frame;

    /** End frame of start animation. */
    static int m_st_last_start_frame;

    /** Start frame of rescue animation. */
    static int m_st_first_rescue_frame;

    /** End frame of rescue animation. */
    static int m_st_last_rescue_frame;

    /** The position the referee should be shown relative to the kart
     *  when starting the race. */
    static Vec3 m_st_start_offset;

    /** Scaling to be applied to the referee. */
    static Vec3 m_st_start_scale;
    static Vec3 m_st_start_light_offset;
    static Vec3 m_st_start_light_rotation;
    static Vec3 m_st_start_light_scale;
    static Vec3 m_st_rescue_offset;
    static Vec3 m_st_rescue_rotation;
    static Vec3 m_st_rescue_scale;
    static Vec3 m_st_rescue_rotor_offset;
    static Vec3 m_st_rescue_rotor_rotation;
    static Vec3 m_st_rescue_rotor_scale;

    /** A rotation to be applied to the referee before displaying it. */
    static Vec3 m_st_start_rotation;

    static float m_height;

    /** The root scene node for an instance of the referee. */
    scene::IAnimatedMeshSceneNode *m_scene_node;

    scene::IAnimatedMeshSceneNode *m_body_node;
    scene::IAnimatedMeshSceneNode *m_start_light_node;
    scene::IAnimatedMeshSceneNode *m_rescue_rotor_node;

    scene::ISceneNode* m_light;

public:
                Referee();
                Referee(const AbstractKart &kart);
               ~Referee();
    void        selectReadySetGo(int rsg);
    void        attachToSceneNode();
    static void init();
    static void cleanup();
    void        removeFromSceneGraph();
    // ------------------------------------------------------------------------
    /** Returns the scene node of this referee. */
    scene::IAnimatedMeshSceneNode* getSceneNode() { return m_scene_node; }
    // ------------------------------------------------------------------------
    void        setPosition(const Vec3 &xyz);
    // ------------------------------------------------------------------------
    void        setRotation(const Vec3 &hpr);
    // ------------------------------------------------------------------------
    bool        isAttached() const;
    // ------------------------------------------------------------------------
    void        setAnimationFrameWithCreatedTicks(int created_ticks);
    // ------------------------------------------------------------------------
    void        updateRescueVisuals(int created_ticks);
    // ------------------------------------------------------------------------
    /** Returns the graphical offset the referee should be drawn at at the
     *  start of a race. */
    static const Vec3& getStartOffset() {return m_st_start_offset; }
    // ------------------------------------------------------------------------
    /** Returns the rotation of the mesh so that it faces the kart (when
     *  applied to a kart with heading 0). */
    static const Vec3& getStartRotation() {return m_st_start_rotation; }
    // ------------------------------------------------------------------------
    /** Returns the height of the referee. */
    static const float getHeight() {return m_height; }
};   // Referee

#endif

/* EOF */
