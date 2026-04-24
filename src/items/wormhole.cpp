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

#include "items/wormhole.hpp"

#include "audio/sfx_base.hpp"
#include "audio/sfx_manager.hpp"
#include "config/stk_config.hpp"
#include "graphics/camera/camera.hpp"
#include "graphics/irr_driver.hpp"
#include "graphics/sp/sp_base.hpp"
#include "graphics/sp/sp_mesh_node.hpp"
#include "guiengine/engine.hpp"
#include "io/file_manager.hpp"
#include "io/xml_node.hpp"
#include "karts/abstract_kart.hpp"
#include "modes/linear_world.hpp"
#include "modes/world.hpp"
#include "network/network_string.hpp"
#include "physics/triangle_mesh.hpp"
#include "tracks/drive_graph.hpp"
#include "tracks/drive_node.hpp"
#include "tracks/graph.hpp"
#include "tracks/track.hpp"
#include "utils/string_utils.hpp"

#ifndef SERVER_ONLY
#include <ICameraSceneNode.h>
#include <ISceneNode.h>
#include <ISceneManager.h>
#include <IVideoDriver.h>
#endif

#include <algorithm>
#include <cmath>
#include <sstream>

float Wormhole::m_st_entry_distance = 20.0f;
float Wormhole::m_st_exit_distance = 100.0f;
float Wormhole::m_st_lifetime = 20.0f;
float Wormhole::m_st_reentry_cooldown = 0.5f;
float Wormhole::m_st_trigger_radius = 3.5f;
float Wormhole::m_st_visual_radius = 3.0f;

// ----------------------------------------------------------------------------
Wormhole::Wormhole(AbstractKart *kart)
    : Flyable(kart, PowerupManager::POWERUP_WORMHOLE, 0.0f /* mass */),
      m_expiry_ticks(0),
      m_reentry_cooldown_ticks(0),
      m_have_endpoints(false)
#ifndef SERVER_ONLY
    , m_spawn_sfx(nullptr)
    , m_teleport_sfx(nullptr)
#endif
{
    m_endpoint_transforms[0].setIdentity();
    m_endpoint_transforms[1].setIdentity();

#ifndef SERVER_ONLY
    m_spawn_sfx = SFXManager::get()->createSoundSource("portal");
    m_teleport_sfx = SFXManager::get()->createSoundSource("portal");
    fixSFXSplitscreen(m_spawn_sfx);
    fixSFXSplitscreen(m_teleport_sfx);
#endif
}   // Wormhole

// ----------------------------------------------------------------------------
Wormhole::~Wormhole()
{
#ifndef SERVER_ONLY
    destroyVisuals();
    if (m_spawn_sfx)
        m_spawn_sfx->deleteSFX();
    if (m_teleport_sfx)
        m_teleport_sfx->deleteSFX();
#endif

    // This is only a supplemental post effect. The live wormhole instance
    // that updates last will republish its location on the next frame.
    SP::sp_wormhole_active = false;
    SP::sp_wormhole_radius = 0.0f;
}   // ~Wormhole

// ----------------------------------------------------------------------------
btQuaternion Wormhole::buildOrientation(const btVector3 &forward,
                                        const btVector3 &up)
{
    btVector3 f = forward.length2() > 0.0001f ? forward.normalized()
                                              : btVector3(0.0f, 0.0f, 1.0f);
    btVector3 u = up.length2() > 0.0001f ? up.normalized()
                                         : btVector3(0.0f, 1.0f, 0.0f);
    btVector3 r = u.cross(f);
    if (r.length2() < 0.0001f)
        r = btVector3(1.0f, 0.0f, 0.0f);
    r.normalize();
    u = f.cross(r).normalized();

    btMatrix3x3 basis(r.getX(), u.getX(), f.getX(),
                      r.getY(), u.getY(), f.getY(),
                      r.getZ(), u.getZ(), f.getZ());
    btQuaternion q;
    basis.getRotation(q);
    return q;
}   // buildOrientation

// ----------------------------------------------------------------------------
btVector3 Wormhole::getForward(const btTransform &transform)
{
    return transform.getBasis().getColumn(2).normalized();
}   // getForward

// ----------------------------------------------------------------------------
btVector3 Wormhole::getUp(const btTransform &transform)
{
    return transform.getBasis().getColumn(1).normalized();
}   // getUp

