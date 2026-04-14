//
//  Minkowski Kart - Relativistic VFX Manager
//  Implementation of visual effects for all relativistic powerups
//

#include "graphics/relativistic_vfx.hpp"
#include "graphics/blackboard_overlay.hpp"

#ifndef SERVER_ONLY
#include "graphics/irr_driver.hpp"
#include "graphics/particle_emitter.hpp"
#include "graphics/particle_kind.hpp"
#include "graphics/particle_kind_manager.hpp"
#include "graphics/stk_particle.hpp"
#include <IMeshSceneNode.h>
#include <IBillboardSceneNode.h>
#endif

#include "config/stk_config.hpp"
#include "karts/abstract_kart.hpp"
#include "modes/world.hpp"
#include "utils/constants.hpp"

#include <algorithm>
#include <cmath>

RelativisticVFXManager *relativistic_vfx_manager = nullptr;

static RelativisticVFXManager *g_instance = nullptr;

RelativisticVFXManager *RelativisticVFXManager::get()
{
    return g_instance;
}

void RelativisticVFXManager::create()
{
    g_instance = new RelativisticVFXManager();
    relativistic_vfx_manager = g_instance;
}

void RelativisticVFXManager::destroy()
{
    delete g_instance;
    g_instance = nullptr;
    relativistic_vfx_manager = nullptr;
}

RelativisticVFXManager::RelativisticVFXManager()
    : m_global_time(0)
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
    m_mass_spikes.resize(num_karts);
    m_tidal_arms.resize(num_karts);
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
    for (auto &ms : m_mass_spikes)
    {
        if (ms.downward_emitter) { delete ms.downward_emitter; ms.downward_emitter = nullptr; }
        if (ms.spark_emitter) { delete ms.spark_emitter; ms.spark_emitter = nullptr; }
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
    for (auto &ns : m_neutron_stars)
    {
        if (ns.core_emitter) { delete ns.core_emitter; ns.core_emitter = nullptr; }
        if (ns.halo_emitter) { delete ns.halo_emitter; ns.halo_emitter = nullptr; }
    }
    for (auto &cs : m_cosmic_strings)
    {
        if (cs.filament_emitter) { delete cs.filament_emitter; cs.filament_emitter = nullptr; }
    }
    if (m_frame_shift.grid_emitter) { delete m_frame_shift.grid_emitter; m_frame_shift.grid_emitter = nullptr; }
#endif

    for (BlackboardOverlay *bb : m_blackboards)
        delete bb;
    m_blackboards.clear();

    m_warp_bubbles.clear();
    m_time_dilations.clear();
    m_mass_spikes.clear();
    m_tidal_arms.clear();
    m_geodesic_missiles.clear();
    m_black_holes.clear();
    m_neutron_stars.clear();
    m_cosmic_strings.clear();
    m_frame_shift = FrameShiftVFX();
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
    AbstractKart *kart = World::getWorld()->getKart(kart_id);
    if (!wb.shimmer_emitter)
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
    vfx.ripple_phase += dt * 3.0f;
    if (vfx.ripple_phase > 2.0f * M_PI)
        vfx.ripple_phase -= 2.0f * (float)M_PI;

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
    td.redshift_intensity = 1.0f;
    td.smear_factor = 0.8f;
    td.drag_sound_pitch = 0.6f;

#ifndef SERVER_ONLY
    AbstractKart *kart = World::getWorld()->getKart(kart_id);
    if (!td.halo_emitter)
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
    if (vfx.redshift_intensity <= 0) return;

    // Pulsing redshift
    float pulse = 0.8f + 0.2f * sinf(m_global_time * 2.0f);
    vfx.redshift_intensity = pulse;

    // Motion smear based on speed
    float speed = kart->getSpeed();
    vfx.smear_factor = 0.5f + 0.5f * std::min(1.0f, speed / 30.0f);

#ifndef SERVER_ONLY
    if (vfx.halo_emitter)
        vfx.halo_emitter->setPosition(kart->getXYZ());
#endif
}

