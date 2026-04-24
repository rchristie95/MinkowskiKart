//
//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2006-2015 Joerg Henrichs
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

#include "items/attachment.hpp"

#include "graphics/relativistic_vfx.hpp"
#include <algorithm>
#include "achievements/achievements_status.hpp"
#include "audio/sfx_base.hpp"
#include "audio/sfx_manager.hpp"
#include "config/player_manager.hpp"
#include "config/stk_config.hpp"
#include "config/user_config.hpp"
#include "graphics/explosion.hpp"
#include "graphics/hit_effect.hpp"
#include "graphics/irr_driver.hpp"
#include <ge_render_info.hpp>
#include "guiengine/engine.hpp"
#include "items/attachment_manager.hpp"
#include "items/item_manager.hpp"
#include "items/powerup_manager.hpp"
#include "items/projectile_manager.hpp"
#include "items/swatter.hpp"
#include "karts/abstract_kart.hpp"
#include "karts/controller/controller.hpp"
#include "karts/explosion_animation.hpp"
#include "karts/kart_properties.hpp"
#include "modes/capture_the_flag.hpp"
#include "modes/world.hpp"
#include "network/network_string.hpp"
#include "network/rewind_manager.hpp"
#include "physics/triangle_mesh.hpp"
#include "race/race_manager.hpp"
#include "tracks/track.hpp"
#include "utils/constants.hpp"

#include "irrMath.h"
#include <IAnimatedMeshSceneNode.h>
#include <ISceneNode.h>
#include <btBulletDynamicsCommon.h>

#include <cstdint>
#include <random>

namespace
{
    const float MAXWELL_BOLTZMANN_DURATION_SECONDS = 10.0f;
    const float MAXWELL_BOLTZMANN_KICK_PERIOD      = 1.0f;
    const float MAXWELL_BOLTZMANN_SIGMA            = 10.0f;
    const float BROWNIAN_SPHERE_START_DISTANCE     = 1.0f;

    uint64_t mixSeed(uint64_t value)
    {
        value ^= value >> 33;
        value *= 0xff51afd7ed558ccdULL;
        value ^= value >> 33;
        value *= 0xc4ceb9fe1a85ec53ULL;
        value ^= value >> 33;
        return value;
    }   // mixSeed

    class BrownianKickEffect : public HitEffect
    {
    private:
        Vec3              m_center;
        Vec3              m_direction;
        float             m_bounce_distance;
        float             m_out_distance;
        float             m_speed;
        float             m_age;
        float             m_duration;
        bool              m_impact_played;
        float             m_impact_age;
        const char       *m_impact_sound;
        SFXBase          *m_sfx;
        scene::ISceneNode *m_node;
        scene::ISceneNode *m_ring_node;

    public:
        BrownianKickEffect(const Vec3& center, const Vec3& direction,
                           float kart_width, float magnitude)
            : m_center(center), m_direction(direction), m_age(0.0f),
              m_impact_played(false), m_impact_age(0.0f),
              m_impact_sound("ball_bounce"), m_sfx(NULL), m_node(NULL),
              m_ring_node(NULL)
        {
            if (m_direction.length2() <= btScalar(1.0e-8f))
                m_direction = Vec3(1.0f, 0.0f, 0.0f);
            m_direction.normalize();
            m_bounce_distance = std::max(0.20f, kart_width * 0.45f);
            m_out_distance = BROWNIAN_SPHERE_START_DISTANCE;
            m_speed = core::clamp(magnitude * 0.45f, 4.0f, 12.0f);
            const float travel = std::max(0.05f,
                (m_out_distance - m_bounce_distance) * 2.0f);
            m_duration = std::max(0.25f, travel / m_speed);
            if (magnitude >= 22.0f)
                m_impact_sound = "metal_clang";
            else if (magnitude >= 10.0f)
                m_impact_sound = "boing";

#ifndef SERVER_ONLY
            if (!GUIEngine::isNoGraphics() && irr_driver)
            {
                const float t = core::clamp(magnitude / 30.0f, 0.0f, 1.0f);
                const u32 red = (u32)(255.0f * t);
                const u32 blue = (u32)(255.0f * (1.0f - t));
                const u32 green = (u32)(80.0f * (1.0f - fabsf(0.5f - t) * 2.0f));
                video::SColor colour(230, red, green, blue);
                m_node = irr_driver->addSphere(0.16f, colour);
                if (m_node)
                {
                    m_node->setPosition((m_center - m_direction *
                        m_out_distance).toIrrVector());
                }
            }
#endif
        }

        ~BrownianKickEffect()
        {
#ifndef SERVER_ONLY
            if (m_node && irr_driver)
                irr_driver->removeNode(m_node);
            if (m_ring_node && irr_driver)
                irr_driver->removeNode(m_ring_node);
            if (m_sfx)
                m_sfx->deleteSFX();
#endif
        }

