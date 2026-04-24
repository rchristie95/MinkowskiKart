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

#include "items/powerup.hpp"

#include "graphics/relativistic_vfx.hpp"
#include "audio/sfx_base.hpp"
#include "audio/sfx_manager.hpp"
#include "config/player_manager.hpp"
#include "config/stk_config.hpp"
#include "items/attachment.hpp"
#include "items/item_manager.hpp"
#include "items/projectile_manager.hpp"
#include "items/rubber_ball.hpp"
#include "karts/abstract_kart.hpp"
#include "karts/max_speed.hpp"
#include "karts/controller/controller.hpp"
#include "karts/kart_properties.hpp"
#include "modes/world.hpp"
#include "network/network_config.hpp"
#include "network/network_string.hpp"
#include "network/rewind_manager.hpp"
#include "physics/triangle_mesh.hpp"
#include "tracks/track.hpp"
#include "utils/string_utils.hpp"
#include "utils/log.hpp" //TODO: remove after debugging is done

namespace
{
uint64_t mixItemCollectionSeed(uint64_t value)
{
    value = (1664525L * value + 1013904223L);
    value ^= (value >> 16);
    value ^= (value >> 8);
    return value;
}

uint64_t getItemCollectionSeed(const ItemState &item_state, int position,
                               uint64_t random_seed, int ticks,
                               uint64_t salt = 0)
{
    uint64_t value = item_state.getItemId() * 31ULL +
        static_cast<uint64_t>(ticks / 10) +
        static_cast<uint64_t>(position) * 23ULL +
        random_seed + salt;
    return mixItemCollectionSeed(value);
}
}   // namespace

//-----------------------------------------------------------------------------
/** Constructor, stores the kart to which this powerup belongs.
 *  \param kart The kart to which this powerup belongs.
 */
Powerup::Powerup(AbstractKart* kart)
{
    m_kart      = kart;
    m_sound_use = NULL;
    reset();
}   // Powerup

//-----------------------------------------------------------------------------
/** Frees the memory for the sound effects.
 */
Powerup::~Powerup()
{
    if(m_sound_use) m_sound_use->deleteSFX();
}   // ~Powerup

//-----------------------------------------------------------------------------
/** Resets the powerup, called at begin of a race.
 */
void Powerup::reset()
{
    m_type = PowerupManager::POWERUP_NOTHING;
    m_number = 0;

    // Ghost kart will update powerup every frame
    if (m_kart->isGhostKart())
        return;
    int type, number;
    World::getWorld()->getDefaultCollectibles( &type, &number );
    set( (PowerupManager::PowerupType)type, number );
}   // reset

//-----------------------------------------------------------------------------
/** Save the powerup state. Called from the kart rewinder when saving the kart
 *  state or when a new powerup even is saved.
 *  \param buffer The buffer into which to save the state.
 */
void Powerup::saveState(BareNetworkString *buffer) const
{
    buffer->addUInt8(uint8_t(m_type));
    buffer->addUInt8(m_number);   // number is <=255
}   // saveState

//-----------------------------------------------------------------------------
/** Restore a powerup state. Called from the kart rewinder when restoring a
 *  state.
 *  \param buffer Buffer with the state of this powerup object.
 */
void Powerup::rewindTo(BareNetworkString *buffer)
{
    PowerupManager::PowerupType new_type =
        PowerupManager::PowerupType(buffer->getUInt8());
    int n=0;
    if(new_type==PowerupManager::POWERUP_NOTHING)
    {
        set(new_type, 0);
        return;
    }
    n = buffer->getUInt8();
    if(m_type == new_type)
        m_number = n;
    else
    {
        m_number = 0;
        set(new_type, n);
    }
}   // rewindTo

//-----------------------------------------------------------------------------
void Powerup::update(int ticks)
{
    // Remove any sound ticks that should have played
    const int remove_ticks = World::getWorld()->getTicksSinceStart() - 1000;
    for (auto it = m_played_sound_ticks.begin();
         it != m_played_sound_ticks.end();)
    {
        if (*it < remove_ticks)
        {
            it = m_played_sound_ticks.erase(it);
            continue;
        }
        break;
    }
}   // update

//-----------------------------------------------------------------------------
/** Sets the collected items. The number of items is increased if the same
 *  item is currently collected, otherwise replaces the existing item. It also
 *  sets item specific sounds.
 *  \param type Thew new type.
 *  \param n Number of items of the given type.
 */
