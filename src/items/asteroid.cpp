//
//  MinkowskiKart - a fun racing game with go-kart
//  Copyright (C) 2007-2015 Joerg Henrichs
//
//  Physics improvements and linear intersection algorithm by
//  Copyright (C) 2009-2015 David Mikos.
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

#include "items/asteroid.hpp"

#include "config/stk_config.hpp"
#include "config/user_config.hpp"
#include "graphics/explosion.hpp"
#include "graphics/hit_sfx.hpp"
#include "graphics/irr_driver.hpp"
#include "graphics/mesh_tools.hpp"
#ifndef SERVER_ONLY
#include "graphics/particle_emitter.hpp"
#include "graphics/particle_kind.hpp"
#include "graphics/particle_kind_manager.hpp"
#include "graphics/sp/sp_base.hpp"
#include "graphics/stk_particle.hpp"
#include <ISceneNode.h>
#endif
#include "guiengine/engine.hpp"
#include "io/file_manager.hpp"
#include "io/xml_node.hpp"
#include "karts/abstract_kart.hpp"
#include "karts/explosion_animation.hpp"
#include "modes/world.hpp"
#include "tracks/mobius_surface.hpp"
#include "utils/constants.hpp"
#include "utils/random_generator.hpp"

#include <cstdlib>
#include <vector>

// Radius (world units) within which the asteroid's landing detonation stuns
// karts. The normal per-kart explosion radius is ~6, so this gives the
// asteroid a markedly larger blast/stun footprint (see applyBigBlast).
static const float ASTEROID_BLAST_RADIUS = 12.0f;

#ifndef SERVER_ONLY
// Mesh flung from the explosion. velociraptor.spm was converted from the
// source .glb (which STK can't load) and normalised to unit size, so
// RAPTOR_SCALE reads directly as the creature's length in metres.
static const char* RAPTOR_MODEL = "velociraptor.spm";
static const float RAPTOR_SCALE = 3.0f;   // ~3 m raptor (clearly readable)
static const int   RAPTOR_COUNT = 2;
#endif

#ifndef SERVER_ONLY
namespace
{
inline float frand() { return (float)rand() / (float)RAND_MAX; }   // 0..1

/** Layered impact effect for the asteroid landing: a brief white-hot
 *  procedural flash, the warm fireball, a rising mushroom cloud (a narrow stem
 *  plus a billowing cap spawned above it), and two meshes flung from the blast
 *  that tumble through the air and vanish on ground contact. Owns its particle
 *  emitters and mesh nodes and tears them all down once everything has faded.
 *  Lifecycle modeled on graphics/explosion.cpp. Purely visual (client-side):
 *  the props have no gameplay collision, so non-deterministic randomness is
 *  fine. */
class AsteroidImpact : public HitSFX
{
private:
    /** A mesh flung from the explosion, integrated ballistically each tick. */
    struct FlyingProp
    {
        scene::ISceneNode* node;
        core::vector3df    pos;        // world position (m)
        core::vector3df    vel;        // velocity (m/s)
        core::vector3df    rot;        // current euler rotation (deg)
        core::vector3df    ang_vel;    // tumble rate (deg/s)
        float              ground_y;   // remove once it falls back to here
        float              age;        // safety cap if it never lands
    };

    std::vector<ParticleEmitter*> m_emitters;
    std::vector<FlyingProp>       m_props;
    int m_remaining_ticks;   // emission window
    int m_total_ticks;       // overall lifetime (covers the slow cap fade-out)
    int m_emission_frames;

    void add(const char* file, const Vec3& at)
    {
        ParticleKind* pk = ParticleKindManager::get()->getParticles(file);
        if (!pk)
            return;
        ParticleEmitter* e = new ParticleEmitter(pk, at, NULL);
        e->getNode()->setPreGenerating(false);
        m_emitters.push_back(e);
    }

