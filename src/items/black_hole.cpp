//
//  MinkowskiKart - a fun racing game with go-kart
//  Copyright (C) 2007-2015 Joerg Henrichs
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

#include "items/black_hole.hpp"

#include "audio/sfx_base.hpp"
#include "audio/sfx_manager.hpp"
#include "config/stk_config.hpp"
#include "graphics/hit_sfx.hpp"
#include "io/file_manager.hpp"
#include "ge_render_info.hpp"
#include "graphics/irr_driver.hpp"
#include "graphics/material.hpp"
#include "graphics/sp/sp_base.hpp"
#include "guiengine/engine.hpp"
#include "io/xml_node.hpp"
#include "karts/abstract_kart.hpp"
#include "modes/linear_world.hpp"
#include "modes/world.hpp"

#include <ISceneManager.h>
#include <ISceneNode.h>
#include <IBillboardSceneNode.h>
#include <SColor.h>

float BlackHole::m_st_max_distance;   // maximum distance for a black hole to be attracted
float BlackHole::m_st_max_distance_squared;
float BlackHole::m_st_force_to_target;

// Overall scale of the black hole relative to the base model extent, applied
// to both the collision sphere and the visual lens radius so gameplay and
// appearance stay consistent.
static const float BH_VISUAL_SCALE = 1.2f;

// -----------------------------------------------------------------------------
BlackHole::BlackHole(AbstractKart *kart)
        : Flyable(kart, PowerupManager::POWERUP_BLACK_HOLE, 50.0f /* mass */)
{
    m_has_hit_kart = false;
    m_expiry_ticks = 0;
    m_roll_sfx = SFXManager::get()->createSoundSource("bowling_roll");
    fixSFXSplitscreen(m_roll_sfx);
    m_roll_sfx->play();
    m_roll_sfx->setLoop(true);

#ifndef SERVER_ONLY
    // Hide the sphere mesh — the shader event horizon renders the black core.
    if (!GUIEngine::isNoGraphics() && getNode())
        getNode()->setVisible(false);
#endif
}   // BlackHole

// ----------------------------------------------------------------------------
/** Destructor, removes any playing sfx.
 */
BlackHole::~BlackHole()
{
#ifndef SERVER_ONLY
    SP::removeBlackHoleLens(this);
#endif
    removeRollSfx();
}   // ~BlackHole

// -----------------------------------------------------------------------------
/** Initialises this object with data from the power.xml file.
 *  \param node XML Node
 *  \param black_hole The black hole mesh
 */
void BlackHole::init(const XMLNode &node, scene::IMesh *black_hole)
{
    Flyable::init(node, black_hole, PowerupManager::POWERUP_BLACK_HOLE);
    m_st_max_distance         = 20.0f;
    m_st_max_distance_squared = 20.0f * 20.0f;
    m_st_force_to_target      = 10.0f;

    node.get("max-distance",    &m_st_max_distance   );
    m_st_max_distance_squared = m_st_max_distance*m_st_max_distance;

    node.get("force-to-target", &m_st_force_to_target);
}   // init

// ----------------------------------------------------------------------------
/** Updates the black hole in each frame. If this function returns true, the
 *  object will be removed by the projectile manager.
 *  \param dt Time step size.
 *  \returns True of this object should be removed.
 */
bool BlackHole::updateAndDelete(int ticks)
{
#ifndef SERVER_ONLY
    // Keep this ball's lensing entry up to date each frame. Several black
    // holes can be live at the same time; each registers under its own key.
    const Vec3& bhpos = getXYZ();

    // Compute collapse scale: linearly shrink from 1→0 over the final second.
    const float remaining = stk_config->ticks2Time(
        std::max(0, m_expiry_ticks - World::getWorld()->getTicksSinceStart()));
    const float collapse = remaining < 1.0f ? std::max(0.0f, remaining) : 1.0f;

    // Pass world-space sphere radius; shader projects this to screen pixels for R_E.
    // Lift the rendered singularity ~one radius above the rolling ball so it
    // sits around kart height as thrown (not hovering high), while the
    // generous shader occlusion margin keeps the disk from clipping into the
    // ground. BH_VISUAL_SCALE matches the enlarged collision sphere (see
    // onFireFlyable) so the lens tracks the bigger ball.
    const float vis_r = 0.5f * m_extend.getY() * BH_VISUAL_SCALE * collapse;
    SP::setBlackHoleLens(this, irr::core::vector3df(
        bhpos.getX(), bhpos.getY() + vis_r * 1.0f, bhpos.getZ()),
        vis_r);
#endif

    bool can_be_deleted = Flyable::updateAndDelete(ticks);
    if (can_be_deleted)
    {
#ifndef SERVER_ONLY
        SP::removeBlackHoleLens(this);
#endif
        removeRollSfx();
        return true;
    }

    const AbstractKart *kart=0;
    Vec3        direction;
    float       minDistSquared;
    getClosestKart(&kart, &minDistSquared, &direction);
    if(kart && minDistSquared<m_st_max_distance_squared)   // move black hole towards kart
    {
        // limit angle, so that the black hole does not turn
        // around to hit a kart behind
        if(fabs(m_body->getLinearVelocity().angle(direction)) < 1.3)
        {
            direction*=1/direction.length()*m_st_force_to_target;
            m_body->applyCentralForce(direction);
        }
    }
    
   
    // Black holes lose energy (e.g. when hitting the track), so increase
    // the speed if the ball is too slow, but only if it's not too high (if
    // the ball is too high, it is 'pushed down', which can reduce the
    // speed, which causes the speed to increase, which in turn causes
    // the ball to fly higher and higher.
    //btTransform trans = getTrans();
    float hat = (getXYZ() - getHitPoint()).length();
    if(hat-0.5f*m_extend.getY()<0.01f)
    {
        const Material *material = getMaterial();
        if(!material || material->isDriveReset())
        {
            hit(NULL);
            removeRollSfx();
            return true;
        }
    }
    btVector3 v       = m_body->getLinearVelocity();
    float vlen        = v.length2();
    if (hat<= m_max_height)
    {
        if(vlen<0.8*m_speed*m_speed)
        {   // black hole lost energy (less than 80%), i.e. it's too slow - speed it up:
            if(vlen==0.0f) {
                v = btVector3(.5f, .0, 0.5f);  // avoid 0 div.
            }
            m_body->setLinearVelocity(v*(m_speed/sqrt(vlen)));
        }   // vlen < 0.8*m_speed*m_speed
    }   // hat< m_max_height

    if(vlen<0.1)
    {
        hit(NULL);
        removeRollSfx();
        return true;
    }

    if (m_roll_sfx && m_roll_sfx->getStatus()==SFXBase::SFX_PLAYING)
        m_roll_sfx->setPosition(getXYZ());

    return false;
}   // updateAndDelete