void Powerup::set(PowerupManager::PowerupType type, int n)
{
    if (m_type==type)
    {
        m_number+=n;
        // Limit to 255 (save space in network state saving)
        if(m_number>255) m_number = 255;
        return;
    }
    m_type=type;

    // Limit to 255 (save space in network state saving)
    if(n>255) n = 255;

    m_number=n;

    if (m_sound_use != NULL)
    {
        m_sound_use->deleteSFX();
        m_sound_use = NULL;
    }

    switch (m_type)
    {
        case PowerupManager::POWERUP_ZIPPER:
            m_sound_use = SFXManager::get()->createSoundSource("rocket_boost");
            break ;

        case PowerupManager::POWERUP_BLACK_HOLE:
            m_sound_use = SFXManager::get()->createSoundSource("bowling_shoot");
            break ;

        case PowerupManager::POWERUP_MASS_SPIKE:
            m_sound_use = SFXManager::get()->createSoundSource("anvil");
            break;

        case PowerupManager::POWERUP_TIME_DILATION:
            m_sound_use = SFXManager::get()->createSoundSource("parachute");
            break;

        case PowerupManager::POWERUP_WARP_BUBBLE:
            m_sound_use = SFXManager::get()->createSoundSource("goo");
            break ;

        case PowerupManager::POWERUP_WORMHOLE:
            m_sound_use = SFXManager::get()->createSoundSource("portal");
            break;

        case PowerupManager::POWERUP_SUPER_POSITION:
            m_sound_use = SFXManager::get()->createSoundSource("swap");
            break;

        case PowerupManager::POWERUP_NOTHING:
        case PowerupManager::POWERUP_COSMIC_STRING:
        default :
            m_sound_use = SFXManager::get()->createSoundSource("shoot");
            break ;
    }

}  // set

//-----------------------------------------------------------------------------
/** Sets the amount of the current collected item.
 *  \param n Number of items.
 */
void Powerup::setNum(int n)
{
    // Limit to 255 (save space in network state saving)
    if(n>255) n = 255;

    m_number=n;
}

//-----------------------------------------------------------------------------
/** Returns the icon for the currently collected powerup. Used in the
 *  race_gui to display the collected item.
 */
Material *Powerup::getIcon() const
{
    // Check if it's one of the types which have a separate
    // data file which includes the icon:
    return powerup_manager->getIcon(m_type);
}   // getIcon

//-----------------------------------------------------------------------------
/** Does the sound configuration.
 */
void Powerup::adjustSound()
{
    m_sound_use->setPosition(m_kart->getXYZ());
    // in multiplayer mode, sounds are NOT positional (because we have multiple listeners)
    // so the sounds of all AIs are constantly heard. So reduce volume of sounds.
    if (RaceManager::get()->getNumLocalPlayers() > 1)
    {
        // player karts played at full volume; AI karts much dimmer

        if (m_kart->getController()->isLocalPlayerController())
        {
            m_sound_use->setVolume( 1.0f );
        }
        else
        {
            m_sound_use->setVolume(
                     std::min(0.5f, 1.0f / RaceManager::get()->getNumberOfKarts()) );
        }
    }
}   // adjustSound

//-----------------------------------------------------------------------------
/** Use (fire) this powerup.
 */