    /** Flings RAPTOR_COUNT meshes up and out from the blast, two of them in
     *  roughly opposite directions, each spinning on all axes. */
    void spawnProps(const Vec3& coord)
    {
        scene::IMesh* mesh = irr_driver->getMesh(
            file_manager->getAsset(FileManager::MODEL, RAPTOR_MODEL));
        if (!mesh)
            return;
        for (int i = 0; i < RAPTOR_COUNT; i++)
        {
            scene::ISceneNode* node =
                irr_driver->addMesh(mesh, "asteroid_raptor");
            if (!node)
                continue;
            node->setScale(core::vector3df(RAPTOR_SCALE));

            FlyingProp p;
            p.node     = node;
            p.ground_y = coord.getY();
            p.pos      = coord.toIrrVector() + core::vector3df(0.0f, 1.2f, 0.0f);
            // Launch up and outward in opposite-ish headings so the two
            // raptors arc out of the smoke column to either side, tumbling,
            // before falling back to the ground.
            float side  = (i % 2 == 0) ? 1.0f : -1.0f;
            float yaw   = side * (1.1f + (frand() * 0.6f - 0.3f));   // radians
            float horiz = 7.0f + frand() * 2.5f;                     // m/s out
            p.vel = core::vector3df(sinf(yaw) * horiz,
                                    11.5f + frand() * 2.5f,
                                    cosf(yaw) * horiz);
            p.rot = core::vector3df(frand() * 360.0f, frand() * 360.0f,
                                    frand() * 360.0f);
            p.ang_vel = core::vector3df((frand() * 2.0f - 1.0f) * 520.0f,
                                        (frand() * 2.0f - 1.0f) * 420.0f,
                                        (frand() * 2.0f - 1.0f) * 600.0f);
            p.age = 0.0f;
            node->setPosition(p.pos);
            node->setRotation(p.rot);
            // Relativity-stationary (velocity 0): keeps the props un-warped and
            // co-located with the rest of the (un-warped) explosion, and the
            // explicit-0 override path is stable - not the noisy estimator that
            // ghosted the raptors.
            SP::setNodeRelativityVelocity(node,
                                          core::vector3df(0.0f, 0.0f, 0.0f));
            m_props.push_back(p);
        }
    }

    /** Ballistic + tumble integration; removes each prop on ground contact
     *  (or after a safety timeout if it somehow never returns to ground). */
    void updateProps(float dt)
    {
        const float GRAVITY = 12.0f;   // m/s^2 (visible, ~2 s hang time)
        for (size_t i = 0; i < m_props.size(); )
        {
            FlyingProp& p = m_props[i];
            p.age += dt;
            p.vel.Y -= GRAVITY * dt;
            p.pos   += p.vel * dt;
            p.rot   += p.ang_vel * dt;
            if (p.pos.Y <= p.ground_y || p.age > 4.0f)
            {
                SP::clearNodeRelativityVelocity(p.node);
                irr_driver->removeNode(p.node);
                m_props[i] = m_props.back();
                m_props.pop_back();   // disappear on ground contact
            }
            else
            {
                p.node->setPosition(p.pos);
                p.node->setRotation(p.rot);
                SP::setNodeRelativityVelocity(p.node,
                                              core::vector3df(0.0f, 0.0f, 0.0f));
                ++i;
            }
        }
    }

public:
    AsteroidImpact(const Vec3& coord) : HitSFX(coord, "asteriod_impact")
    {
        m_remaining_ticks = stk_config->time2Ticks(0.12f);
        m_total_ticks     = stk_config->time2Ticks(4.0f);
        m_emission_frames = 0;

        if (UserConfigParams::m_particles_effects > 1)
        {
            add("asteroid_flash.xml",         coord);
            add("explosion_asteroid.xml",     coord);
            add("asteroid_mushroom_stem.xml", coord);
            // Cap forms a few metres up so it sits atop the rising stem.
            add("asteroid_mushroom_cap.xml",  coord + Vec3(0.0f, 3.2f, 0.0f));
        }

        // Two raptors blasted out of the explosion, tumbling through the air.
        spawnProps(coord);
    }

    ~AsteroidImpact()
    {
        for (ParticleEmitter* e : m_emitters)
            delete e;
        for (FlyingProp& p : m_props)
        {
            SP::clearNodeRelativityVelocity(p.node);
            irr_driver->removeNode(p.node);
        }
    }