// ----------------------------------------------------------------------------
btVector3 Wormhole::transformVectorBetween(const btTransform &src,
                                           const btTransform &dst,
                                           const btVector3 &world_vector)
{
    const btVector3 local = src.getBasis().transpose() * world_vector;
    return dst.getBasis() * local;
}   // transformVectorBetween

// ----------------------------------------------------------------------------
btTransform Wormhole::offsetAlongForward(const btTransform &transform,
                                         float distance)
{
    btTransform result = transform;
    result.setOrigin(transform.getOrigin() + getForward(transform) * distance);
    return result;
}   // offsetAlongForward

// ----------------------------------------------------------------------------
void Wormhole::ensureCooldownCapacity()
{
    World *world = World::getWorld();
    if (!world)
        return;
    m_kart_cooldowns.resize(world->getNumKarts(), 0);
}   // ensureCooldownCapacity

// ----------------------------------------------------------------------------
btVector3 Wormhole::projectToTrack(const btVector3 &position,
                                   const btVector3 &fallback_up) const
{
    const TriangleMesh &tm = Track::getCurrentTrack()->getTriangleMesh();
    Vec3 hit_point;
    const Material *material = nullptr;
    const btVector3 up = fallback_up.length2() > 0.0001f
        ? fallback_up.normalized() : btVector3(0.0f, 1.0f, 0.0f);
    const Vec3 from(position + up * 60.0f);
    const Vec3 to(position - up * 120.0f);
    tm.castRay(from, to, &hit_point, &material);
    return material ? btVector3(hit_point) : position;
}   // projectToTrack

// ----------------------------------------------------------------------------
btTransform Wormhole::buildEntryTransform() const
{
    const btTransform owner = m_owner->getTrans();
    const btVector3 forward = getForward(owner);
    const btVector3 up = getUp(owner);

    btTransform entry;
    entry.setIdentity();
    btVector3 entry_position = m_owner->getXYZ() + forward * m_st_entry_distance;
    entry_position = projectToTrack(entry_position, up)
                   + up * (m_st_visual_radius * 0.12f);
    entry.setOrigin(entry_position);
    entry.setRotation(buildOrientation(forward, up));
    return entry;
}   // buildEntryTransform

// ----------------------------------------------------------------------------
btTransform Wormhole::buildFallbackExitTransform() const
{
    const btTransform owner = m_owner->getTrans();
    const btVector3 forward = getForward(owner);
    const btVector3 up = getUp(owner);

    btTransform exit;
    exit.setIdentity();
    btVector3 exit_position = m_owner->getXYZ() + forward * m_st_exit_distance;
    exit_position = projectToTrack(exit_position, up)
                  + up * (m_st_visual_radius * 0.12f);
    exit.setOrigin(exit_position);
    exit.setRotation(buildOrientation(forward, up));
    return exit;
}   // buildFallbackExitTransform

// ----------------------------------------------------------------------------
bool Wormhole::sampleTrackPlacement(float distance_ahead,
                                    btTransform *out_transform) const
{
    LinearWorld *world = dynamic_cast<LinearWorld*>(World::getWorld());
    DriveGraph *graph = DriveGraph::get();
    if (!world || !graph)
        return false;

    const int sector = world->getSectorForKart(m_owner);
    if (sector == Graph::UNKNOWN_SECTOR || sector < 0)
        return false;

    const DriveNode *node = graph->getNode(sector);
    if (!node || node->getNumberOfSuccessors() == 0)
        return false;

    btVector3 current = m_owner->getXYZ();
    btVector3 up = node->getNormal();
    unsigned int successor = node->getSuccessor(0);
    btVector3 next = graph->getNode(successor)->getCenter();
    btVector3 segment = next - current;
    if (segment.length2() < 0.0001f)
    {
        current = node->getCenter();
        segment = next - current;
    }

    btVector3 forward = segment.length2() > 0.0001f
        ? segment.normalized() : getForward(m_owner->getTrans());
    float remaining = distance_ahead;

    for (int guard = 0; guard < 512; guard++)
    {
        const float segment_length = segment.length();
        if (segment_length < 0.0001f)
            break;

        if (segment_length >= remaining)
        {
            btVector3 position = current + forward * remaining;
            position = projectToTrack(position, up)
                     + up * (m_st_visual_radius * 0.12f);
            out_transform->setIdentity();
            out_transform->setOrigin(position);
            out_transform->setRotation(buildOrientation(forward, up));
            return true;
        }

        remaining -= segment_length;
        current = next;
        node = graph->getNode(successor);
        if (!node || node->getNumberOfSuccessors() == 0)
            break;

        up = node->getNormal();
        successor = node->getSuccessor(0);
        next = graph->getNode(successor)->getCenter();
        segment = next - current;
        if (segment.length2() > 0.0001f)
            forward = segment.normalized();
    }

    return false;
}   // sampleTrackPlacement