void Powerup::use()
{
    const int ticks = World::getWorld()->getTicksSinceStart();
    bool has_played_sound = false;
    auto it = m_played_sound_ticks.find(ticks);
    if (it != m_played_sound_ticks.end())
        has_played_sound = true;
    else
        m_played_sound_ticks.insert(ticks);

    const KartProperties *kp = m_kart->getKartProperties();

    // The player gets an achievement point for using a powerup
    if (m_type != PowerupManager::POWERUP_NOTHING      &&
        m_kart->getController()->canGetAchievements()    )
    {
        PlayerManager::increaseAchievement(AchievementsStatus::POWERUP_USED, 1);
        if (RaceManager::get()->isLinearRaceMode())
            PlayerManager::increaseAchievement(AchievementsStatus::POWERUP_USED_1RACE, 1);
    }

    // Play custom kart sound when collectible is used //TODO: what about the bubble gum?
    if (m_type != PowerupManager::POWERUP_NOTHING &&
        m_type != PowerupManager::POWERUP_ZIPPER)
        m_kart->playCustomSFX(SFXManager::CUSTOM_SHOOT);

    // FIXME - for some collectibles, set() is never called
    if (!has_played_sound && m_sound_use == NULL)
    {
        m_sound_use = SFXManager::get()->createSoundSource(
            m_type == PowerupManager::POWERUP_ZIPPER ? "rocket_boost" : "shoot");
    }

    m_number--;
    World *world = World::getWorld();
    ItemManager* im = Track::getCurrentTrack()->getItemManager();
    switch (m_type)
    {
    case PowerupManager::POWERUP_ZIPPER:
        if (!has_played_sound && m_sound_use != NULL)
        {
            Powerup::adjustSound();
            m_sound_use->play();
        }
        m_kart->handleZipper(NULL, true);
        break ;
    case PowerupManager::POWERUP_SUPER_POSITION:
        {
            im->switchItems();
            // Trigger the sweeping Lorentz grid wave visual
            if (relativistic_vfx_manager)
                relativistic_vfx_manager->triggerSuperPosition(m_kart->getXYZ());
            if (!has_played_sound)
            {
                m_sound_use->setPosition(m_kart->getXYZ());
                m_sound_use->play();
            }
            break;
        }
    case PowerupManager::POWERUP_ASTEROID:
    case PowerupManager::POWERUP_WORMHOLE:
    case PowerupManager::POWERUP_BLACK_HOLE:
    case PowerupManager::POWERUP_COSMIC_STRING:
    case PowerupManager::POWERUP_ANTI_KARTICLE:
        if(stk_config->m_shield_restrict_weapons)
            m_kart->setShieldTime(0.0f); // make weapon usage destroy the shield
        if (!has_played_sound)
        {
            Powerup::adjustSound();
            m_sound_use->play();
        }
        ProjectileManager::get()->newProjectile(m_kart, m_type);
        break ;

    case PowerupManager::POWERUP_WARP_BUBBLE:
        // use the bubble gum the traditional way, if the kart is looking back
        if (m_kart->getControls().getLookBack())
        {
            Item *new_item = im->dropNewItem(Item::ITEM_BUBBLEGUM, m_kart);

            // E.g. ground not found in raycast.
            if(!new_item) return;
            if (!has_played_sound)
            {
                Powerup::adjustSound();
                m_sound_use->play();
            }
        }
        else // if the kart is looking forward, use the bubblegum as a shield
        {

            if(!m_kart->isShielded()) //if the previous shield had been used up.
            {
                if (m_kart->getIdent() == "nolok")
                {
                    m_kart->getAttachment()
                          ->set(Attachment::ATTACH_NOLOK_WARP_BUBBLE,
                                stk_config->
                                  time2Ticks(kp->getBubblegumShieldDuration()));
                }
                else
                {
                    m_kart->getAttachment()
                          ->set(Attachment::ATTACH_WARP_BUBBLE,
                                stk_config->
                                  time2Ticks(kp->getBubblegumShieldDuration()));
                }
            }
            else // using a bubble gum while still having a shield
            {
                if (m_kart->getIdent() == "nolok")
                {
                    m_kart->getAttachment()
                          ->set(Attachment::ATTACH_NOLOK_WARP_BUBBLE,
                                stk_config->
                                 time2Ticks(kp->getBubblegumShieldDuration()));
                }
                else
                {
                    m_kart->getAttachment()
                          ->set(Attachment::ATTACH_WARP_BUBBLE,
                                stk_config->
                                time2Ticks(kp->getBubblegumShieldDuration()
                                           + m_kart->getShieldTime()       ) );
                }
            }

            // Add 7.5 m/s max speed increase for Warp Bubble (BubbleGum shield)
            m_kart->increaseMaxSpeed(MaxSpeed::MS_INCREASE_WARP_BUBBLE,
                                     7.5f, 0.0f,
                                     stk_config->time2Ticks(kp->getBubblegumShieldDuration()),
                                     stk_config->time2Ticks(0.5f));

            if (!has_played_sound)
            {
                if (m_sound_use != NULL)
                {
                    m_sound_use->deleteSFX();
                    m_sound_use = NULL;
                }
                //Extraordinary. Usually sounds are set in Powerup::set()
                m_sound_use = SFXManager::get()->createSoundSource("inflate");
                //In this case this is a workaround, since the bubblegum item has two different sounds.

                Powerup::adjustSound();
                m_sound_use->play();
            }

        }   // end of PowerupManager::POWERUP_WARP_BUBBLE
        break;

    case PowerupManager::POWERUP_MASS_SPIKE:
        // Maxwell-Boltzmann: punish the leader with deterministic Brownian
        // velocity kicks in the local track-tangent plane.
        for(unsigned int i = 0 ; i < world->getNumKarts(); ++i)
        {
            AbstractKart *kart=world->getKart(i);
            if(kart->isEliminated() || kart->isInvulnerable()) continue;
            if(kart == m_kart) continue;
            if(kart->getPosition() == 1)
            {
                kart->getAttachment()->set(Attachment::ATTACH_MASS_SPIKE,
                                           stk_config->time2Ticks(
                                               Attachment::
                                               getMaxwellBoltzmannDurationSeconds()) );

                // Position the legacy hit sound near the affected leader if local,
                // otherwise near the kart that used the powerup.
                // Meanwhile, don't play it near AI karts since they obviously
                // don't hear anything
                if (!has_played_sound)
                {
                    if(kart->getController()->isLocalPlayerController())
                        m_sound_use->setPosition(kart->getXYZ());
                    else
                        m_sound_use->setPosition(m_kart->getXYZ());

                    m_sound_use->play();
                }
                break;
            }
        }

        break;

    case PowerupManager::POWERUP_TIME_DILATION:
        {
            AbstractKart* player_kart = NULL;
            // Apply the time-dilation field to every other active kart. Karts
            // ahead keep the old rank-scaled duration bonus; karts behind use
            // the base "other kart" duration.
            for(unsigned int i = 0 ; i < world->getNumKarts(); ++i)
            {
                AbstractKart *kart=world->getKart(i);
                if(kart->isEliminated() || kart== m_kart || kart->isInvulnerable()) continue;
                if(kart->isShielded())
                {
                    kart->decreaseShieldTime();
                    continue;
                }

                float rank_mult = 1.0f;
                if(m_kart->getPosition() > kart->getPosition())
                {
                    float position_factor = 1.0f;
                    if (kart->getPosition() == 1)
                    {
                        position_factor = 1.0f;
                    }
                    else if (m_kart->getPosition() > 2)
                    {
                        float rank_factor;

                        rank_factor = (float)(kart->getPosition()   - 1)
                                    / (float)(m_kart->getPosition() - 2);
                        position_factor = 1.0f - rank_factor;
                    }
                    if (position_factor < 0.0f)
                        position_factor = 0.0f;
                    else if (position_factor > 1.0f)
                        position_factor = 1.0f;

                    rank_mult = 1 + (position_factor *
                                     (kp->getParachuteDurationRankMult() - 1));
                }

                kart->getAttachment()
                    ->set(Attachment::ATTACH_TIME_DILATION,
                          stk_config->time2Ticks(
                              kp->getParachuteDurationOther() * rank_mult),
                          m_kart);

                if(kart->getController()->isLocalPlayerController())
                    player_kart = kart;
            }

            // should we position the sound at the kart that is hit,
            // or the kart using the powerup? Ideally it should be both.
            // Meanwhile, don't play it near AI karts since they obviously
            // don't hear anything
            if (!has_played_sound)
            {
                if(m_kart->getController()->isLocalPlayerController())
                    m_sound_use->setPosition(m_kart->getXYZ());
                else if(player_kart)
                    m_sound_use->setPosition(player_kart->getXYZ());
                m_sound_use->play();
            }
        }
        break;

    case PowerupManager::POWERUP_NOTHING:
        {
            if(!m_kart->getKartAnimation())
                m_kart->beep();
        }
        break;
    default : break;
    }

    if ( m_number <= 0 )
    {
        m_number = 0;
        m_type   = PowerupManager::POWERUP_NOTHING;
    }
}   // use

