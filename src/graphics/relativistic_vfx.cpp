//
//  Minkowski Kart - Relativistic VFX Manager
//  Implementation of visual effects for all relativistic powerups
//

#include "graphics/relativistic_vfx.hpp"
#include "graphics/blackboard_overlay.hpp"

#ifndef SERVER_ONLY
#include "graphics/central_settings.hpp"
#include "graphics/irr_driver.hpp"
#include "graphics/material_manager.hpp"
#include "graphics/particle_emitter.hpp"
#include "graphics/particle_kind.hpp"
#include "graphics/particle_kind_manager.hpp"
#include "graphics/sp/sp_base.hpp"
#include "graphics/sp/sp_dynamic_draw_call.hpp"
#include "graphics/sp/sp_shader_manager.hpp"
#include "graphics/stk_particle.hpp"
#include <IMeshSceneNode.h>
#include <IBillboardSceneNode.h>
#endif

#include "config/stk_config.hpp"
#include "guiengine/engine.hpp"
#include "karts/abstract_kart.hpp"
#include "modes/world.hpp"
#include "utils/constants.hpp"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr int kPairWaveSegments = 14;
    constexpr float kPairWaveCycles = 3.0f;
    constexpr float kPairWaveHalfWidth = 0.04f;
    constexpr float kPairWaveLength = 10.0f;
    constexpr float kPairForwardSpawnOffset = 1.4f;
    constexpr float kPairVerticalSpawnOffset = 0.45f;
    constexpr float kPhotonYawRange = (float)M_PI * 0.25f;

    // Big blue flash that pops at the spawn point on activation. Particle
    // based so it renders on both the GL (SP) and Vulkan (GE) backends -- the
    // photon wave above is SP-only and is invisible under Vulkan.
    const char *kPairFlashParticles = "antikarticle_flash.xml";
    constexpr float kPairFlashEmitTime = 0.08f;  // burst window, then cut off
}

static RelativisticVFXManager *g_instance = nullptr;

RelativisticVFXManager *RelativisticVFXManager::get()
{
    return g_instance;
}

void RelativisticVFXManager::create()
{
    g_instance = new RelativisticVFXManager();
}

void RelativisticVFXManager::destroy()
{
    delete g_instance;
    g_instance = nullptr;
}

RelativisticVFXManager::RelativisticVFXManager()
    : m_grav_wave_active(false)
    , m_grav_wave_origin(0.0f, 0.0f, 0.0f)
    , m_grav_wave_velocity(0.0f, 0.0f, 0.0f)
    , m_grav_wave_age(0.0f)
    , m_global_time(0)
{
}

RelativisticVFXManager::~RelativisticVFXManager()
{
    reset();
}

void RelativisticVFXManager::init(unsigned int num_karts)
{
    reset();
    m_warp_bubbles.resize(num_karts);
    m_time_dilations.resize(num_karts);
    m_tidal_arms.resize(num_karts);
    m_compactifications.resize(num_karts);
}