// -----------------------------------------------------------------------------
/** Callback from the physics in case that a kart or physical object is hit.
 *  The black hole triggers an explosion when hit.
 *  \param kart The kart hit (NULL if no kart was hit).
 *  \param object The object that was hit (NULL if none).
 *  \returns True if there was actually a hit (i.e. not owner, and target is
 *           not immune), false otherwise.
 */
bool BlackHole::hit(AbstractKart* kart, PhysicalObject* obj)
{
    // Kart collisions: detonate damage on the hit kart but keep the black hole
    // alive in place. It continues to roll / lens the scene until its
    // m_max_lifespan (20s) expires, at which point the flyable framework
    // deletes it naturally. Shield still absorbs the hit, but also does not
    // remove the ball.
    if (kart && !isOwnerImmunity(kart) && m_has_server_state &&
        !hasAnimation())
    {
        if (kart->isShielded())
        {
            kart->decreaseShieldTime();
            return true;
        }
        if (!kart->getKartAnimation())
        {
            m_has_hit_kart = true;
            // Direct-hit-only explosion; do NOT call Flyable::hit(), since
            // that flag schedules this projectile for deletion next tick.
            explode(kart, obj, /*hit_secondary*/false);
        }
        return true;
    }

    // Non-kart collisions (track geometry, physical objects) fall back to the
    // original behaviour: the ball explodes and is removed.
    bool was_real_hit = Flyable::hit(kart, obj);
    if(was_real_hit)
    {
#ifndef SERVER_ONLY
        SP::removeBlackHoleLens(this);
#endif
        m_has_hit_kart = false;
        explode(kart, obj, /*hit_secondary*/false);
    }
    return was_real_hit;
}   // hit

// ----------------------------------------------------------------------------
void BlackHole::removeRollSfx()
{
    if (m_roll_sfx)
    {
        m_roll_sfx->deleteSFX();
        m_roll_sfx = NULL;
    }
}   // removeRollSfx

// ----------------------------------------------------------------------------
/** Returns the hit effect object to use when this objects hits something.
 *  \returns The hit effect object, or NULL if no hit effect should be played.
 */
HitEffect* BlackHole::getHitEffect() const
{
    if (GUIEngine::isNoGraphics())
        return NULL;
    if (m_deleted_once)
        return NULL;
    if(m_has_hit_kart)
        return new HitSFX(getXYZ(), "strike");
    else
        return new HitSFX(getXYZ(), "crash");
}   // getHitEffect

// ----------------------------------------------------------------------------
void BlackHole::onFireFlyable()
{
    Flyable::onFireFlyable();

    m_has_hit_kart = false;
    // Register this black hole for screen-space lensing in tonemap.frag.
    // Float the lens above the rolling ball so the disk clears the ground
    // (see updateAndDelete); visual only, physics unchanged.
#ifndef SERVER_ONLY
    const float vis_r = 0.5f * m_extend.getY() * BH_VISUAL_SCALE;
    SP::setBlackHoleLens(this, irr::core::vector3df(getXYZ().getX(),
        getXYZ().getY() + vis_r * 1.0f, getXYZ().getZ()), vis_r);
#endif
    m_expiry_ticks = World::getWorld()->getTicksSinceStart()
                   + stk_config->time2Ticks(20);
    float y_offset = 0.5f*m_owner->getKartLength() + m_extend.getZ()*0.5f;

    // if the kart is looking backwards, release from the back
    if( m_owner->getControls().getLookBack())
    {
        y_offset   = -y_offset;
        m_speed    = -m_speed*2;
    }
    else
    {
        float min_speed = m_speed*4.0f;
        /* make it go faster when throwing forward
           so the player doesn't catch up with the ball
           and explode by touching it */
        m_speed = m_owner->getSpeed() + m_speed;
        if(m_speed < min_speed) m_speed = min_speed;
    }

    const Vec3& normal = m_owner->getNormal();
    createPhysics(y_offset, btVector3(0.0f, 0.0f, m_speed*2),
                  new btSphereShape(0.5f*m_extend.getY()*BH_VISUAL_SCALE),
                  0.4f /*restitution*/,
                  -70.0f*normal /*gravity*/,
                  true /*rotates*/);
    // Even if the ball is fired backwards, m_speed must be positive,
    // otherwise the ball can start to vibrate when energy is added.
    m_speed = fabsf(m_speed);
    // Do not adjust the up velociy depending on height above terrain, since
    // this would disable gravity.
    setAdjustUpVelocity(false);

    // should not live forever, auto-destruct after 20 seconds
    m_max_lifespan = stk_config->time2Ticks(20);
}   // onFireFlyable