// ----------------------------------------------------------------------------
void Wormhole::alignBodyToEndpoints()
{
    btTransform midpoint = m_endpoint_transforms[0];
    midpoint.setOrigin((m_endpoint_transforms[0].getOrigin()
                      + m_endpoint_transforms[1].getOrigin()) * 0.5f);
    getBody()->setCenterOfMassTransform(midpoint);
    getBody()->proceedToTransform(midpoint);
    setTrans(midpoint);
}   // alignBodyToEndpoints

// ----------------------------------------------------------------------------
void Wormhole::updateLensingAnchor() const
{
    if (!m_have_endpoints)
    {
        SP::sp_wormhole_active = false;
        SP::sp_wormhole_radius = 0.0f;
        return;
    }

    // Pick whichever endpoint is closer to the active camera so the
    // Interstellar-style lens sits on the mouth the player is actually
    // looking at. Falls back to endpoint 0 when no camera is available
    // (e.g. during lobby / screenshot code paths).
    int anchor_index = 0;
    Camera *active_cam = Camera::getActiveCamera();
    if (active_cam && active_cam->getCameraSceneNode())
    {
        const core::vector3df cam_pos =
            active_cam->getCameraSceneNode()->getAbsolutePosition();
        const btVector3 btcam(cam_pos.X, cam_pos.Y, cam_pos.Z);
        const btScalar d0 =
            (m_endpoint_transforms[0].getOrigin() - btcam).length2();
        const btScalar d1 =
            (m_endpoint_transforms[1].getOrigin() - btcam).length2();
        anchor_index = (d1 < d0) ? 1 : 0;
    }

    // Apply the same collapse curve the visuals use so the lens shrinks
    // as the wormhole ages out, instead of popping off all at once.
    const float remaining = stk_config->ticks2Time(
        std::max(0, m_expiry_ticks - World::getWorld()->getTicksSinceStart()));
    const float collapse = remaining < 1.0f ? std::max(0.0f, remaining) : 1.0f;

    SP::sp_wormhole_world_pos =
        Vec3(m_endpoint_transforms[anchor_index].getOrigin()).toIrrVector();
    SP::sp_wormhole_radius  = m_st_visual_radius * collapse;
    SP::sp_wormhole_active  = true;
}   // updateLensingAnchor

// ----------------------------------------------------------------------------
void Wormhole::teleportKart(AbstractKart *kart, int source_index)
{
    const int destination_index = 1 - source_index;
    const btTransform& source = m_endpoint_transforms[source_index];
    const btTransform& destination = m_endpoint_transforms[destination_index];
    const btVector3 dst_forward = getForward(destination);

    btVector3 planar_offset = source.getBasis().transpose()
                            * (kart->getXYZ() - source.getOrigin());
    planar_offset.setZ(0.0f);

    btTransform kart_transform = kart->getTrans();
    btTransform new_transform = kart_transform;
    btVector3 exit_position = destination.getOrigin()
                            + destination.getBasis() * planar_offset
                            + dst_forward * (m_st_trigger_radius
                                            + kart->getKartLength() * 0.6f);
    new_transform.setOrigin(exit_position);
    new_transform.setRotation(destination.getRotation()
                            * source.getRotation().inverse()
                            * kart_transform.getRotation());

    btVector3 linear_velocity =
        transformVectorBetween(source, destination, kart->getBody()->getLinearVelocity());
    btVector3 angular_velocity =
        transformVectorBetween(source, destination, kart->getBody()->getAngularVelocity());

    kart->getBody()->clearForces();
    kart->getBody()->setCenterOfMassTransform(new_transform);
    kart->getBody()->proceedToTransform(new_transform);
    kart->getBody()->setLinearVelocity(linear_velocity);
    kart->getBody()->setAngularVelocity(angular_velocity);
    kart->getBody()->activate(true);

    kart->setTrans(new_transform);
    kart->setXYZ(exit_position);
    kart->setRotation(new_transform.getRotation());
    kart->setVelocity(linear_velocity);
    kart->setSpeed(linear_velocity.length());

    ensureCooldownCapacity();
    m_kart_cooldowns[kart->getWorldKartId()] =
        World::getWorld()->getTicksSinceStart() + m_reentry_cooldown_ticks;

#ifndef SERVER_ONLY
    playPortalSound(exit_position);
#endif
}   // teleportKart