void RelativisticVFXManager::reset()
{
#ifndef SERVER_ONLY
    for (auto &wb : m_warp_bubbles)
    {
        if (wb.shimmer_emitter) { delete wb.shimmer_emitter; wb.shimmer_emitter = nullptr; }
        if (wb.sphere_node) { irr_driver->removeNode(static_cast<scene::ISceneNode*>(wb.sphere_node)); wb.sphere_node = nullptr; }
    }
    for (auto &td : m_time_dilations)
    {
        if (td.halo_emitter) { delete td.halo_emitter; td.halo_emitter = nullptr; }
    }
    for (auto &ta : m_tidal_arms)
    {
        if (ta.arc_emitter) { delete ta.arc_emitter; ta.arc_emitter = nullptr; }
    }
    for (auto &gm : m_geodesic_missiles)
    {
        if (gm.core_emitter) { delete gm.core_emitter; gm.core_emitter = nullptr; }
        if (gm.trail_emitter) { delete gm.trail_emitter; gm.trail_emitter = nullptr; }
    }
    for (auto &bh : m_black_holes)
    {
        if (bh.accretion_emitter) { delete bh.accretion_emitter; bh.accretion_emitter = nullptr; }
        if (bh.disk_node) { irr_driver->removeNode(static_cast<scene::ISceneNode*>(bh.disk_node)); bh.disk_node = nullptr; }
    }
    for (auto &ns : m_wormholes)
    {
        if (ns.core_emitter) { delete ns.core_emitter; ns.core_emitter = nullptr; }
        if (ns.halo_emitter) { delete ns.halo_emitter; ns.halo_emitter = nullptr; }
    }
    for (auto &cs : m_cosmic_strings)
    {
        if (cs.filament_emitter) { delete cs.filament_emitter; cs.filament_emitter = nullptr; }
    }
    for (auto &pp : m_pair_productions)
    {
        destroyPairProduction(pp);
    }
    if (m_super_position.grid_emitter) { delete m_super_position.grid_emitter; m_super_position.grid_emitter = nullptr; }
#endif

    for (BlackboardOverlay *bb : m_blackboards)
        delete bb;
    m_blackboards.clear();

    m_warp_bubbles.clear();
    m_time_dilations.clear();
    m_tidal_arms.clear();
    m_compactifications.clear();
    m_geodesic_missiles.clear();
    m_black_holes.clear();
    m_wormholes.clear();
    m_cosmic_strings.clear();
    m_pair_productions.clear();
    m_super_position = SuperPositionVFX();

    m_grav_wave_active = false;
    m_grav_wave_age = 0.0f;
    m_grav_wave_origin = Vec3(0.0f, 0.0f, 0.0f);
    m_grav_wave_velocity = Vec3(0.0f, 0.0f, 0.0f);
#ifndef SERVER_ONLY
    SP::clearGravWave();
#endif

    m_global_time = 0;
}

// ---------------------------------------------------------------------------
// Warp Bubble
// ---------------------------------------------------------------------------
void RelativisticVFXManager::activateWarpBubble(unsigned int kart_id)
{
    if (kart_id >= m_warp_bubbles.size()) return;
    WarpBubbleVFX &wb = m_warp_bubbles[kart_id];
    wb.active = true;
    wb.rim_intensity = 1.0f;
    wb.ripple_phase = 0;
    wb.collapse_timer = -1;

#ifndef SERVER_ONLY
    World *world = World::getWorld();
    AbstractKart *kart = world ? world->getKart(kart_id) : nullptr;
    if (kart && !wb.shimmer_emitter)
    {
        ParticleKindManager *pkm = ParticleKindManager::get();
        ParticleKind *particles = pkm->getParticles("warp_bubble_shimmer.xml");
        if (particles)
        {
            wb.shimmer_emitter = new ParticleEmitter(
                particles, kart->getXYZ(), kart->getNode());
        }
    }
#endif
}

void RelativisticVFXManager::deactivateWarpBubble(unsigned int kart_id)
{
    if (kart_id >= m_warp_bubbles.size()) return;
    WarpBubbleVFX &wb = m_warp_bubbles[kart_id];
    wb.active = false;
#ifndef SERVER_ONLY
    if (wb.shimmer_emitter)
    {
        delete wb.shimmer_emitter;
        wb.shimmer_emitter = nullptr;
    }
#endif
}

void RelativisticVFXManager::warpBubbleHit(unsigned int kart_id)
{
    if (kart_id >= m_warp_bubbles.size()) return;
    WarpBubbleVFX &wb = m_warp_bubbles[kart_id];
    wb.rim_intensity = 3.0f;  // bright flash
    wb.ripple_phase = 0;      // start ripple animation
    wb.collapse_timer = 0.5f; // start collapse
}

void RelativisticVFXManager::updateWarpBubble(WarpBubbleVFX &vfx, float dt,
                                               AbstractKart *kart)
{
    if (!vfx.active && vfx.collapse_timer < 0) return;

    // Einstein ring rim pulse
    vfx.rim_intensity = std::max(1.0f, vfx.rim_intensity - dt * 4.0f);

    // Concentric ripple animation
    vfx.ripple_phase = std::fmod(vfx.ripple_phase + dt * 3.0f,
                                 2.0f * (float)M_PI);

    // Collapse animation
    if (vfx.collapse_timer >= 0)
    {
        vfx.collapse_timer -= dt;
        if (vfx.collapse_timer < 0)
        {
            vfx.active = false;
            deactivateWarpBubble(kart->getWorldKartId());
        }
    }

#ifndef SERVER_ONLY
    if (vfx.shimmer_emitter)
        vfx.shimmer_emitter->setPosition(kart->getXYZ());
#endif
}