        bool updateAndDelete(int ticks) OVERRIDE
        {
            m_age += stk_config->ticks2Time(ticks);
            const float phase = m_duration > 0.0f
                ? core::clamp(m_age / m_duration, 0.0f, 1.0f)
                : 1.0f;
            const float segment = phase < 0.5f ? phase * 2.0f
                                                : (phase - 0.5f) * 2.0f;
            const float distance = phase < 0.5f
                ? m_out_distance + (m_bounce_distance - m_out_distance) * segment
                : m_bounce_distance + (m_out_distance - m_bounce_distance) * segment;

#ifndef SERVER_ONLY
            if (!m_impact_played && phase >= 0.5f)
            {
                m_impact_played = true;
                m_impact_age = 0.0f;
                if (irr_driver)
                {
                    m_ring_node = irr_driver->addSphere(
                        0.24f, video::SColor(95, 230, 245, 255));
                    if (m_ring_node)
                        m_ring_node->setPosition((m_center - m_direction *
                            m_bounce_distance).toIrrVector());
                }
                if (SFXManager::get())
                {
                    m_sfx = SFXManager::get()->createSoundSource(m_impact_sound);
                    if (m_sfx)
                    {
                        const float vol =
                            RaceManager::get()->getNumLocalPlayers() > 1
                                ? 0.45f : 0.9f;
                        m_sfx->setVolume(vol);
                        m_sfx->play(m_center - m_direction * m_bounce_distance);
                    }
                }
            }
            if (m_node)
                m_node->setPosition((m_center - m_direction * distance)
                    .toIrrVector());
            if (m_ring_node)
            {
                m_impact_age += stk_config->ticks2Time(ticks);
                const float ring_t = core::clamp(m_impact_age / 0.20f,
                                                 0.0f, 1.0f);
                const float ring_scale = 1.0f + ring_t * 2.5f;
                m_ring_node->setScale(core::vector3df(ring_scale, ring_scale,
                                                      ring_scale));
                if (ring_t >= 1.0f && irr_driver)
                {
                    irr_driver->removeNode(m_ring_node);
                    m_ring_node = NULL;
                }
            }
            if (m_age >= m_duration && m_node && irr_driver)
            {
                irr_driver->removeNode(m_node);
                m_node = NULL;
            }
#endif
            return m_age >= m_duration &&
                (!m_sfx || m_sfx->getStatus() != SFXBase::SFX_PLAYING);
        }
    };
}

/** Initialises the attachment each kart has.
 */
Attachment::Attachment(AbstractKart* kart)
{
    m_type                 = ATTACH_NOTHING;
    m_ticks_left           = 0;
    m_plugin               = NULL;
    m_kart                 = kart;
    m_previous_owner       = NULL;
    m_bomb_sound           = NULL;
    m_bubble_explode_sound = NULL;
    m_initial_speed        = 0;
    m_graphical_type       = ATTACH_NOTHING;
    m_scaling_end_ticks    = -1;
    m_maxwell_ticks_to_next_kick = 0;
    m_maxwell_kick_index = 0;
    m_maxwell_kick_flash_ticks = 0;
    m_maxwell_last_kick_delta_v = Vec3(0.0f, 0.0f, 0.0f);
    m_node = NULL;
    if (GUIEngine::isNoGraphics())
        return;
    // If we attach a NULL mesh, we get a NULL scene node back. So we
    // have to attach some kind of mesh, but make it invisible.
    if (kart->isGhostKart())
        m_node = irr_driver->addAnimatedMesh(
            attachment_manager->getMesh(Attachment::ATTACH_BOMB), "bomb",
            NULL, std::make_shared<GE::GERenderInfo>(0.0f, true));
    else
        m_node = irr_driver->addAnimatedMesh(
            attachment_manager->getMesh(Attachment::ATTACH_BOMB), "bomb");
#ifdef DEBUG
    std::string debug_name = kart->getIdent()+" (attachment)";
    m_node->setName(debug_name.c_str());
#endif
    m_node->setParent(m_kart->getNode());
    m_node->setVisible(false);
}   // Attachment

//-----------------------------------------------------------------------------
/** Removes the attachment object. It removes the scene node used to display
 *  the attachment, and stops any sfx from being played.
 */
Attachment::~Attachment()
{
    clear();
    if(m_node)
        irr_driver->removeNode(m_node);

    if (m_bomb_sound)
    {
        m_bomb_sound->deleteSFX();
        m_bomb_sound = NULL;
    }

    if (m_bubble_explode_sound)
    {
        m_bubble_explode_sound->deleteSFX();
        m_bubble_explode_sound = NULL;
    }
}   // ~Attachment

//-----------------------------------------------------------------------------
float Attachment::getMaxwellBoltzmannDurationSeconds()
{
    return MAXWELL_BOLTZMANN_DURATION_SECONDS;
}   // getMaxwellBoltzmannDurationSeconds