// ----------------------------------------------------------------------------
void Wormhole::updateTeleporters()
{
    World *world = World::getWorld();
    if (!world || !m_have_endpoints)
        return;

    ensureCooldownCapacity();
    const int now = world->getTicksSinceStart();
    const float trigger_radius_sq = m_st_trigger_radius * m_st_trigger_radius;

    for (unsigned int i = 0; i < world->getNumKarts(); i++)
    {
        AbstractKart *kart = world->getKart(i);
        if (!kart || kart->isGhostKart() || kart->isEliminated() ||
            kart->getKartAnimation() || m_kart_cooldowns[i] > now)
        {
            continue;
        }

        const btVector3 kart_position = kart->getXYZ();
        for (int endpoint = 0; endpoint < 2; endpoint++)
        {
            const btVector3 delta = kart_position - m_endpoint_transforms[endpoint].getOrigin();
            if (delta.length2() <= trigger_radius_sq)
            {
                teleportKart(kart, endpoint);
                break;
            }
        }
    }
}   // updateTeleporters

#ifndef SERVER_ONLY
// ----------------------------------------------------------------------------
void Wormhole::playPortalSound(const btVector3 &position)
{
    if (!m_teleport_sfx)
        return;
    m_teleport_sfx->setPosition(Vec3(position));
    m_teleport_sfx->play();
}   // playPortalSound

// ----------------------------------------------------------------------------
void Wormhole::destroyVisuals()
{
    for (size_t i = 0; i < m_visuals.size(); i++)
    {
        if (m_visuals[i].mouth_node)
            irr_driver->removeNode(m_visuals[i].mouth_node);
        if (m_visuals[i].halo_front)
            irr_driver->removeNode(m_visuals[i].halo_front);
        if (m_visuals[i].halo_back)
            irr_driver->removeNode(m_visuals[i].halo_back);
        if (m_visuals[i].camera)
            irr_driver->removeCameraSceneNode(m_visuals[i].camera);
        m_visuals[i] = EndpointVisual();
    }
}   // destroyVisuals