// ---------------------------------------------------------------------------
// Time Dilation
// ---------------------------------------------------------------------------
void RelativisticVFXManager::activateTimeDilation(unsigned int kart_id)
{
    if (kart_id >= m_time_dilations.size()) return;
    TimeDilationVFX &td = m_time_dilations[kart_id];
    td.active = true;
    td.redshift_intensity = 0.0f;
    td.smear_factor = 0.8f;
    td.drag_sound_pitch = 0.6f;

#ifndef SERVER_ONLY
    World *world = World::getWorld();
    AbstractKart *kart = world ? world->getKart(kart_id) : nullptr;
    if (kart && !td.halo_emitter)
    {
        ParticleKindManager *pkm = ParticleKindManager::get();
        ParticleKind *particles = pkm->getParticles("time_dilation_halo.xml");
        if (particles)
        {
            td.halo_emitter = new ParticleEmitter(
                particles, kart->getXYZ(), kart->getNode());
        }
    }
#endif
}

void RelativisticVFXManager::deactivateTimeDilation(unsigned int kart_id)
{
    if (kart_id >= m_time_dilations.size()) return;
    TimeDilationVFX &td = m_time_dilations[kart_id];
    td.active = false;
    td.redshift_intensity = 0;
    td.smear_factor = 0;
    td.drag_sound_pitch = 1.0f;
#ifndef SERVER_ONLY
    if (td.halo_emitter) { delete td.halo_emitter; td.halo_emitter = nullptr; }
#endif
}

void RelativisticVFXManager::updateTimeDilation(TimeDilationVFX &vfx, float dt,
                                                 AbstractKart *kart)
{
    if (!vfx.active) return;

    vfx.redshift_intensity = 0.0f;

    // Motion smear based on speed
    float speed = kart->getSpeed();
    vfx.smear_factor = 0.5f + 0.5f * std::min(1.0f, speed / 30.0f);

#ifndef SERVER_ONLY
    if (vfx.halo_emitter)
        vfx.halo_emitter->setPosition(kart->getXYZ());
#endif
}

// ---------------------------------------------------------------------------
// Tidal Arm
// ---------------------------------------------------------------------------
void RelativisticVFXManager::activateTidalArm(unsigned int kart_id)
{
    if (kart_id >= m_tidal_arms.size()) return;
    TidalArmVFX &ta = m_tidal_arms[kart_id];
    ta.arc_progress = 0;
    ta.distortion_width = 0.5f;
    ta.spaghettification = 0;
}

void RelativisticVFXManager::deactivateTidalArm(unsigned int kart_id)
{
    if (kart_id >= m_tidal_arms.size()) return;
    TidalArmVFX &ta = m_tidal_arms[kart_id];
    ta.arc_progress = 0;
#ifndef SERVER_ONLY
    if (ta.arc_emitter) { delete ta.arc_emitter; ta.arc_emitter = nullptr; }
#endif
}

// ---------------------------------------------------------------------------
// Super Position
// ---------------------------------------------------------------------------
void RelativisticVFXManager::triggerSuperPosition(const Vec3 &origin)
{
    m_super_position.origin = origin;
    m_super_position.wave_progress = 0;
    m_super_position.wave_radius = 0;
    m_super_position.chromatic_split = 1.0f;
}

// ---------------------------------------------------------------------------
// Time-dilation gravitational wave
// ---------------------------------------------------------------------------
void RelativisticVFXManager::triggerGravitationalWave(const Vec3 &origin,
                                                      const Vec3 &velocity)
{
    m_grav_wave_active   = true;
    m_grav_wave_origin   = origin;
    m_grav_wave_velocity = velocity;
    m_grav_wave_age      = 0.0f;
}

