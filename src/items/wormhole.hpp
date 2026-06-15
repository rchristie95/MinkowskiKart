//
//  MinkowskiKart - a fun racing game with go-kart
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

#ifndef HEADER_WORMHOLE_HPP
#define HEADER_WORMHOLE_HPP

#include <array>
#include <vector>

#include "items/flyable.hpp"
#include "utils/cpp2011.hpp"

class AbstractKart;
class PhysicalObject;
class SFXBase;
class XMLNode;

namespace irr
{
    namespace scene
    {
        class ICameraSceneNode;
        class ISceneNode;
        class IMesh;
    }
    namespace video { class ITexture; }
}

/**
  * \ingroup items
  */
class Wormhole : public Flyable
{
private:
    static float m_st_entry_distance;
    static float m_st_exit_distance;
    static float m_st_lifetime;
    static float m_st_reentry_cooldown;
    static float m_st_trigger_radius;
    static float m_st_visual_radius;

    btTransform        m_endpoint_transforms[2];
    int                m_expiry_ticks;
    int                m_reentry_cooldown_ticks;
    std::vector<int>   m_kart_cooldowns;
    bool               m_have_endpoints;

    static btQuaternion buildOrientation(const btVector3 &forward,
                                         const btVector3 &up);
    static btVector3    getForward(const btTransform &transform);
    static btVector3    getUp(const btTransform &transform);
    static btVector3    transformVectorBetween(const btTransform &src,
                                               const btTransform &dst,
                                               const btVector3 &world_vector);
    static btTransform  offsetAlongForward(const btTransform &transform,
                                           float distance);

    void                ensureCooldownCapacity();
    void                alignBodyToEndpoints();
    void                updateLensingAnchor() const;
    bool                sampleTrackPlacement(float distance_ahead,
                                             btTransform *out_transform) const;
    btTransform         buildEntryTransform() const;
    btTransform         buildFallbackExitTransform() const;
    btVector3           projectToTrack(const btVector3 &position,
                                       const btVector3 &fallback_up) const;
    void                teleportKart(AbstractKart *kart, int source_index);
    void                updateTeleporters();

#ifndef SERVER_ONLY
    struct EndpointVisual
    {
        scene::ISceneNode      *mouth_node;
        scene::ISceneNode      *halo_front;
        scene::ISceneNode      *halo_back;
        scene::ICameraSceneNode*camera;
        video::ITexture        *texture;

        EndpointVisual()
            : mouth_node(nullptr), halo_front(nullptr), halo_back(nullptr),
              camera(nullptr), texture(nullptr)
        {
        }
    };

    std::array<EndpointVisual, 2> m_visuals;
    SFXBase*           m_spawn_sfx;
    SFXBase*           m_teleport_sfx;

    void                createVisuals();
    void                destroyVisuals();
    void                updateVisualState(float dt);
    void                updateRenderTargets();
    void                playPortalSound(const btVector3 &position);
#endif

public:
                     Wormhole(AbstractKart* kart);
    virtual         ~Wormhole();
    static  void     init(const XMLNode &node, scene::IMesh *rubberball);
    static  void     unitTesting();
    virtual void     onFireFlyable() OVERRIDE;
    virtual bool     updateAndDelete(int ticks) OVERRIDE;
    virtual bool     hit(AbstractKart* kart, PhysicalObject* obj=NULL) OVERRIDE;
    virtual HitEffect* getHitEffect() const OVERRIDE;
    virtual void     updateGraphics(float dt) OVERRIDE;
    virtual BareNetworkString* saveState(std::vector<std::string>* ru)
        OVERRIDE;
    virtual void     restoreState(BareNetworkString *buffer, int count) OVERRIDE;

    void             getMinimapPositions(std::vector<Vec3>* positions) const;

    // ------------------------------------------------------------------------
    /** Returns true once both endpoints have been placed. */
    bool             hasEndpoints() const             { return m_have_endpoints; }
    // ------------------------------------------------------------------------
    /** World position of the entrance (endpoint 0). Driving into it jumps the
     *  kart forwards. */
    const btVector3& getEntryPosition() const
                                 { return m_endpoint_transforms[0].getOrigin(); }
    // ------------------------------------------------------------------------
    /** World position of the exit (endpoint 1). Note the teleporter is
     *  bidirectional, so driving into the exit jumps the kart backwards. */
    const btVector3& getExitPosition() const
                                 { return m_endpoint_transforms[1].getOrigin(); }
    // ------------------------------------------------------------------------
    /** The radius around an endpoint within which a kart is teleported. */
    static float     getTriggerRadius()           { return m_st_trigger_radius; }
};   // Wormhole

#endif