// ----------------------------------------------------------------------------
void Wormhole::createVisuals()
{
    if (GUIEngine::isNoGraphics())
        return;

    destroyVisuals();

    // The wormhole should read as warped space, not as a physical blue/white
    // object. Endpoint visibility now comes from the screen-space lensing pass
    // in tonemap.frag, so no sphere or halo scene nodes are spawned here.
    if (m_spawn_sfx)
    {
        m_spawn_sfx->setPosition(Vec3(m_endpoint_transforms[0].getOrigin()));
        m_spawn_sfx->play();
    }
    if (m_node)
        m_node->setVisible(false);
    return;

    video::ITexture *fallback_texture =
        irr_driver->getTexture(FileManager::GUI_ICON, "wormhole-icon.png");
    // Halo colour is kept subtle now that the tonemap post-process draws
    // the bright Einstein ring on the silhouette. A strong glow here
    // would fight the lensing ring and wash it out.
    const video::SColor halo_colour(80, 40, 90, 160);
    const core::dimension2du rtt_size(256, 256);

    for (size_t i = 0; i < m_visuals.size(); i++)
    {
        EndpointVisual &visual = m_visuals[i];
        visual.mouth_node = irr_driver->addSphere(1.0f, video::SColor(255, 255, 255, 255));
        visual.halo_front = irr_driver->addSphere(1.0f, halo_colour);
        visual.halo_back = irr_driver->addSphere(1.0f, halo_colour);
        visual.texture = fallback_texture;

        scene::ISceneNode* nodes[] = {
            visual.mouth_node, visual.halo_front, visual.halo_back
        };
        for (scene::ISceneNode *node : nodes)
        {
            if (!node)
                continue;
            // Visuals start hidden until updateVisualState positions them at
            // the real endpoints. Otherwise they sit at world origin (0,0,0)
            // scaled and pulsing, which pollutes the shadow pass and causes
            // the whole track to flicker between lit and shaded cascades
            // until the first visual update lands.
            node->setVisible(false);
            node->setAutomaticCulling(scene::EAC_OFF);
            node->getMaterial(0).Lighting = false;
            node->getMaterial(0).BackfaceCulling = false;
            node->getMaterial(0).EmissiveColor = halo_colour;

            // These are pure effect geometry — they must never cast shadows.
            // Any shadow they cast would sample the per-frame scale pulse and
            // position jitter, making every surface receiving the directional
            // light shimmer on and off.
            if (SP::SPMeshNode *spmn = dynamic_cast<SP::SPMeshNode*>(node))
                spmn->setInShadowPass(false);
        }

        if (visual.mouth_node)
        {
            visual.mouth_node->getMaterial(0).MaterialType = video::EMT_SOLID;
            visual.mouth_node->getMaterial(0).setTexture(
                0, visual.texture ? visual.texture : fallback_texture);
        }
        if (visual.halo_front)
        {
            visual.halo_front->getMaterial(0).MaterialType =
                video::EMT_TRANSPARENT_ADD_COLOR;
            visual.halo_front->getMaterial(0).setTexture(0, fallback_texture);
        }
        if (visual.halo_back)
        {
            visual.halo_back->getMaterial(0).MaterialType =
                video::EMT_TRANSPARENT_ADD_COLOR;
            visual.halo_back->getMaterial(0).setTexture(0, fallback_texture);
        }

        if (visual.camera)
        {
            visual.camera->setNearValue(0.05f);
            visual.camera->setFarValue(1600.0f);
            visual.camera->setFOV(irr::core::PI / 2.8f);
        }
    }

    if (m_spawn_sfx)
    {
        m_spawn_sfx->setPosition(Vec3(m_endpoint_transforms[0].getOrigin()));
        m_spawn_sfx->play();
    }
}   // createVisuals

// ----------------------------------------------------------------------------
void Wormhole::updateVisualState(float dt)
{
    if (!m_have_endpoints)
        return;

    const float remaining = stk_config->ticks2Time(
        std::max(0, m_expiry_ticks - World::getWorld()->getTicksSinceStart()));
    const float collapse = remaining < 1.0f ? std::max(0.0f, remaining) : 1.0f;
    const float pulse = 1.0f + 0.08f * sinf(World::getWorld()->getTime() * 5.0f);

    for (int i = 0; i < 2; i++)
    {
        EndpointVisual &visual = m_visuals[i];
        if (!visual.mouth_node)
            continue;

        const btTransform &endpoint = m_endpoint_transforms[i];
        const btVector3 forward = getForward(endpoint);
        const btQuaternion rotation = endpoint.getRotation();
        Vec3 hpr;
        hpr = rotation;

        const float radius = m_st_visual_radius * collapse;
        const core::vector3df mouth_scale(radius * pulse,
                                          radius * pulse,
                                          radius * 0.16f * collapse);
        const core::vector3df halo_scale(radius * 0.72f * pulse,
                                         radius * 0.72f * pulse,
                                         radius * 0.28f * collapse);

        visual.mouth_node->setPosition(
            Vec3(endpoint.getOrigin()).toIrrVector());
        visual.mouth_node->setRotation(hpr.toIrrHPR());
        visual.mouth_node->setScale(mouth_scale);
        visual.mouth_node->setVisible(true);

        if (visual.halo_front)
        {
            visual.halo_front->setPosition(
                Vec3(endpoint.getOrigin() + forward * (radius * 0.32f))
                    .toIrrVector());
            visual.halo_front->setRotation(hpr.toIrrHPR());
            visual.halo_front->setScale(halo_scale);
            visual.halo_front->setVisible(true);
        }
        if (visual.halo_back)
        {
            visual.halo_back->setPosition(
                Vec3(endpoint.getOrigin() - forward * (radius * 0.32f))
                    .toIrrVector());
            visual.halo_back->setRotation(hpr.toIrrHPR());
            visual.halo_back->setScale(halo_scale);
            visual.halo_back->setVisible(true);
        }
    }

    if (m_node)
        m_node->setVisible(false);
}   // updateVisualState