void RelativisticVFXManager::updateSuperPosition(float dt)
{
    if (m_super_position.wave_progress >= 1.0f) return;
    if (m_super_position.wave_progress < 0) return;

    m_super_position.wave_progress += dt * 0.5f;  // ~2 second sweep
    m_super_position.wave_radius = m_super_position.wave_progress * 200.0f;
    m_super_position.chromatic_split = std::max(0.0f,
        1.0f - m_super_position.wave_progress * 2.0f);

    if (m_super_position.wave_progress >= 1.0f)
    {
        m_super_position.wave_progress = -1;  // done
    }
}

// ---------------------------------------------------------------------------
// Pair Production
// ---------------------------------------------------------------------------
void RelativisticVFXManager::destroyPairProduction(PairProductionVFX &vfx)
{
#ifndef SERVER_ONLY
    if (vfx.wave_draw_call)
    {
        vfx.wave_draw_call->removeFromSP();
        vfx.wave_draw_call = nullptr;
    }
    if (vfx.flash_emitter)
    {
        delete vfx.flash_emitter;
        vfx.flash_emitter = nullptr;
    }
#endif
}

void RelativisticVFXManager::triggerPairProduction(const Vec3 &origin,
                                                   const Vec3 &forward,
                                                   const Vec3 &normal,
                                                   uint32_t seed)
{
    Vec3 up = normal;
    if (up.length2() < 0.0001f)
        up = Vec3(0.0f, 1.0f, 0.0f);
    up.normalize();

    Vec3 f = forward - up * forward.dot(up);
    if (f.length2() < 0.0001f)
        f = Vec3(0.0f, 0.0f, 1.0f) - up * up.getZ();
    if (f.length2() < 0.0001f)
        f = Vec3(1.0f, 0.0f, 0.0f);
    f.normalize();

    Vec3 side = f.cross(up);
    if (side.length2() < 0.0001f)
        side = Vec3(1.0f, 0.0f, 0.0f);
    side.normalize();

    const float random_fraction = (float)(seed & 0xffffu) / 65535.0f;
    const float yaw = (random_fraction * 2.0f - 1.0f) * kPhotonYawRange;
    Vec3 photon_direction = f * std::cos(yaw) + side * std::sin(yaw);
    photon_direction.normalize();

    PairProductionVFX vfx;
    vfx.origin = origin + f * kPairForwardSpawnOffset
        + up * kPairVerticalSpawnOffset;
    vfx.axis = photon_direction;
    vfx.normal = up;
    vfx.age = 0.0f;
    vfx.wave_time = 0.0f;

#ifndef SERVER_ONLY
    if (!GUIEngine::isNoGraphics() && CVS->isGLSL())
    {
        const video::SColor color(255, 90, 175, 255);
        vfx.wave_draw_call =
            std::make_shared<SP::SPDynamicDrawCall>(
                scene::EPT_TRIANGLE_STRIP,
                SP::SPShaderManager::get()->getSPShader("additive_dynamic"),
                material_manager->getDefaultSPMaterial("additive"));
        vfx.wave_draw_call->getVerticesVector().resize(
            (kPairWaveSegments + 1) * 2);
        for (auto &vertex : vfx.wave_draw_call->getVerticesVector())
            vertex.m_color = color;
        SP::addDynamicDrawCall(vfx.wave_draw_call);
    }

    // Big blue flash: a short particle burst. Unlike the SP wave above this
    // uses the particle path (STKParticle), which renders on GL and Vulkan.
    if (!GUIEngine::isNoGraphics() && ParticleKindManager::get())
    {
        ParticleKind *flash =
            ParticleKindManager::get()->getParticles(kPairFlashParticles);
        if (flash)
            vfx.flash_emitter = new ParticleEmitter(flash, vfx.origin, NULL);
    }
#endif

    m_pair_productions.push_back(vfx);
}