// ---------------------------------------------------------------------------
// Mass Spike
// ---------------------------------------------------------------------------
void RelativisticVFXManager::activateMassSpike(unsigned int kart_id)
{
    if (kart_id >= m_mass_spikes.size()) return;
    MassSpikeVFX &ms = m_mass_spikes[kart_id];
    ms.compression_factor = 1.0f;
    ms.ground_ripple = 1.0f;

#ifndef SERVER_ONLY
    AbstractKart *kart = World::getWorld()->getKart(kart_id);
    if (!ms.downward_emitter)
    {
        ParticleKindManager *pkm = ParticleKindManager::get();
        ParticleKind *particles = pkm->getParticles("mass_spike_gravity.xml");
        if (particles)
        {
            ms.downward_emitter = new ParticleEmitter(
                particles, kart->getXYZ(), kart->getNode());
        }
    }
    if (!ms.spark_emitter)
    {
        ParticleKindManager *pkm = ParticleKindManager::get();
        ParticleKind *particles = pkm->getParticles("mass_spike_sparks.xml");
        if (particles)
        {
            ms.spark_emitter = new ParticleEmitter(
                particles, kart->getXYZ(), kart->getNode());
        }
    }
#endif
}

void RelativisticVFXManager::deactivateMassSpike(unsigned int kart_id)
{
    if (kart_id >= m_mass_spikes.size()) return;
    MassSpikeVFX &ms = m_mass_spikes[kart_id];
    ms.compression_factor = 0;
    ms.ground_ripple = 0;
#ifndef SERVER_ONLY
    if (ms.downward_emitter) { delete ms.downward_emitter; ms.downward_emitter = nullptr; }
    if (ms.spark_emitter) { delete ms.spark_emitter; ms.spark_emitter = nullptr; }
#endif
}

void RelativisticVFXManager::updateMassSpike(MassSpikeVFX &vfx, float dt,
                                              AbstractKart *kart)
{
    if (vfx.compression_factor <= 0) return;

    // Ground ripple oscillation
    vfx.ground_ripple = 0.7f + 0.3f * sinf(m_global_time * 4.0f);

    // Sparks increase with speed
    float speed = kart->getSpeed();
    vfx.strain_sparks = std::min(1.0f, speed / 20.0f);

#ifndef SERVER_ONLY
    Vec3 pos = kart->getXYZ();
    if (vfx.downward_emitter) vfx.downward_emitter->setPosition(pos);
    if (vfx.spark_emitter) vfx.spark_emitter->setPosition(pos);
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
// Frame Shift
// ---------------------------------------------------------------------------
void RelativisticVFXManager::triggerFrameShift(const Vec3 &origin)
{
    m_frame_shift.origin = origin;
    m_frame_shift.wave_progress = 0;
    m_frame_shift.wave_radius = 0;
    m_frame_shift.chromatic_split = 1.0f;
}

void RelativisticVFXManager::updateFrameShift(float dt)
{
    if (m_frame_shift.wave_progress >= 1.0f) return;
    if (m_frame_shift.wave_progress < 0) return;

    m_frame_shift.wave_progress += dt * 0.5f;  // ~2 second sweep
    m_frame_shift.wave_radius = m_frame_shift.wave_progress * 200.0f;
    m_frame_shift.chromatic_split = std::max(0.0f,
        1.0f - m_frame_shift.wave_progress * 2.0f);

    if (m_frame_shift.wave_progress >= 1.0f)
    {
        m_frame_shift.wave_progress = -1;  // done
    }
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
    return m_time_dilations[kart_id].redshift_intensity > 0
        ? &m_time_dilations[kart_id] : nullptr;
}

const MassSpikeVFX *RelativisticVFXManager::getMassSpike(unsigned int kart_id) const
{
    if (kart_id >= m_mass_spikes.size()) return nullptr;
    return m_mass_spikes[kart_id].compression_factor > 0
        ? &m_mass_spikes[kart_id] : nullptr;
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

    for (unsigned int i = 0; i < m_mass_spikes.size() && i < world->getNumKarts(); i++)
        updateMassSpike(m_mass_spikes[i], dt, world->getKart(i));

    updateFrameShift(dt);

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