//-----------------------------------------------------------------------------
/** This function is called when a bnous box is it. This function can be
 *  called on a server (in which case item and add_info are not used),
 *  or on a client, in which case the item and additional info is used
 *  to make sure server and clients are synched correctly.
 *  \param n
 *  \param item_state The item_state (bonux box) that was hit. This is
 *         necessary for servers so that the clients can be informed which
 *         item was collected.
 */
void Powerup::hitBonusBox(const ItemState &item_state)
{
    // Position can be -1 in case of a battle mode (which doesn't have
    // positions), but this case is properly handled in getRandomPowerup.
    int position = m_kart->getPosition();

    unsigned int n=1;
    PowerupManager::PowerupType new_powerup;
    World *world = World::getWorld();

    // Determine a 'random' number based on time, index of the item,
    // and position of the kart. The idea is that this process is
    // randomly enough to get the right distribution of the powerups,
    // does not involve additional network communication to keep
    // client and server in sync, and is not exploitable:
    // While it is not possible for a client to determine the item
    // (the server will always finally determine which item a player
    // receives), we need to make sure that people cannot modify the
    // sources and display the item that will be collected next
    // at a box - otherwise the player could chose the 'best' box.
    // Using synchronised pseudo-random-generators would not prevent
    // cheating, since the a cheater could determine the next random
    // number that will be used. If we use the server to always
    // send the information to the clients, we need to add a delay
    // before items can be used.
    // So instead we determine a random number that is based on:
    // (1) The item id
    // (2) The time
    // (3) The position of the kart
    // (4) An extra random 64bit integer
    // Using (1) means that not all boxes at a certain time for a kart
    // will give the same box. Using (2) means that the item will
    // change over time - even if the next item is displayed, it
    // will mean a cheater has to wait, and because of the frequency
    // of the time component it will also be difficult to get the
    // item at the right time. Using (3) adds another cheat-prevention
    // layer: even if a cheater is waiting for the right sequence
    // of items, if he is overtaken the sequence will change, using (4)
    // to avoid same item sequence when starting
    //
    // In order to increase the probability of correct client prediction
    // in networking (where there might be 1 or 2 frames difference
    // between client and server when collecting an item), the time
    // is divided by 10, meaning even if there is one frame difference,
    // the client will still have a 90% chance to correctly predict the
    // item. We multiply the item with a 'large' (more or less random)
    // number to spread the random values across the (typically 200)
    // weights used in the PowerupManager - same for the position.
    uint64_t random_number = getItemCollectionSeed(item_state, position,
        powerup_manager->getRandomSeed(), world->getTicksSinceStart());

    new_powerup = powerup_manager->getRandomPowerup(position, &n,
                                                    random_number);

    // Always add a new powerup in ITEM_MODE_NEW (or if the kart
    // doesn't have a powerup atm).
    if(m_type == PowerupManager::POWERUP_NOTHING ||
       stk_config->m_same_powerup_mode == STKConfig::POWERUP_MODE_NEW )
    {
        set( new_powerup, n );
    }
    else
    {
        // If powerup mode is 'SAME', or it's ONLY_IF_SAME and it is the
        // same powerup, increase the number of items.
        if(stk_config->m_same_powerup_mode == STKConfig::POWERUP_MODE_SAME ||
            new_powerup==m_type)
        {
            m_number+=n;
            if(m_number > MAX_POWERUPS)
                m_number = MAX_POWERUPS;
        }
    }
    // Ignore new powerup if it is different from the current one and not
    // POWERUP_MODE_SAME

}   // hitBonusBox