void RelativisticVFXManager::updatePairProduction(PairProductionVFX &vfx,
                                                  float dt)
{
    vfx.age += dt;
    vfx.wave_time += dt;

#ifndef SERVER_ONLY
    const float progress = std::min(1.0f, vfx.age / vfx.lifetime);
    const float fade = std::max(0.0f, 1.0f - progress);
    const float length = kPairWaveLength * (0.25f + 0.75f * progress);
    const Vec3 axis = vfx.axis;
    Vec3 wave_axis = vfx.normal.cross(axis);
    if (wave_axis.length2() < 0.0001f)
        wave_axis = Vec3(0.0f, 1.0f, 0.0f).cross(axis);
    if (wave_axis.length2() < 0.0001f)
        wave_axis = Vec3(1.0f, 0.0f, 0.0f);
    wave_axis.normalize();
    Vec3 wave_width = axis.cross(wave_axis);
    if (wave_width.length2() < 0.0001f)
        wave_width = vfx.normal;
    wave_width.normalize();

    const float amplitude = 0.55f * fade + 0.08f;
    const float phase_per_unit = 2.0f * (float)M_PI /
        std::max(0.5f, length / kPairWaveCycles);
    const float phase_shift = vfx.wave_time * 24.0f * phase_per_unit;

    if (vfx.wave_draw_call)
    {
        // SP packed normal: Y-up (0,1,0) encoded as 0x1FF in the Y channel
        // of a 10-10-10-2 SNORM format used by STK's SP vertex layout.
        constexpr uint32_t kNormalYUp = 0x1FF << 10;

        const Vec3 end = vfx.origin + axis * length;
        auto &vertices = vfx.wave_draw_call->getVerticesVector();
        for (int i = 0; i <= kPairWaveSegments; i++)
        {
            const float t = (float)i / (float)kPairWaveSegments;
            const float phase = t * length * phase_per_unit + phase_shift;
            const Vec3 center = vfx.origin + (end - vfx.origin) * t
                + wave_axis * (std::sin(phase) * amplitude);
            const Vec3 photon_offset = wave_width * kPairWaveHalfWidth;
            vertices[i * 2].m_position =
                Vec3(center - photon_offset).toIrrVector();
            vertices[i * 2 + 1].m_position =
                Vec3(center + photon_offset).toIrrVector();
            vertices[i * 2].m_normal     = kNormalYUp;
            vertices[i * 2 + 1].m_normal = kNormalYUp;
            vertices[i * 2].m_color.setAlpha((uint32_t)(255.0f * fade));
            vertices[i * 2 + 1].m_color.setAlpha((uint32_t)(255.0f * fade));
        }
        vfx.wave_draw_call->setUpdateOffset(0);
        vfx.wave_draw_call->recalculateBoundingBox();
    }

    // One-shot flash: cut emission after the initial burst so the particles
    // pop once and fade, rather than streaming for the whole effect.
    if (vfx.flash_emitter && !vfx.flash_stopped &&
        vfx.age >= kPairFlashEmitTime)
    {
        vfx.flash_emitter->setCreationRateAbsolute(0.0f);
        vfx.flash_stopped = true;
    }
#endif
}

// ---------------------------------------------------------------------------
// Compactification
// ---------------------------------------------------------------------------
void RelativisticVFXManager::activateCompactification(unsigned int kart_id)
{
    if (kart_id >= m_compactifications.size()) return;
    m_compactifications[kart_id].active = true;
}

void RelativisticVFXManager::deactivateCompactification(unsigned int kart_id)
{
    if (kart_id >= m_compactifications.size()) return;
    m_compactifications[kart_id].active = false;
}

// ---------------------------------------------------------------------------
// Query functions
// ---------------------------------------------------------------------------
const WarpBubbleVFX *RelativisticVFXManager::getWarpBubble(unsigned int kart_id) const
{
    if (kart_id >= m_warp_bubbles.size()) return nullptr;
    return m_warp_bubbles[kart_id].active ? &m_warp_bubbles[kart_id] : nullptr;
}

const TimeDilationVFX *RelativisticVFXManager::getTimeDilation(unsigned int kart_id) const
{
    if (kart_id >= m_time_dilations.size()) return nullptr;
    return m_time_dilations[kart_id].active
        ? &m_time_dilations[kart_id] : nullptr;
}