//-----------------------------------------------------------------------------
bool Attachment::applySwatterStyleSquash(AbstractKart* attacker,
                                         AbstractKart* victim,
                                         bool award_swatter_achievements)
{
    if (!victim)
        return false;

    const KartProperties *kp = victim->getKartProperties();
    const bool success = victim->setSquash(kp->getSwatterSquashDuration(),
                                           kp->getSwatterSquashSlowdown());
    const bool has_created_explosion_animation =
        success && victim->getKartAnimation() != NULL;

    if (success)
    {
        World::getWorld()->kartHit(victim->getWorldKartId(),
            attacker ? attacker->getWorldKartId() : -1);

        CaptureTheFlag* ctf = dynamic_cast<CaptureTheFlag*>(World::getWorld());
        if (ctf)
        {
            const int reset_ticks = (ctf->getTicksSinceStart() / 10) * 10 + 80;
            ctf->resetKartForSwatterHit(victim->getWorldKartId(), reset_ticks);
        }

        if (attacker && award_swatter_achievements &&
            attacker->getController()->canGetAchievements())
        {
            PlayerManager::addKartHit(victim->getWorldKartId());
            PlayerManager::increaseAchievement(
                AchievementsStatus::SWATTER_HIT, 1);
            PlayerManager::increaseAchievement(
                AchievementsStatus::ALL_HITS, 1);
            if (RaceManager::get()->isLinearRaceMode())
            {
                PlayerManager::increaseAchievement(
                    AchievementsStatus::SWATTER_HIT_1RACE, 1);
                PlayerManager::increaseAchievement(
                    AchievementsStatus::ALL_HITS_1RACE, 1);
            }
        }
    }

    if (!GUIEngine::isNoGraphics() && has_created_explosion_animation &&
        !RewindManager::get()->isRewinding())
    {
        const Vec3& hit_origin = attacker ? attacker->getXYZ() : victim->getXYZ();
        HitEffect *he = new Explosion(hit_origin, "explosion", "explosion.xml");
        if ((attacker && attacker->getController()->isLocalPlayerController()) ||
            (!attacker && victim->getController()->isLocalPlayerController()))
        {
            he->setLocalPlayerKartHit();
        }
        ProjectileManager::get()->addHitEffect(he);
    }

    return success;
}   // applySwatterStyleSquash

//-----------------------------------------------------------------------------
void Attachment::resetMaxwellBoltzmannState(int ticks_left)
{
    const int duration_ticks =
        stk_config->time2Ticks(MAXWELL_BOLTZMANN_DURATION_SECONDS);
    const int cadence_ticks =
        stk_config->time2Ticks(MAXWELL_BOLTZMANN_KICK_PERIOD);
    const int elapsed_ticks = std::max(0, duration_ticks - ticks_left);
    m_maxwell_kick_index = elapsed_ticks / cadence_ticks;
    m_maxwell_ticks_to_next_kick = cadence_ticks -
        (elapsed_ticks % cadence_ticks);
    if (m_maxwell_ticks_to_next_kick <= 0)
        m_maxwell_ticks_to_next_kick = cadence_ticks;
    m_maxwell_kick_flash_ticks = 0;
    m_maxwell_last_kick_delta_v = Vec3(0.0f, 0.0f, 0.0f);
}   // resetMaxwellBoltzmannState

//-----------------------------------------------------------------------------
void Attachment::applyMaxwellBoltzmannKick()
{
    if (!m_kart || !m_kart->getBody() || m_kart->isEliminated() ||
        m_kart->getKartAnimation() != NULL)
    {
        return;
    }

    btVector3 normal = m_kart->getNormal();
    if (normal.length2() <= btScalar(1.0e-8f))
        normal = m_kart->getBody()->getWorldTransform().getBasis().getColumn(1);
    normal.normalize();

    btVector3 tangent_a =
        m_kart->getBody()->getWorldTransform().getBasis().getColumn(2);
    tangent_a -= normal * tangent_a.dot(normal);
    if (tangent_a.length2() <= btScalar(1.0e-8f))
    {
        tangent_a = normal.cross(btVector3(1.0f, 0.0f, 0.0f));
        if (tangent_a.length2() <= btScalar(1.0e-8f))
            tangent_a = normal.cross(btVector3(0.0f, 0.0f, 1.0f));
    }
    tangent_a.normalize();
    btVector3 tangent_b = normal.cross(tangent_a);
    tangent_b.normalize();
    btRigidBody* body = m_kart->getBody();

    uint64_t seed = powerup_manager ? powerup_manager->getRandomSeed() : 0;
    seed ^= uint64_t(m_kart->getWorldKartId() + 1) * 0x9e3779b97f4a7c15ULL;
    seed ^= uint64_t(m_maxwell_kick_index + 1) * 0xbf58476d1ce4e5b9ULL;
    std::mt19937 rng((uint32_t)(mixSeed(seed) & 0xffffffffULL));
    std::normal_distribution<float> normal_dist(
        0.0f, MAXWELL_BOLTZMANN_SIGMA);

    const btVector3 current_velocity =
        body->getLinearVelocity() - normal * body->getLinearVelocity().dot(normal);
    btVector3 travel_dir = current_velocity;
    if (travel_dir.length2() <= btScalar(1.0e-6f))
        travel_dir = tangent_a;
    travel_dir.normalize();

    const float kick_a = normal_dist(rng) * 0.45f;
    const float kick_b = normal_dist(rng) * 0.45f;
    const float drag_kick = fabsf(normal_dist(rng));
    const btVector3 delta_v = tangent_a * kick_a + tangent_b * kick_b -
                              travel_dir * drag_kick;
    m_maxwell_last_kick_delta_v = Vec3(delta_v);
    m_maxwell_kick_flash_ticks = stk_config->time2Ticks(0.45f);
    const btVector3 velocity = body->getLinearVelocity() + delta_v;
    body->setLinearVelocity(velocity);
    body->setInterpolationLinearVelocity(velocity);
    body->activate();

    if (!GUIEngine::isNoGraphics() && !RewindManager::get()->isRewinding())
    {
        Vec3 kick_dir(delta_v);
        const float magnitude = kick_dir.length();
        if (magnitude > 1.0e-5f)
        {
            kick_dir /= magnitude;
            Vec3 center = m_kart->getXYZ() + Vec3(normal) *
                std::max(0.25f, m_kart->getHighestPoint() * 0.45f);
            ProjectileManager::get()->addHitEffect(
                new BrownianKickEffect(center, kick_dir,
                                       m_kart->getKartWidth(), magnitude));
        }
    }
}   // applyMaxwellBoltzmannKick