// ----------------------------------------------------------------------------
void Wormhole::updateRenderTargets()
{
    // Do not render portal RTTs from here. Calling ISceneManager::drawAll()
    // inside item graphics update bypasses the shader renderer's shadow and
    // framebuffer pipeline, leaving global GL state unstable for the main
    // world pass. The tonemap lensing stays active; true see-through portals
    // need to be implemented through ShaderBasedRenderer.
}   // updateRenderTargets
#endif

// ----------------------------------------------------------------------------
void Wormhole::init(const XMLNode &node, scene::IMesh *rubberball)
{
    node.get("entry-distance", &m_st_entry_distance);
    node.get("exit-distance", &m_st_exit_distance);
    node.get("lifetime", &m_st_lifetime);
    node.get("reentry-cooldown", &m_st_reentry_cooldown);

    if (m_st_entry_distance < 1.0f) m_st_entry_distance = 1.0f;
    if (m_st_exit_distance <= m_st_entry_distance)
        m_st_exit_distance = m_st_entry_distance + 20.0f;
    if (m_st_lifetime < 1.0f) m_st_lifetime = 1.0f;
    if (m_st_reentry_cooldown < 0.05f) m_st_reentry_cooldown = 0.05f;

    Flyable::init(node, rubberball, PowerupManager::POWERUP_WORMHOLE);

    const Vec3 &extent = m_st_extend[PowerupManager::POWERUP_WORMHOLE];
    const float extent_radius = std::max(std::max(extent.getX(), extent.getY()),
                                         extent.getZ());
    m_st_trigger_radius = std::max(3.0f, extent_radius * 1.4f);
    m_st_visual_radius = std::max(2.8f, m_st_trigger_radius * 0.8f);
}   // init

// ----------------------------------------------------------------------------
void Wormhole::unitTesting()
{
    btTransform source;
    source.setIdentity();
    source.setRotation(buildOrientation(btVector3(0.0f, 0.0f, 1.0f),
                                        btVector3(0.0f, 1.0f, 0.0f)));
    btTransform destination;
    destination.setIdentity();
    destination.setRotation(buildOrientation(btVector3(1.0f, 0.0f, 0.0f),
                                             btVector3(0.0f, 1.0f, 0.0f)));

    const btTransform offset = offsetAlongForward(source, 20.0f);
    assert((offset.getOrigin() - btVector3(0.0f, 0.0f, 20.0f)).length() < 0.001f);

    const btVector3 velocity(0.0f, 0.0f, 42.0f);
    const btVector3 rotated = transformVectorBetween(source, destination, velocity);
    assert(fabs(rotated.length() - velocity.length()) < 0.001f);
    assert(fabs(rotated.getX() - 42.0f) < 0.05f);
}   // unitTesting

// ----------------------------------------------------------------------------
void Wormhole::onFireFlyable()
{
    Flyable::onFireFlyable();
    setDoTerrainInfo(false);
    setAdjustUpVelocity(false);
    ensureCooldownCapacity();
    std::fill(m_kart_cooldowns.begin(), m_kart_cooldowns.end(), 0);

    btTransform midpoint = buildEntryTransform();
    midpoint.setOrigin((midpoint.getOrigin() + buildFallbackExitTransform().getOrigin()) * 0.5f);
    createPhysics(0.0f, btVector3(0.0f, 0.0f, 0.0f),
                  new btSphereShape(std::max(0.5f, m_st_trigger_radius * 0.25f)),
                  0.0f, btVector3(0.0f, 0.0f, 0.0f),
                  false /* rotates */, false /* backwards */, &midpoint);
    int flags = getBody()->getCollisionFlags();
    getBody()->setCollisionFlags(flags | btCollisionObject::CF_NO_CONTACT_RESPONSE);
    getBody()->setLinearVelocity(btVector3(0.0f, 0.0f, 0.0f));
    getBody()->setAngularVelocity(btVector3(0.0f, 0.0f, 0.0f));

    m_endpoint_transforms[0] = buildEntryTransform();
    if (!sampleTrackPlacement(m_st_exit_distance, &m_endpoint_transforms[1]))
        m_endpoint_transforms[1] = buildFallbackExitTransform();

    alignBodyToEndpoints();
    m_have_endpoints = true;
    m_expiry_ticks = World::getWorld()->getTicksSinceStart()
                   + stk_config->time2Ticks(m_st_lifetime);
    m_reentry_cooldown_ticks = stk_config->time2Ticks(m_st_reentry_cooldown);

    if (m_node)
        m_node->setVisible(false);

#ifndef SERVER_ONLY
    createVisuals();
#endif
    updateLensingAnchor();
}   // onFireFlyable