const CompactificationVFX *RelativisticVFXManager::getCompactification(unsigned int kart_id) const
{
    if (kart_id >= m_compactifications.size()) return nullptr;
    const CompactificationVFX &cvfx = m_compactifications[kart_id];
    return (cvfx.active || cvfx.strength > 0.0f) ? &cvfx : nullptr;
}

// ---------------------------------------------------------------------------
// Main update
// ---------------------------------------------------------------------------
void RelativisticVFXManager::update(float dt)
{
    m_global_time += dt;

    World *world = World::getWorld();
    if (!world) return;

    for (unsigned int i = 0; i < m_warp_bubbles.size() && i < world->getNumKarts(); i++)
        updateWarpBubble(m_warp_bubbles[i], dt, world->getKart(i));

    for (unsigned int i = 0; i < m_time_dilations.size() && i < world->getNumKarts(); i++)
        updateTimeDilation(m_time_dilations[i], dt, world->getKart(i));

    // Ramp compactification strength up/down smoothly (0.4 s full transition)
    const float ramp = dt / 0.4f;
    for (auto &cvfx : m_compactifications)
    {
        if (cvfx.active)
            cvfx.strength = std::min(1.0f, cvfx.strength + ramp);
        else
            cvfx.strength = std::max(0.0f, cvfx.strength - ramp);
    }

    updateSuperPosition(dt);

    for (auto it = m_pair_productions.begin(); it != m_pair_productions.end(); )
    {
        updatePairProduction(*it, dt);
        if (it->age >= it->lifetime)
        {
            destroyPairProduction(*it);
            it = m_pair_productions.erase(it);
        }
        else
            ++it;
    }

    // Update active blackboard overlays, remove finished ones
    for (auto it = m_blackboards.begin(); it != m_blackboards.end(); )
    {
        (*it)->update(dt);
        if ((*it)->isFinished())
        {
            delete *it;
            it = m_blackboards.erase(it);
        }
        else
            ++it;
    }
}

void RelativisticVFXManager::updateGraphics(float dt)
{
    // Graphics-only updates (particle positions already handled in update)
#ifndef SERVER_ONLY
    // Animate the time-dilation gravitational wave and publish its current
    // origin + radius to the camera UBO so displace_color.frag (GE/Vulkan) can
    // draw the expanding screen-space ring. A short fade tail past RADIUS lets
    // the ring dissolve instead of popping out at exactly 50 m.
    if (m_grav_wave_active)
    {
        m_grav_wave_age += dt;
        const float radius = TimeDilationWave::SPEED * m_grav_wave_age;
        const float fade_tail = 0.25f; // extra seconds after reaching the edge
        if (m_grav_wave_age >= TimeDilationWave::TRAVEL_TIME + fade_tail)
        {
            m_grav_wave_active = false;
            SP::clearGravWave();
        }
        else
        {
            // Drift the ripple centre at the emission velocity so the wave is
            // co-moving with the shooter's frame (relativistic feel).
            const core::vector3df center(
                m_grav_wave_origin.getX()
                    + m_grav_wave_velocity.getX() * m_grav_wave_age,
                m_grav_wave_origin.getY()
                    + m_grav_wave_velocity.getY() * m_grav_wave_age,
                m_grav_wave_origin.getZ()
                    + m_grav_wave_velocity.getZ() * m_grav_wave_age);
            SP::setGravWave(center, radius);
        }
    }
#endif
}

// ---------------------------------------------------------------------------
// Blackboard overlay (Cosmic String backward-fire gag)
// ---------------------------------------------------------------------------
void RelativisticVFXManager::triggerBlackboard(unsigned int kart_id,
                                               float duration_seconds)
{
    // Remove any existing blackboard for this kart
    for (auto it = m_blackboards.begin(); it != m_blackboards.end(); ++it)
    {
        if ((*it)->getOwnerKartId() == (int)kart_id)
        {
            delete *it;
            it = m_blackboards.erase(it);
            break;
        }
    }
    m_blackboards.push_back(new BlackboardOverlay((int)kart_id, duration_seconds));
}

void RelativisticVFXManager::renderBlackboards()
{
#ifndef SERVER_ONLY
    for (auto &bb : m_blackboards)
        bb->render();
#endif
}