//-----------------------------------------------------------------------------
void Attachment::updateMaxwellBoltzmann(int ticks)
{
    const int cadence_ticks =
        stk_config->time2Ticks(MAXWELL_BOLTZMANN_KICK_PERIOD);
    if (m_maxwell_kick_flash_ticks > 0)
        m_maxwell_kick_flash_ticks = std::max(0,
            m_maxwell_kick_flash_ticks - ticks);
    m_maxwell_ticks_to_next_kick -= ticks;
    while (m_maxwell_ticks_to_next_kick <= 0)
    {
        applyMaxwellBoltzmannKick();
        m_maxwell_kick_index++;
        m_maxwell_ticks_to_next_kick += cadence_ticks;
    }
}   // updateMaxwellBoltzmann

//-----------------------------------------------------------------------------
/** Sets the attachment a kart has. This will also handle animation to be
 *  played, e.g. when a swatter replaces a bomb.
 *  \param type The type of the new attachment.
 *  \param time How long this attachment should stay with the kart.
 *  \param current_kart The kart from which an attachment is transferred.
 *         This is currently used for the bomb (to avoid that a bomb
 *         can be passed back to the previous owner). NULL if a no
 *         previous owner exists.
 */
void Attachment::set(AttachmentType type, int ticks,
                     AbstractKart *current_kart,
                     bool set_by_rewind_parachute)
{
    bool was_bomb = m_type == ATTACH_BOMB;
    int16_t prev_ticks = m_ticks_left;
    clear();

    // If necessary create the appropriate plugin which encapsulates
    // the associated behavior
    switch(type)
    {
    case ATTACH_TIDAL_ARM:
        m_plugin =
            new Swatter(m_kart, was_bomb ? prev_ticks : -1, ticks, this);
        break;
    default:
        break;
    }   // switch(type)

    if (type == ATTACH_MASS_SPIKE)
        ticks = stk_config->time2Ticks(getMaxwellBoltzmannDurationSeconds());

    m_type             = type;
    m_ticks_left       = ticks;
    m_previous_owner   = current_kart;
    m_scaling_end_ticks = World::getWorld()->getTicksSinceStart() +
        stk_config->time2Ticks(0.7f);

    resetMaxwellBoltzmannState(type == ATTACH_MASS_SPIKE ? m_ticks_left : 0);

    // Activate relativistic VFX for new attachment
    if (relativistic_vfx_manager)
    {
        unsigned int kid = m_kart->getWorldKartId();
        switch (type)
        {
        case ATTACH_WARP_BUBBLE:
        case ATTACH_NOLOK_WARP_BUBBLE:
            relativistic_vfx_manager->activateWarpBubble(kid);
            break;
        case ATTACH_TIME_DILATION:
            relativistic_vfx_manager->activateTimeDilation(kid);
            break;
        case ATTACH_TIDAL_ARM:
            relativistic_vfx_manager->activateTidalArm(kid);
            break;
        case ATTACH_COMPACTIFICATION:
            relativistic_vfx_manager->activateCompactification(kid);
            break;
        default: break;
        }
    }

    m_initial_speed = 0;
    // A parachute can be attached as result of the usage of an item. In this
    // case we have to save the current kart speed so that it can be detached
    // by slowing down.
    // if set by rewind the parachute ticks is already correct
    if (m_type == ATTACH_TIME_DILATION && !set_by_rewind_parachute)
    {
        const KartProperties *kp = m_kart->getKartProperties();
        float speed_mult;

        float initial_speed = m_kart->getSpeed();
        // if going very slowly or backwards, braking won't remove parachute
        if(initial_speed <= 1.5f) initial_speed = 1.5f;

        float f = initial_speed / kp->getParachuteMaxSpeed();
        float temp_mult = kp->getParachuteDurationSpeedMult();

        // duration can't be reduced by higher speed
        if (temp_mult < 1.0f) temp_mult = 1.0f;

        if (f > 1.0f) f = 1.0f;   // cap fraction

        speed_mult = 1.0f + (f *  (temp_mult - 1.0f));

        m_ticks_left = int(m_ticks_left * speed_mult);
        int initial_speed_round = (int)(initial_speed * 100.0f);
        initial_speed_round =
            irr::core::clamp(initial_speed_round, -32768, 32767);
        m_initial_speed = (int16_t)initial_speed_round;
    }
}   // set