    virtual bool updateAndDelete(int ticks) OVERRIDE
    {
        HitSFX::updateAndDelete(ticks);
        m_emission_frames++;
        m_remaining_ticks -= ticks;
        m_total_ticks     -= ticks;

        updateProps(stk_config->ticks2Time(ticks));

        // Stop emission shortly after the burst (but keep a couple of frames
        // in case the framerate is very low), then let the already emitted
        // particles rise and fade out over the rest of the lifetime. The
        // STKParticle nodes self-animate, so no per-frame update is needed.
        if (m_remaining_ticks <= 0 && m_emission_frames > 2)
        {
            for (ParticleEmitter* e : m_emitters)
            {
                e->getNode()->getEmitter()->setMinParticlesPerSecond(0);
                e->getNode()->getEmitter()->setMaxParticlesPerSecond(0);
            }
        }
        // Stay alive until the cloud has faded AND every raptor has landed, so
        // none get popped out of the air on teardown.
        return m_total_ticks <= 0 && m_props.empty();
    }
};   // AsteroidImpact
}   // namespace
#endif

#include "utils/log.hpp" //TODO: remove after debugging is done

float Asteroid::m_st_max_distance_squared;
float Asteroid::m_gravity;

Asteroid::Asteroid (AbstractKart *kart) : Flyable(kart, PowerupManager::POWERUP_ASTEROID)
{
    m_target = NULL;
#ifndef SERVER_ONLY
    m_plasma_emitter = NULL;
    m_smoke_emitter  = NULL;
#endif
}   // Asteroid

// ----------------------------------------------------------------------------
Asteroid::~Asteroid()
{
#ifndef SERVER_ONLY
    if (m_plasma_emitter && m_plasma_emitter->getNode())
        SP::clearNodeRelativityVelocity(m_plasma_emitter->getNode());
    if (m_smoke_emitter && m_smoke_emitter->getNode())
        SP::clearNodeRelativityVelocity(m_smoke_emitter->getNode());
    delete m_plasma_emitter;
    delete m_smoke_emitter;
#endif
}   // ~Asteroid

// ----------------------------------------------------------------------------
/** Per-rendered-frame update. Drags the plasma-tail emitters along the
 *  asteroid's interpolated graphical position (getSmoothedXYZ) so each frame
 *  sheds fresh particles into world space, leaving a glowing re-entry trail
 *  behind the moving rock. Using the smoothed position (not the tick-rate
 *  physics position) keeps the tail glued to the rendered asteroid.
 */
void Asteroid::updateGraphics(float dt)
{
    Flyable::updateGraphics(dt);

#ifndef SERVER_ONLY
    // Render the whole asteroid system as relativity-stationary (velocity 0):
    // the landing explosion particles are not relativistically warped, so if
    // the mesh/trail used their real velocity they'd be pulled back to their
    // retarded position and the explosion would appear to "arrive" ahead of
    // the still-approaching mesh. Feeding 0 to the mesh AND both trail emitters
    // keeps them mutually aligned (the base class would otherwise warp the mesh
    // by the stale launch velocity while the trail uses the estimator) and
    // co-located with the explosion at the true impact point. Explicit 0 is the
    // override path, so it's stable - not the noisy estimator that ghosted.
    const irr::core::vector3df rel_v(0.0f, 0.0f, 0.0f);
    if (getNode())
        SP::setNodeRelativityVelocity(getNode(), rel_v);
    if (m_plasma_emitter)
    {
        m_plasma_emitter->setPosition(getSmoothedXYZ());
        if (m_plasma_emitter->getNode())
            SP::setNodeRelativityVelocity(m_plasma_emitter->getNode(), rel_v);
        m_plasma_emitter->update(dt);
    }
    if (m_smoke_emitter)
    {
        m_smoke_emitter->setPosition(getSmoothedXYZ());
        if (m_smoke_emitter->getNode())
            SP::setNodeRelativityVelocity(m_smoke_emitter->getNode(), rel_v);
        m_smoke_emitter->update(dt);
    }
#endif
}   // updateGraphics

// -----------------------------------------------------------------------------
/** Initialises the object from an entry in the powerup.xml file.
 *  \param node The xml node for this object.
 *  \param asteroid_model The mesh model of the asteroid.
 */
void Asteroid::init(const XMLNode &node, scene::IMesh *asteroid_model)
{
    Flyable::init(node, asteroid_model, PowerupManager::POWERUP_ASTEROID);
    float max_distance        = 80.0f;
    m_gravity                 = 9.8f;

    // Keep the original collision envelope for launch offset and collision
    // shape so asteroid visuals don't change the classic arc/seek behaviour.
    const std::string legacy_model =
        file_manager->getAsset(FileManager::MODEL, "asteroid_collision.spm");
    scene::IMesh* legacy_collision_model = irr_driver->getMesh(legacy_model);
    if (legacy_collision_model != NULL)
    {
        Vec3 min, max;
        MeshTools::minMax3D(legacy_collision_model, &min, &max);
        m_st_extend[PowerupManager::POWERUP_ASTEROID] = btVector3(max-min);
    }

    node.get("max-distance",    &max_distance  );
    m_st_max_distance_squared = max_distance*max_distance;
}   // init