// ----------------------------------------------------------------------------
bool Wormhole::updateAndDelete(int ticks)
{
    if (!m_has_server_state || !m_have_endpoints)
        return false;

    if (m_ticks_since_thrown < 32767)
        m_ticks_since_thrown += ticks;

    if (World::getWorld()->getTicksSinceStart() >= m_expiry_ticks)
        return true;

    alignBodyToEndpoints();
    Moveable::update(ticks);
    updateTeleporters();
    updateLensingAnchor();
    return false;
}   // updateAndDelete

// ----------------------------------------------------------------------------
bool Wormhole::hit(AbstractKart* kart, PhysicalObject* obj)
{
    (void)kart;
    (void)obj;
    return false;
}   // hit

// ----------------------------------------------------------------------------
HitEffect* Wormhole::getHitEffect() const
{
    return nullptr;
}   // getHitEffect

// ----------------------------------------------------------------------------
void Wormhole::updateGraphics(float dt)
{
    Flyable::updateGraphics(dt);
    if (!m_have_endpoints)
        return;

#ifndef SERVER_ONLY
    updateVisualState(dt);
    updateRenderTargets();
#endif
    updateLensingAnchor();
}   // updateGraphics

// ----------------------------------------------------------------------------
BareNetworkString* Wormhole::saveState(std::vector<std::string>* ru)
{
    BareNetworkString* buffer = Flyable::saveState(ru);
    if (!buffer)
        return nullptr;

    const int now = World::getWorld()->getTicksSinceStart();
    buffer->addUInt8(m_have_endpoints ? 1 : 0);
    for (int i = 0; i < 2; i++)
    {
        buffer->add(Vec3(m_endpoint_transforms[i].getOrigin()));
        const btQuaternion q = m_endpoint_transforms[i].getRotation();
        buffer->addFloat(q.getX());
        buffer->addFloat(q.getY());
        buffer->addFloat(q.getZ());
        buffer->addFloat(q.getW());
    }
    buffer->addUInt16((uint16_t)std::max(0, m_expiry_ticks - now));
    buffer->addUInt16((uint16_t)m_reentry_cooldown_ticks);

    ensureCooldownCapacity();
    buffer->addUInt8((uint8_t)m_kart_cooldowns.size());
    for (size_t i = 0; i < m_kart_cooldowns.size(); i++)
        buffer->addUInt16((uint16_t)std::max(0, m_kart_cooldowns[i] - now));

    return buffer;
}   // saveState

// ----------------------------------------------------------------------------
void Wormhole::restoreState(BareNetworkString *buffer, int count)
{
    Flyable::restoreState(buffer, count);
    m_have_endpoints = buffer->getUInt8() != 0;
    for (int i = 0; i < 2; i++)
    {
        const btVector3 origin = buffer->getVec3();
        btQuaternion rotation;
        rotation.setX(buffer->getFloat());
        rotation.setY(buffer->getFloat());
        rotation.setZ(buffer->getFloat());
        rotation.setW(buffer->getFloat());
        m_endpoint_transforms[i].setIdentity();
        m_endpoint_transforms[i].setOrigin(origin);
        m_endpoint_transforms[i].setRotation(rotation);
    }

    const int now = World::getWorld()->getTicksSinceStart();
    m_expiry_ticks = now + buffer->getUInt16();
    m_reentry_cooldown_ticks = buffer->getUInt16();

    const unsigned int cooldown_count = buffer->getUInt8();
    m_kart_cooldowns.assign(cooldown_count, 0);
    for (unsigned int i = 0; i < cooldown_count; i++)
        m_kart_cooldowns[i] = now + buffer->getUInt16();

    alignBodyToEndpoints();
    if (m_node)
        m_node->setVisible(false);

#ifndef SERVER_ONLY
    createVisuals();
#endif
    updateLensingAnchor();
}   // restoreState

// ----------------------------------------------------------------------------
void Wormhole::getMinimapPositions(std::vector<Vec3>* positions) const
{
    if (!positions || !m_have_endpoints)
        return;

    positions->push_back(m_endpoint_transforms[0].getOrigin());
    positions->push_back(m_endpoint_transforms[1].getOrigin());
}   // getMinimapPositions