// -----------------------------------------------------------------------------
/** Removes any attachement currently on the kart. */
void Attachment::clear()
{
    // Deactivate relativistic VFX
    if (relativistic_vfx_manager && m_kart)
    {
        unsigned int kid = m_kart->getWorldKartId();
        switch (m_type)
        {
        case ATTACH_WARP_BUBBLE:
        case ATTACH_NOLOK_WARP_BUBBLE:
            relativistic_vfx_manager->deactivateWarpBubble(kid);
            break;
        case ATTACH_TIME_DILATION:
            relativistic_vfx_manager->deactivateTimeDilation(kid);
            break;
        case ATTACH_TIDAL_ARM:
            relativistic_vfx_manager->deactivateTidalArm(kid);
            break;
        case ATTACH_COMPACTIFICATION:
            relativistic_vfx_manager->deactivateCompactification(kid);
            break;
        default: break;
        }
    }

    if (m_plugin)
    {
        delete m_plugin;
        m_plugin = NULL;
    }

    m_type = ATTACH_NOTHING;
    m_ticks_left = 0;
    m_initial_speed = 0;
    resetMaxwellBoltzmannState(0);
}   // clear

// -----------------------------------------------------------------------------
/** Saves the attachment state. Called as part of the kart saving its state.
 *  \param buffer The kart rewinder's state buffer.
 */
void Attachment::saveState(BareNetworkString *buffer) const
{
    // We use bit 6 to indicate if a previous owner is defined for a bomb,
    // bit 7 to indicate if the attachment is a plugin
    assert(ATTACH_MAX < 64);
    uint8_t bit_7 = 0;
    if (m_plugin)
    {
        bit_7 = 1 << 7;
    }
    uint8_t type = m_type | (( (m_type==ATTACH_BOMB) && (m_previous_owner!=NULL) )
                             ? (1 << 6) : 0 ) | bit_7;
    buffer->addUInt8(type);
    buffer->addUInt16(m_ticks_left);
    if (m_type==ATTACH_BOMB && m_previous_owner)
        buffer->addUInt8(m_previous_owner->getWorldKartId());
    if (m_type == ATTACH_TIME_DILATION)
        buffer->addUInt16(m_initial_speed);
    if (m_plugin)
        m_plugin->saveState(buffer);
}   // saveState

// -----------------------------------------------------------------------------
/** Called from the kart rewinder when resetting to a certain state.
 *  \param buffer The kart rewinder's buffer with the attachment state next.
 */
void Attachment::rewindTo(BareNetworkString *buffer)
{
    uint8_t type = buffer->getUInt8();
    bool is_plugin = (type >> 7 & 1) == 1;

    // mask out bit 6 and 7
    AttachmentType new_type = AttachmentType(type & 63);
    type &= 127;

    int16_t ticks_left = buffer->getUInt16();
    // Now it is a new attachment:
    if (type == (ATTACH_BOMB | 64))   // we have previous owner information
    {
        uint8_t kart_id = buffer->getUInt8();
        m_previous_owner = World::getWorld()->getKart(kart_id);
    }
    else
    {
        m_previous_owner = NULL;
    }

    if (new_type == ATTACH_TIME_DILATION)
        m_initial_speed = buffer->getUInt16();
    else
        m_initial_speed = 0;

    if (is_plugin)
    {
        if (!m_plugin)
            m_plugin = new Swatter(m_kart, -1, 0, this);
        m_plugin->restoreState(buffer);
    }
    else
    {
        // Remove unconfirmed plugin
        delete m_plugin;
        m_plugin = NULL;
    }

    m_type = new_type;
    m_ticks_left = ticks_left;
    resetMaxwellBoltzmannState(new_type == ATTACH_MASS_SPIKE ? ticks_left : 0);
}   // rewindTo

// -----------------------------------------------------------------------------
/** Selects the new attachment. In order to simplify synchronisation with the
 *  server, the new item is based on the current world time.
 *  \param item The item that was collected.
 */