// ----------------------------------------------------------------------------
/** Callback from the physics in case that a kart or physical object is hit.
 *  The asteroid triggers an explosion when hit.
 *  \param kart The kart hit (NULL if no kart was hit).
 *  \param object The object that was hit (NULL if none).
 *  \returns True if there was actually a hit (i.e. not owner, and target is
 *           not immune), false otherwise.
 */
bool Asteroid::hit(AbstractKart* kart, PhysicalObject* obj)
{
    bool was_real_hit = Flyable::hit(kart, obj);
    if(was_real_hit)
    {
        if(kart && kart->isShielded())
        {
            kart->decreaseShieldTime();
            return false; //Not sure if a shield hit is a real hit.
        }
        // Direct hit only here: handles the direct kart's full stun plus
        // scoring/achievements and track-object impulses. The oversized area
        // stun is applied separately so the asteroid's blast/stun radius is
        // much larger than the normal per-kart explosion radius.
        explode(kart, obj, /*secondary_hits*/false);
        applyBigBlast(kart);
    }

    return was_real_hit;
}   // hit

// ----------------------------------------------------------------------------
/** Asteroid-specific oversized detonation: stuns every (non-owner,
 *  non-teammate) kart within ASTEROID_BLAST_RADIUS with a full explosion
 *  animation. Passing direct_hit=true bypasses the normal per-kart explosion
 *  radius gate (~6) and applies the full, un-halved stun duration, giving the
 *  asteroid a markedly larger blast and stun footprint than other powerups.
 *  \param direct_hit The kart already handled by explode() (skipped here).
 */
void Asteroid::applyBigBlast(const AbstractKart* direct_hit)
{
    World* world = World::getWorld();
    if (!world)
        return;
    const Vec3 pos = getXYZ();
    const float r2 = ASTEROID_BLAST_RADIUS * ASTEROID_BLAST_RADIUS;
    for (unsigned int i = 0; i < world->getNumKarts(); i++)
    {
        AbstractKart* k = world->getKart(i);
        if (k == direct_hit || k == m_owner)
            continue;
        if (k->isGhostKart() || k->getKartAnimation())
            continue;
        if (world->hasTeam() && m_owner &&
            world->getKartTeam(k->getWorldKartId()) ==
            world->getKartTeam(m_owner->getWorldKartId()))
            continue;
        if (pos.distance2(k->getXYZ()) > r2)
            continue;
        ExplosionAnimation::create(k, pos, /*direct_hit*/true);
    }
}   // applyBigBlast

// ----------------------------------------------------------------------------
HitEffect* Asteroid::getHitEffect() const
{
    if (GUIEngine::isNoGraphics())
        return NULL;
    if (m_deleted_once)
        return NULL;
#ifndef SERVER_ONLY
    // Procedural flash + fireball + rising mushroom cloud on landing. Use the
    // smoothed (rendered) position, not the raw physics position, so the
    // explosion spawns exactly where the mesh is drawn rather than slightly
    // ahead of it.
    return new AsteroidImpact(getSmoothedXYZ());
#else
    return new Explosion(getXYZ(), "explosion", "explosion_asteroid.xml");
#endif
}   // getHitEffect