//-----------------------------------------------------------------------------
/** Called when a super-position item is hit. It deterministically resolves to
 *  either a normal bonus-box reward or a banana hit.
 *  \param item_state The super-position item that was hit.
 */
void Powerup::hitSuperPosition(ItemState *item_state)
{
    int position = m_kart->getPosition();
    World *world = World::getWorld();
    const uint64_t base_seed = getItemCollectionSeed(*item_state, position,
        powerup_manager->getRandomSeed(), world->getTicksSinceStart());

    if ((base_seed & 1ULL) == 0)
    {
        // Box outcome: warp bubble does not block collection.
        hitBonusBox(*item_state);
        return;
    }

    // Cat outcome: warp bubble absorbs the debuff (shield consumed, no slow).
    Attachment *attachment = m_kart->getAttachment();
    if (attachment->getType() == Attachment::ATTACH_WARP_BUBBLE ||
        attachment->getType() == Attachment::ATTACH_NOLOK_WARP_BUBBLE)
    {
        attachment->clear();
        return;
    }

    if (SFXManager::get())
        SFXManager::get()->quickSound("cat_meow");

    const KartProperties *kp = m_kart->getKartProperties();
    attachment->set(
        Attachment::ATTACH_SUPERPOSITION_CAT,
        stk_config->time2Ticks(kp->getAnvilDuration()));
    m_kart->adjustSpeed(kp->getAnvilSpeedFactor() * 0.5f);
}   // hitSuperPosition