void Attachment::hitBanana(ItemState *item_state)
{
    if (m_kart->getController()->canGetAchievements())
    {
        PlayerManager::increaseAchievement(AchievementsStatus::BANANA, 1);
        if (RaceManager::get()->isLinearRaceMode())
            PlayerManager::increaseAchievement(AchievementsStatus::BANANA_1RACE, 1);
    }
    //Bubble gum shield effect:
    if(m_type == ATTACH_WARP_BUBBLE ||
       m_type == ATTACH_NOLOK_WARP_BUBBLE)
    {
        m_ticks_left = 0;
        return;
    }

    int leftover_ticks = 0;

    bool add_a_new_item = true;

    if (RaceManager::get()->isBattleMode())
    {
        World::getWorld()->kartHit(m_kart->getWorldKartId());
        if (m_kart->getKartAnimation() == NULL)
            ExplosionAnimation::create(m_kart);
        return;
    }

    const KartProperties *kp = m_kart->getKartProperties();
    if (item_state != NULL && item_state->getType() == Item::ITEM_BANANA)
    {
        if (m_kart->isInvulnerable() || m_kart->getKartAnimation() != NULL)
            return;

        // Visual squash only (no speed reduction); doubled duration
        const float compact_duration = kp->getParachuteDurationOther() * 2.0f;
        m_kart->setSquash(compact_duration, 1.0f);

        set(ATTACH_COMPACTIFICATION,
            stk_config->time2Ticks(compact_duration));
        return;
    }

    AttachmentType new_attachment = ATTACH_NOTHING;
    // Use this as a basic random number to make sync with server easier.
    // Divide by 16 to increase probablity to have same time as server in
    // case of a few physics frames different between client and server.
    int ticks = World::getWorld()->getTicksSinceStart() / 16;
    switch(getType())   // If there already is an attachment, make it worse :)
    {
    case ATTACH_BOMB:
        {
        add_a_new_item = false;
        if (!GUIEngine::isNoGraphics() && !RewindManager::get()->isRewinding())
        {
            HitEffect* he = new Explosion(m_kart->getXYZ(), "explosion",
                "explosion_bomb.xml");
            // Rumble!
            Controller* controller = m_kart->getController();
            if (controller && controller->isLocalPlayerController())
            {
                controller->rumble(0, 0.8f, 500);
            }
            if (m_kart->getController()->isLocalPlayerController())
                he->setLocalPlayerKartHit();
            ProjectileManager::get()->addHitEffect(he);
        }
        if (m_kart->getKartAnimation() == NULL)
            ExplosionAnimation::create(m_kart);
        clear();
        new_attachment = AttachmentType(ticks % 3);
        // Disable the banana on which the kart just is for more than the
        // default time. This is necessary to avoid that a kart lands on the
        // same banana again once the explosion animation is finished, giving
        // the kart the same penalty twice.
        int ticks =
            std::max(item_state->getTicksTillReturn(),
                     stk_config->time2Ticks(kp->getExplosionDuration() + 2.0f));
        item_state->setTicksTillReturn(ticks);
        break;
        }
    case ATTACH_MASS_SPIKE:
        // Maxwell-Boltzmann refreshes to a clean full-duration window.
        new_attachment = ATTACH_MASS_SPIKE;
        leftover_ticks  = 0;
        break;
    case ATTACH_TIME_DILATION:
        new_attachment = ATTACH_TIME_DILATION;
        leftover_ticks  = m_ticks_left;
        break;
    default:
        // There is no attachment currently, but there will be one
        // so play the character sound ("Uh-Oh")
        m_kart->playCustomSFX(SFXManager::CUSTOM_ATTACH);

        if (RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_TIME_TRIAL)
            new_attachment = AttachmentType(ticks % 2);
        else
            new_attachment = AttachmentType(ticks % 3);
    }   // switch

    if (add_a_new_item)
    {
        switch (new_attachment)
        {
        case ATTACH_TIME_DILATION:
        {
            int parachute_ticks = stk_config->time2Ticks(
                kp->getParachuteDuration()) + leftover_ticks;
            set(ATTACH_TIME_DILATION, parachute_ticks);
            int initial_speed_round = (int)(m_kart->getSpeed() * 100.0f);
            initial_speed_round =
                irr::core::clamp(initial_speed_round, -32768, 32767);
            m_initial_speed = (int16_t)initial_speed_round;
            // if going very slowly or backwards,
            // braking won't remove parachute
            if (m_initial_speed <= 150) m_initial_speed = 150;
            break;
        }
        case ATTACH_MASS_SPIKE:
            set(ATTACH_MASS_SPIKE,
                stk_config->time2Ticks(getMaxwellBoltzmannDurationSeconds()));
            break;
        case ATTACH_BOMB:
            set( ATTACH_BOMB, stk_config->time2Ticks(stk_config->m_bomb_time)
                            + leftover_ticks                                 );
            break;
        default:
            break;
        }   // switch
    }
}   // hitBanana

//-----------------------------------------------------------------------------
/** Updates the attachments in case of a kart-kart collision. This must only
 *  be called for one of the karts in the collision, since it will update
 *  the attachment for both karts.
 *  \param other Pointer to the other kart hit.
 */
void Attachment::handleCollisionWithKart(AbstractKart *other)
{
    Attachment *attachment_other=other->getAttachment();

    if(getType()==Attachment::ATTACH_BOMB)
    {
        // Don't attach a bomb when the kart is shielded
        if(other->isShielded())
        {
            other->decreaseShieldTime();
            return;
        }
        // If both karts have a bomb, explode them immediately:
        if(attachment_other->getType()==Attachment::ATTACH_BOMB)
        {
            setTicksLeft(0);
            attachment_other->setTicksLeft(0);
        }
        else  // only this kart has a bomb, move it to the other
        {
            // if there are only two karts, let them switch bomb from one to other
            if (getPreviousOwner() != other ||
                World::getWorld()->getNumKarts() <= 2)
            {
                // Don't move if this bomb was from other kart originally
                other->getAttachment()
                    ->set(ATTACH_BOMB,
                          getTicksLeft()+stk_config->time2Ticks(
                                           stk_config->m_bomb_time_increase),
                          m_kart);
                other->playCustomSFX(SFXManager::CUSTOM_ATTACH);
                clear();
            }
        }
    }   // type==BOMB
    else if(attachment_other->getType()==Attachment::ATTACH_BOMB &&
             (attachment_other->getPreviousOwner()!=m_kart ||
               World::getWorld()->getNumKarts() <= 2         )      )
    {
        // Don't attach a bomb when the kart is shielded
        if(m_kart->isShielded())
        {
            m_kart->decreaseShieldTime();
            return;
        }
        set(ATTACH_BOMB,
            other->getAttachment()->getTicksLeft()+
               stk_config->time2Ticks(stk_config->m_bomb_time_increase),
            other);
        other->getAttachment()->clear();
        m_kart->playCustomSFX(SFXManager::CUSTOM_ATTACH);
    }
    else
    {
        m_kart->playCustomSFX(SFXManager::CUSTOM_CRASH);
        other->playCustomSFX(SFXManager::CUSTOM_CRASH);
    }

}   // handleCollisionWithKart