// ----------------------------------------------------------------------------
void Asteroid::onFireFlyable()
{
    Flyable::onFireFlyable();
    // On the Mobius track the surface "down" curves as the asteroid flies, so
    // it must follow the strip's local gravity. That adaptation lives in
    // Flyable::updateAndDelete but is gated on terrain info, so enable terrain
    // info on Mobius (otherwise the fixed launch-time gravity makes the rock
    // fly off over the strip or plough straight into it). Normal tracks keep
    // the simple ballistic arc with terrain info off.
    setDoTerrainInfo(MobiusSurface::isActive());

    btVector3 gravity_vector;
    btQuaternion q = m_owner->getTrans().getRotation();
    gravity_vector = Vec3(0, -1, 0).rotate(q.getAxis(), q.getAngle());
    gravity_vector = gravity_vector.normalize() * m_gravity;
    // A bit of a hack: the mass of this kinematic object is still 1.0
    // (see flyable), which enables collisions. I tried setting
    // collisionFilterGroup/mask, but still couldn't get this object to
    // collide with the track. By setting the mass to 1, collisions happen.
    // (if bullet is compiled with _DEBUG, a warning will be printed the first
    // time a homing-track collision happens).
    float forward_offset=m_owner->getKartLength()/2.0f + m_extend.getZ()/2.0f;

    float up_velocity = m_speed/7.0f;

    // give a speed proportional to kart speed. m_speed is defined in flyable
    m_speed *= m_owner->getSpeed() / 23.0f;

    // When going backwards, decrease speed of the asteroid by less.
    if (m_owner->getSpeed() < 0) m_speed /= 3.6f;

    m_speed += 16.0f;

    if (m_speed < 1.0f) m_speed = 1.0f;

    btTransform trans = m_owner->getTrans();

    float heading=m_owner->getHeading();
    float pitch = m_owner->getTerrainPitch(heading);

    // Find closest kart in front of the current one
    const bool  backwards = m_owner->getControls().getLookBack();
    const AbstractKart *closest_kart=NULL;
    Vec3        direction;
    float       kart_dist_squared;
    getClosestKart(&closest_kart, &kart_dist_squared, &direction,
                   m_owner /* search in front of this kart */, backwards);

    // aim at this kart if 1) it's not too far, 2) if the aimed kart's speed
    // allows the projectile to catch up with it
    //
    // this code finds the correct angle and upwards velocity to hit an opponents'
    // vehicle if they were to continue travelling in the same direction and same speed
    // (barring any obstacles in the way of course)
    if(closest_kart != NULL && kart_dist_squared < m_st_max_distance_squared &&
        m_speed>closest_kart->getSpeed())
    {
        m_target = (AbstractKart*)closest_kart;

        float fire_angle     = 0.0f;
        getLinearKartItemIntersection (m_owner->getXYZ(), closest_kart,
                                       m_speed, m_gravity, forward_offset,
                                       &fire_angle, &up_velocity);

        // apply transformation to the bullet object (without pitch)
        btQuaternion q;
        q = trans.getRotation() * btQuaternion(btVector3(0, 1, 0), fire_angle);
        trans.setRotation(q);
        m_initial_velocity = Vec3(0.0f, up_velocity, m_speed);

        createPhysics(forward_offset, m_initial_velocity,
                      new btCylinderShape(0.5f*m_extend),
                      0.5f /* restitution */, gravity_vector,
                      true /* rotation */, false /* backwards */, &trans);
    }
    else
    {
        m_target = NULL;
        // kart is too far to be hit. so throw the projectile in a generic way,
        // straight ahead, without trying to hit anything in particular
        trans = m_owner->getAlignedTransform(pitch);

        m_initial_velocity = Vec3(0.0f, up_velocity, m_speed);

        createPhysics(forward_offset, m_initial_velocity,
                      new btCylinderShape(0.5f*m_extend),
                      0.5f /* restitution */, gravity_vector,
                      true /* rotation */, backwards, &trans);
    }

    //do not adjust height according to terrain
    setAdjustUpVelocity(false);
    m_body->setActivationState(DISABLE_DEACTIVATION);
    m_body->clearForces();
    m_body->applyTorque(btVector3(5.0f, -3.0f, 7.0f));

#ifndef SERVER_ONLY
    // Light the re-entry plasma tail: a bright incandescent core and a darker
    // trailing smoke wake. Both are world-space (no parent) and repositioned
    // each rendered frame in updateGraphics, so the shed particles stay put
    // and stream out behind the flying rock.
    if (!GUIEngine::isNoGraphics())
    {
        ParticleKindManager* pkm = ParticleKindManager::get();
        ParticleKind* plasma = pkm->getParticles("asteroid_plasma.xml");
        if (plasma && !m_plasma_emitter)
            m_plasma_emitter = new ParticleEmitter(plasma, getXYZ());
        ParticleKind* smoke = pkm->getParticles("asteroid_smoke.xml");
        if (smoke && !m_smoke_emitter)
            m_smoke_emitter = new ParticleEmitter(smoke, getXYZ());
    }
#endif
}   // onFireFlyable