//-----------------------------------------------------------------------------
void Attachment::update(int ticks)
{
    if(m_type==ATTACH_NOTHING) return;

    // suspend the bomb during animations to avoid having 2 animations at the
    // same time should the bomb explode before the previous animation is done
    if (m_type == ATTACH_BOMB && m_kart->getKartAnimation() != NULL)
        return;

    m_ticks_left -= ticks;

    if (m_plugin)
    {
        if (m_plugin->updateAndTestFinished())
        {
            clear();  // also removes the plugin
            return;
        }
    }

    switch (m_type)
    {
    case ATTACH_MASS_SPIKE:
        updateMaxwellBoltzmann(ticks);
        m_initial_speed = 0;
        break;
    case ATTACH_TIME_DILATION:
        {
        // Partly handled in Kart::updatePhysics
        // Otherwise: disable if a certain percantage of
        // initial speed was lost
        // This percentage is based on the ratio of
        // initial_speed / initial_max_speed

        const KartProperties *kp = m_kart->getKartProperties();

        float initial_speed = (float)m_initial_speed / 100.f;
        float f = initial_speed / kp->getParachuteMaxSpeed();
        if (f > 1.0f) f = 1.0f;   // cap fraction
        if (m_kart->getSpeed() <= initial_speed *
                                 (kp->getParachuteLboundFraction() +
                                  f * (kp->getParachuteUboundFraction()
                                     - kp->getParachuteLboundFraction())))
        {
            m_ticks_left = -1;
        }
        }
        break;
    case ATTACH_SUPERPOSITION_CAT:
    case ATTACH_COMPACTIFICATION:
    case ATTACH_NOTHING:   // Nothing to do, but complete all cases for switch
    case ATTACH_MAX:
        m_initial_speed = 0;
        break;
    case ATTACH_TIDAL_ARM:
        // Everything is done in the plugin.
        m_initial_speed = 0;
        break;
    case ATTACH_NOLOKS_SWATTER:
    case ATTACH_TIDAL_ARM_ANIM:
        // Should never be called, these symbols are only used as an index for
        // the model, Nolok's attachment type is ATTACH_TIDAL_ARM
        assert(false);
        break;
    case ATTACH_BOMB:
    {
        m_initial_speed = 0;
        if (m_ticks_left <= 0)
        {
            if (!GUIEngine::isNoGraphics() && !RewindManager::get()->isRewinding())
            {
                HitEffect* he = new Explosion(m_kart->getXYZ(), "explosion",
                    "explosion_bomb.xml");
                // Rumble!
                Controller* controller = m_kart->getController();
                if (controller && controller->isLocalPlayerController())
                {
                    controller->rumble(0, 0.8f, 500);
                }
                if (m_kart->getController()->isLocalPlayerController())
                    he->setLocalPlayerKartHit();
                ProjectileManager::get()->addHitEffect(he);
            }
            if (m_kart->getKartAnimation() == NULL)
                ExplosionAnimation::create(m_kart);
        }
        break;
    }
    case ATTACH_WARP_BUBBLE:
    case ATTACH_NOLOK_WARP_BUBBLE:
        m_initial_speed = 0;
        if (m_ticks_left <= 0)
        {
            if (!RewindManager::get()->isRewinding())
            {
                if (m_bubble_explode_sound) m_bubble_explode_sound->deleteSFX();
                m_bubble_explode_sound =
                    SFXManager::get()->createSoundSource("bubblegum_explode");
                m_bubble_explode_sound->setPosition(m_kart->getXYZ());
                m_bubble_explode_sound->play();
            }
            if (!m_kart->isGhostKart())
                Track::getCurrentTrack()->getItemManager()->dropNewItem(Item::ITEM_BUBBLEGUM, m_kart);
        }
        break;
    }   // switch

    // Detach attachment if its time is up.
    if (m_ticks_left <= 0)
        clear();
}   // update

// ----------------------------------------------------------------------------
void Attachment::updateGraphics(float dt)
{
    // Add the suitable graphical effects if different attachment is set
    if (m_type != m_graphical_type)
    {
        // Attachement is different, reset and add suitable sfx effects
        m_node->setPosition(core::vector3df(0.0f, 0.0f, 0.0f));
        m_node->setRotation(core::vector3df(0.0f, 0.0f, 0.0f));
        m_node->setScale(core::vector3df(1.0f, 1.0f, 1.0f));
        m_node->setLoopMode(true);
        switch (m_type)
        {
        case ATTACH_NOTHING:
            break;
        case ATTACH_TIDAL_ARM:
            // Graphical model set in swatter class
            break;
        case ATTACH_COMPACTIFICATION:
            // Pure screen-space effect — no attachment mesh
            break;
        default:
            m_node->setMesh(attachment_manager->getMesh(m_type));
            break;
        }   // switch(type)

        if (m_type != ATTACH_NOTHING)
        {
            m_node->setAnimationSpeed(0);
            m_node->setCurrentFrame(0);
        }
        if (UserConfigParams::m_particles_effects > 1 &&
            m_type == ATTACH_TIME_DILATION)
        {
            // .blend was created @25 (<10 real, slow computer), make it faster
            m_node->setAnimationSpeed(50);
        }
        m_graphical_type = m_type;
    }

    if (m_plugin)
        m_plugin->updateGraphics(dt);

    if (m_type != ATTACH_NOTHING)
    {
        // Time-dilation no longer renders its legacy trailing parachute mesh.
        // These debuffs are represented through HUD/VFX, not a rear mesh.
        const bool hide_attachment_mesh = (m_type == ATTACH_TIME_DILATION ||
                                           m_type == ATTACH_MASS_SPIKE    ||
                                           m_type == ATTACH_COMPACTIFICATION);
        m_node->setVisible(!hide_attachment_mesh);
        bool is_shield = m_type == ATTACH_WARP_BUBBLE ||
                        m_type == ATTACH_NOLOK_WARP_BUBBLE;
        float wanted_node_scale = is_shield ?
            std::max(1.0f, m_kart->getHighestPoint() * 1.1f) : 1.0f;
        if (m_type == ATTACH_SUPERPOSITION_CAT)
        {
            wanted_node_scale =
                std::max(0.7f, std::min(1.0f, m_kart->getHighestPoint() * 0.7f));
        }
        float scale_ratio = stk_config->ticks2Time(m_scaling_end_ticks -
            World::getWorld()->getTicksSinceStart()) / 0.7f;
        if (scale_ratio > 0.0f)
        {
            if (m_type == ATTACH_TIME_DILATION)
            {
                const float progress = 1.0f - scale_ratio;

                const float x = 0.2f * atan(20.0f * progress - 5.0f) + 0.7f;
                const float y = x;
                const float z = 1.0f - pow(2.0f, -15.f * progress);

                m_node->setScale(core::vector3df(x * wanted_node_scale,
                                                 y * wanted_node_scale,
                                                 z * wanted_node_scale));
            }
            else
            {
                if (is_shield)
                {
                    // Taken from https://easings.net/#easeInElastic
                    const float c4 = (2.0f * PI) / 3.0f;
                    const float x = scale_ratio;

                    scale_ratio = x <= 0 ? 0 : x >= 1 ? 1
                      : -pow(2, 8 * x - 8) * sin((x * 8 - 8.75) * c4);
                }

                float scale = 0.3f * scale_ratio +
                    wanted_node_scale * (1.0f - scale_ratio);
                m_node->setScale(core::vector3df(scale, scale, scale));
            }
        }
        else
        {
            m_node->setScale(core::vector3df(
                wanted_node_scale, wanted_node_scale, wanted_node_scale));
        }
        int slow_flashes = stk_config->time2Ticks(3.0f);
        if (is_shield && m_ticks_left < slow_flashes)
        {
            // Bubble gum flashing when close to dropping
            int ticks_per_flash = stk_config->time2Ticks(0.2f);

            int fast_flashes = stk_config->time2Ticks(0.5f);
            if (m_ticks_left < fast_flashes)
            {
                ticks_per_flash = stk_config->time2Ticks(0.07f);
            }

            int division = (m_ticks_left / ticks_per_flash);
            m_node->setVisible((division & 0x1) == 0);
        }

        if (m_type == ATTACH_SUPERPOSITION_CAT)
        {
            const float y = std::max(0.25f, m_kart->getHighestPoint() * 0.45f);
            const float z = std::max(0.35f, m_kart->getKartLength() * 0.52f);
            m_node->setPosition(core::vector3df(0.0f, y, z));
        }

    }
    else
        m_node->setVisible(false);

    switch (m_type)
    {
    case ATTACH_BOMB:
    {
        if (!m_bomb_sound)
        {
            m_bomb_sound = SFXManager::get()->createSoundSource("clock");
            m_bomb_sound->setLoop(true);
            m_bomb_sound->play();
        }
        m_bomb_sound->setPosition(m_kart->getXYZ());
        // Mesh animation frames are 1 to 61 frames (60 steps)
        // The idea is change second by second, counterclockwise 60 to 0 secs
        // If longer times needed, it should be a surprise "oh! bomb activated!"
        float time_left = stk_config->ticks2Time(m_ticks_left);
        if (time_left <= (m_node->getEndFrame() - m_node->getStartFrame() - 1))
        {
            m_node->setCurrentFrame(m_node->getEndFrame()
                - m_node->getStartFrame() - 1 - time_left);
        }
        return;
    }
    default:
        break;
    }   // switch

    if (m_bomb_sound)
    {
        m_bomb_sound->deleteSFX();
        m_bomb_sound = NULL;
    }
}   // updateGraphics

// ----------------------------------------------------------------------------
/** Return the additional weight of the attachment (some attachments slow
 *  karts down by also making them heavier).
 */
float Attachment::weightAdjust() const
{
    return (m_type == ATTACH_SUPERPOSITION_CAT)
           ? m_kart->getKartProperties()->getAnvilWeight()
          : 0.0f;
}   // weightAdjust
