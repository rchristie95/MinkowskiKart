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

#include "physics/physics.hpp"

#include "animations/three_d_animation.hpp"
#include "config/player_manager.hpp"
#include "config/player_profile.hpp"
#include "config/user_config.hpp"
#include "karts/abstract_kart.hpp"
#include "graphics/irr_driver.hpp"
#include "graphics/stars.hpp"
#include "items/flyable.hpp"
#include "karts/kart_properties.hpp"
#include "karts/rescue_animation.hpp"
#include "karts/controller/local_player_controller.hpp"
#include "modes/soccer_world.hpp"
#include "modes/world.hpp"
#include "network/network_config.hpp"
#include "karts/explosion_animation.hpp"
#include "physics/btKart.hpp"
#include "physics/irr_debug_drawer.hpp"
#include "physics/physical_object.hpp"
#include "physics/stk_dynamics_world.hpp"
#include "physics/triangle_mesh.hpp"
#include "race/race_manager.hpp"
#include "relativity/relativity_math.hpp"
#include "scriptengine/script_engine.hpp"
#include "tracks/track.hpp"
#include "tracks/track_object.hpp"
#include "utils/profiler.hpp"
#include "utils/stk_process.hpp"

#include <IVideoDriver.h>

namespace
{

btVector3 toBt(const Vec3& v)
{
    return btVector3(v.getX(), v.getY(), v.getZ());
}   // toBt

btVector3 normalizedOrDefault(const btVector3& v, const btVector3& fallback)
{
    if (v.length2() <= btScalar(1.0e-8f))
        return fallback;
    btVector3 result(v);
    result.normalize();
    return result;
}   // normalizedOrDefault

int getRelativisticSubsteps()
{
    if (!Relativity::isPreferredFrameDynamics())
        return 1;

    World* world = World::getWorld();
    if (!world)
        return 1;

    float max_beta = 0.0f;
    const float c = Relativity::getConfiguredSpeedOfLight();
    for (unsigned int i = 0; i < world->getNumKarts(); i++)
    {
        AbstractKart* kart = world->getKart(i);
        if (!kart)
            continue;

        const float speed = kart->getVelocity().length();
        max_beta = std::max(max_beta,
            (float)Relativity::betaForSpeed((double)speed, (double)c));
    }

    return Relativity::getRecommendedPhysicsSubsteps(max_beta);
}   // getRelativisticSubsteps

void configureRelativisticCCD(const AbstractKart* kart)
{
    if (!kart)
        return;

    btRigidBody* body = kart->getBody();
    if (!body || body->isStaticOrKinematicObject())
        return;

    const btScalar extent = std::max(
        btScalar(0.25f),
        btScalar(0.25f *
            std::min(kart->getKartWidth(), kart->getKartLength())));
    body->setCcdSweptSphereRadius(extent * btScalar(0.8f));
    body->setCcdMotionThreshold(extent);
}   // configureRelativisticCCD

void clampKartBodyVelocity(AbstractKart* kart)
{
    if (!kart || !Relativity::isEnabled())
        return;

    btRigidBody* body = kart->getBody();
    if (!body)
        return;

    bool was_clamped = false;
    const btVector3 clamped = Relativity::KartAdapter::clampVelocity(
        body->getLinearVelocity(), &was_clamped);
    if (!was_clamped)
        return;

    body->setLinearVelocity(clamped);
    body->setInterpolationLinearVelocity(clamped);
}   // clampKartBodyVelocity

void applyRelativisticStaticContactCorrection(AbstractKart* kart,
                                              const btPersistentManifold* manifold,
                                              bool kart_is_body_a,
                                              float dt)
{
    if (!Relativity::isPreferredFrameDynamics() || !kart || !manifold ||
        dt <= 0.0f)
    {
        return;
    }

    btRigidBody* body = kart->getBody();
    if (!body)
        return;

    btVector3 accumulated_normal(0.0f, 0.0f, 0.0f);
    btScalar min_distance = btScalar(0.0f);
    int contacts = 0;
    for (int i = 0; i < manifold->getNumContacts(); i++)
    {
        const btManifoldPoint& point = manifold->getContactPoint(i);
        if (point.getDistance() > btScalar(0.08f))
            continue;

        btVector3 normal = kart_is_body_a ? point.m_normalWorldOnB
                                          : -point.m_normalWorldOnB;
        if (normal.length2() <= btScalar(1.0e-8f))
            continue;

        accumulated_normal += normal;
        if (contacts == 0)
            min_distance = point.getDistance();
        else
            min_distance = std::min(min_distance, point.getDistance());
        contacts++;
    }

    if (contacts <= 0)
        return;

    const btVector3 contact_normal = normalizedOrDefault(
        accumulated_normal, btVector3(0.0f, 1.0f, 0.0f));
    const btVector3 velocity = body->getLinearVelocity();
    const btScalar inward_speed = -velocity.dot(contact_normal);
    const btScalar penetration = std::max(btScalar(0.0f), -min_distance);
    if (inward_speed <= btScalar(0.02f) && penetration <= btScalar(0.01f))
        return;

    const btScalar separation_speed = std::min(
        btScalar(8.0f), penetration / std::max(btScalar(dt), btScalar(1.0e-4f)));
    btVector3 corrected_velocity = velocity;
    if (inward_speed > btScalar(0.0f))
        corrected_velocity += contact_normal * inward_speed;
    if (separation_speed > btScalar(0.0f))
        corrected_velocity += contact_normal * separation_speed;

    bool was_clamped = false;
    corrected_velocity = Relativity::KartAdapter::clampVelocity(
        corrected_velocity, &was_clamped);
    (void)was_clamped;
    body->setLinearVelocity(corrected_velocity);
    body->setInterpolationLinearVelocity(corrected_velocity);
}   // applyRelativisticStaticContactCorrection

}   // anonymous namespace

//=============================================================================
Physics* g_physics[PT_COUNT];
// ----------------------------------------------------------------------------
Physics* Physics::get()
{
    ProcessType type = STKProcess::getType();
    return g_physics[type];
}   // get

// ----------------------------------------------------------------------------
void Physics::create()
{
    ProcessType type = STKProcess::getType();
    g_physics[type] = new Physics();
}   // create

// ----------------------------------------------------------------------------
void Physics::destroy()
{
    ProcessType type = STKProcess::getType();
    delete g_physics[type];
    g_physics[type] = NULL;
}   // destroy

// ----------------------------------------------------------------------------
/** Initialise physics.
 *  Create the bullet dynamics world.
 */
Physics::Physics() : btSequentialImpulseConstraintSolver()
{
    m_collision_conf      = new btDefaultCollisionConfiguration();
    m_dispatcher          = new btCollisionDispatcher(m_collision_conf);
}   // Physics

//-----------------------------------------------------------------------------
/** The actual initialisation of the physics, which is called after the track
 *  model is loaded. This allows the physics to use the actual track dimension
 *  for the axis sweep.
 */
void Physics::init(const Vec3 &world_min, const Vec3 &world_max)
{
    m_physics_loop_active = false;
    m_axis_sweep          = new btAxisSweep3(world_min, world_max);
    m_dynamics_world      = new STKDynamicsWorld(m_dispatcher,
                                                 m_axis_sweep,
                                                 this,
                                                 m_collision_conf);
    m_karts_to_delete.clear();
    m_dynamics_world->setGravity(
        btVector3(0.0f,
                  -Track::getCurrentTrack()->getGravity(),
                  0.0f));
    m_debug_drawer = new IrrDebugDrawer();
    m_dynamics_world->setDebugDrawer(m_debug_drawer);

    // Get the solver settings from the config file
    btContactSolverInfo& info = m_dynamics_world->getSolverInfo();
    info.m_numIterations = stk_config->m_solver_iterations;
    info.m_splitImpulse  = stk_config->m_solver_split_impulse;
    info.m_splitImpulsePenetrationThreshold =
        stk_config->m_solver_split_impulse_thresh;

    // Modify the mode according to the bits of the solver mode:
    info.m_solverMode = (info.m_solverMode & (~stk_config->m_solver_reset_flags))
                      | stk_config->m_solver_set_flags;
}   // init

//-----------------------------------------------------------------------------
Physics::~Physics()
{
    delete m_debug_drawer;
    delete m_dynamics_world;
    delete m_axis_sweep;
    delete m_dispatcher;
    delete m_collision_conf;
}   // ~Physics

// ----------------------------------------------------------------------------
/** Adds a kart to the physics engine.
 *  This adds the rigid body and the vehicle but only if the kart is not
 *  already in the physics world.
 *  \param kart The kart to add.
 *  \param vehicle The raycast vehicle object.
 */
void Physics::addKart(const AbstractKart *kart)
{
    const btCollisionObjectArray &all_objs =
        m_dynamics_world->getCollisionObjectArray();
    for(unsigned int i=0; i<(unsigned int)all_objs.size(); i++)
    {
        if(btRigidBody::upcast(all_objs[i])== kart->getBody())
            return;
    }
    configureRelativisticCCD(kart);
    m_dynamics_world->addRigidBody(kart->getBody());
    m_dynamics_world->addVehicle(kart->getVehicle());
}   // addKart

//-----------------------------------------------------------------------------
/** Removes a kart from the physics engine. This is used when rescuing a kart
 *  (and during cleanup).
 *  \param kart The kart to remove.
 */
void Physics::removeKart(const AbstractKart *kart)
{
    // We can't simply remove a kart from the physics world when currently
    // loops over all kart objects are active. This can happen in collision
    // handling, where a collision of a kart with a cake etc. removes
    // a kart from the physics. In this case save pointers to the kart
    // to be removed, and remove them once the physics processing is done.
    if(m_physics_loop_active)
    {
        // Make sure to remove each kart only once.
        if(std::find(m_karts_to_delete.begin(), m_karts_to_delete.end(), kart)
                     == m_karts_to_delete.end())
        {
            m_karts_to_delete.push_back(kart);
        }
    }
    else
    {
        m_dynamics_world->removeRigidBody(kart->getBody());
        m_dynamics_world->removeVehicle(kart->getVehicle());
    }
}   // removeKart

//-----------------------------------------------------------------------------
/** Updates the physics simulation and handles all collisions.
 *  \param ticks Number of physics steps to simulate.
 */
void Physics::update(int ticks)
{
    PROFILER_PUSH_CPU_MARKER("Physics", 0, 0, 0);

    m_physics_loop_active = true;
    // Bullet can report the same collision more than once (up to 4
    // contact points per collision). Additionally, more than one internal
    // substep might be taken, resulting in potentially even more
    // duplicates. To handle this, all collisions (i.e. pair of objects)
    // are stored in a vector, but only one entry per collision pair
    // of objects.
    m_all_collisions.clear();

    // Since the world update (which calls physics update) is called at the
    // fixed frequency necessary for the physics update, we need to do exactly
    // one physic step only.
    double start;
    if(UserConfigParams::m_physics_debug) start = StkTime::getRealTime();

    const float dt = stk_config->ticks2Time(1);
    const int substeps = getRelativisticSubsteps();
    m_dynamics_world->stepSimulation(dt, substeps,
                                     dt / (float)substeps);
    if (UserConfigParams::m_physics_debug)
    {
        Log::verbose("Physics", "At %d physics duration %12.8f",
                     World::getWorld()->getTicksSinceStart(),
                     StkTime::getRealTime() - start);
    }

    // Now handle the actual collision. Note: flyables can not be removed
    // inside of this loop, since the same flyables might hit more than one
    // other object. So only a flag is set in the flyables, the actual
    // clean up is then done later in the projectile manager.
    std::vector<CollisionPair>::iterator p;
    // Child process currently has no scripting engine
    bool is_child = STKProcess::getType() == PT_CHILD;
    for(p=m_all_collisions.begin(); p!=m_all_collisions.end(); ++p)
    {
        // Kart-kart collision
        // --------------------
        if(p->getUserPointer(0)->is(UserPointer::UP_KART))
        {
            KartKartCollision(p->getUserPointer(0)->getPointerKart(),
                              p->getContactPointCS(0),
                              p->getUserPointer(1)->getPointerKart(),
                              p->getContactPointCS(1)                );
            if (!is_child)
            {
                Scripting::ScriptEngine* script_engine =
                                                Scripting::ScriptEngine::getInstance();
                int kartid1 = p->getUserPointer(0)->getPointerKart()->getWorldKartId();
                int kartid2 = p->getUserPointer(1)->getPointerKart()->getWorldKartId();
                script_engine->runFunction(false, "void onKartKartCollision(int, int)",
                    [=](asIScriptContext* ctx) {
                        ctx->SetArgDWord(0, kartid1);
                        ctx->SetArgDWord(1, kartid2);
                    });
            }
            continue;
        }  // if kart-kart collision

        if(p->getUserPointer(0)->is(UserPointer::UP_PHYSICAL_OBJECT))
        {
            // Kart hits physical object
            // -------------------------
            AbstractKart *kart = p->getUserPointer(1)->getPointerKart();
            int kartId = kart->getWorldKartId();
            PhysicalObject* obj = p->getUserPointer(0)->getPointerPhysicalObject();
            std::string obj_id = obj->getID();
            std::string scripting_function = obj->getOnKartCollisionFunction();

            TrackObject* to = obj->getTrackObject();
            TrackObject* library = to->getParentLibrary();
            std::string lib_id;
            std::string* lib_id_ptr = NULL;
            if (library != NULL)
                lib_id = library->getID();
            lib_id_ptr = &lib_id;

            if (!is_child && scripting_function.size() > 0)
            {
                Scripting::ScriptEngine* script_engine = Scripting::ScriptEngine::getInstance();
                script_engine->runFunction(true, "void " + scripting_function + "(int, const string, const string)",
                    [&](asIScriptContext* ctx) {
                        ctx->SetArgDWord(0, kartId);
                        ctx->SetArgObject(1, lib_id_ptr);
                        ctx->SetArgObject(2, &obj_id);
                    });
            }
            if (obj->isCrashReset())
            {
                RescueAnimation::create(kart);
            }
            else if (obj->isExplodeKartObject())
            {
                ExplosionAnimation::create(kart);
                if (kart->getKartAnimation() != NULL)
                {
                    World::getWorld()->kartHit(kart->getWorldKartId());
                }
            }
            else if (obj->isFlattenKartObject())
            {
                const KartProperties *kp = kart->getKartProperties();
                // Count squash only once from original state
                bool was_squashed = kart->isSquashed();
                if (kart->setSquash(kp->getSwatterSquashDuration(),
                    kp->getSwatterSquashSlowdown()) && !was_squashed)
                {
                    World::getWorld()->kartHit(kart->getWorldKartId());
                }
            }
            else if(obj->isSoccerBall() &&
                    RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_SOCCER)
            {
                SoccerWorld* soccerWorld = (SoccerWorld*)World::getWorld();
                soccerWorld->setBallHitter(kartId);
            }
            continue;
        }

        if (p->getUserPointer(0)->is(UserPointer::UP_ANIMATION))
        {
            // Kart hits animation
            ThreeDAnimation *anim=p->getUserPointer(0)->getPointerAnimation();
            if(anim->isCrashReset())
            {
                AbstractKart *kart = p->getUserPointer(1)->getPointerKart();
                RescueAnimation::create(kart);
            }
            else if (anim->isExplodeKartObject())
            {
                AbstractKart *kart = p->getUserPointer(1)->getPointerKart();
                ExplosionAnimation::create(kart);
                if (kart->getKartAnimation() != NULL)
                {
                    World::getWorld()->kartHit(kart->getWorldKartId());
                }
            }
            else if (anim->isFlattenKartObject())
            {
                AbstractKart *kart = p->getUserPointer(1)->getPointerKart();
                const KartProperties *kp = kart->getKartProperties();

                // Count squash only once from original state
                bool was_squashed = kart->isSquashed();
                if (kart->setSquash(kp->getSwatterSquashDuration(),
                    kp->getSwatterSquashSlowdown()) && !was_squashed)
                {
                    World::getWorld()->kartHit(kart->getWorldKartId());
                }

            }
            continue;

        }
        // now the first object must be a projectile
        // =========================================
        if(p->getUserPointer(1)->is(UserPointer::UP_TRACK))
        {
            // Projectile hits track
            // ---------------------
            p->getUserPointer(0)->getPointerFlyable()->hitTrack();
        }
        else if(p->getUserPointer(1)->is(UserPointer::UP_PHYSICAL_OBJECT))
        {
            // Projectile hits physical object
            // -------------------------------
            Flyable* flyable = p->getUserPointer(0)->getPointerFlyable();
            PhysicalObject* obj = p->getUserPointer(1)->getPointerPhysicalObject();
            std::string obj_id = obj->getID();
            std::string scripting_function = obj->getOnItemCollisionFunction();
            if (!is_child && scripting_function.size() > 0)
            {
                Scripting::ScriptEngine* script_engine = Scripting::ScriptEngine::getInstance();
                script_engine->runFunction(true, "void " + scripting_function + "(int, int, const string)",
                        [&](asIScriptContext* ctx) {
                        ctx->SetArgDWord(0, (int)flyable->getType());
                        ctx->SetArgDWord(1, flyable->getOwnerId());
                        ctx->SetArgObject(2, &obj_id);
                    });
            }
            flyable->hit(NULL, obj);

            if (obj->isSoccerBall() &&
                RaceManager::get()->getMinorMode() == RaceManager::MINOR_MODE_SOCCER)
            {
                int kartId = p->getUserPointer(0)->getPointerFlyable()->getOwnerId();
                SoccerWorld* soccerWorld = (SoccerWorld*)World::getWorld();
                soccerWorld->setBallHitter(kartId);
            }

        }
        else if(p->getUserPointer(1)->is(UserPointer::UP_KART))
        {
            // Projectile hits kart
            // --------------------
            // Only explode a bowling ball if the target is
            // not invulnerable
            AbstractKart* target_kart = p->getUserPointer(1)->getPointerKart();
            PowerupManager::PowerupType type = p->getUserPointer(0)->getPointerFlyable()->getType();
            if(type != PowerupManager::POWERUP_BOWLING || !target_kart->isInvulnerable())
            {
                Flyable *f = p->getUserPointer(0)->getPointerFlyable();
                f->hit(target_kart);

                // Check for achievements
                AbstractKart * kart = World::getWorld()->getKart(f->getOwnerId());
                LocalPlayerController *lpc =
                    dynamic_cast<LocalPlayerController*>(kart->getController());

                // Check that it's not a kart hitting itself (this can
                // happen at the time a flyable is shot - release too close
                // to the kart, and it's the current player. At this stage
                // only the current player can get achievements.
                if (target_kart != kart && lpc && lpc->canGetAchievements())
                {
                    if (type == PowerupManager::POWERUP_BOWLING)
                    {
                        PlayerManager::increaseAchievement(AchievementsStatus::BOWLING_HIT, 1);
                        if (RaceManager::get()->isLinearRaceMode())
                            PlayerManager::increaseAchievement(AchievementsStatus::BOWLING_HIT_1RACE, 1);
                    }   // is bowling ball
                }   // if target_kart != kart && is a player kart and is current player
            }

        }
        else
        {
            // Projectile hits projectile
            // --------------------------
            p->getUserPointer(0)->getPointerFlyable()->hit(NULL);
            p->getUserPointer(1)->getPointerFlyable()->hit(NULL);
        }
    }  // for all p in m_all_collisions

    if (Relativity::isEnabled())
    {
        World* world = World::getWorld();
        if (world)
        {
            for (unsigned int i = 0; i < world->getNumKarts(); i++)
                clampKartBodyVelocity(world->getKart(i));
        }
    }

    m_physics_loop_active = false;
    // Now remove the karts that were removed while the above loop
    // was active. Now we can safely call removeKart, since the loop
    // is finished and m_physics_world_active is not set anymore.
    for(unsigned int i=0; i<m_karts_to_delete.size(); i++)
        removeKart(m_karts_to_delete[i]);
    m_karts_to_delete.clear();

    PROFILER_POP_CPU_MARKER();
}   // update

//-----------------------------------------------------------------------------
/** Handles the special case of two karts colliding with each other, which
 *  means that bombs must be passed on. If both karts have a bomb, they'll
 *  explode immediately. This function is called from physics::update() on the
 *  server and if no networking is used, and from race_state on the client to
 *  replay what happened on the server.
 *  \param kart_a First kart involved in the collision.
 *  \param contact_point_a Location of collision at first kart (in kart
 *         coordinates).
 *  \param kart_b Second kart involved in the collision.
 *  \param contact_point_b Location of collision at second kart (in kart
 *         coordinates).
 */
void Physics::KartKartCollision(AbstractKart *kart_a,
                                const Vec3 &contact_point_a,
                                AbstractKart *kart_b,
                                const Vec3 &contact_point_b)
{
    // Only one kart needs to handle the attachments, it will
    // fix the attachments for the other kart.
    kart_a->crashed(kart_b, /*handle_attachments*/true);
    kart_b->crashed(kart_a, /*handle_attachments*/false);

    if (!Relativity::isPreferredFrameDynamics())
    {
        AbstractKart *left_kart, *right_kart;

        if(contact_point_a.getX() < contact_point_b.getX())
        {
            left_kart  = kart_b;
            right_kart = kart_a;
        }
        else
        {
            left_kart  = kart_a;
            right_kart = kart_b;
        }

        float f_right =  right_kart->getKartProperties()->getMass() > 0
                         ? left_kart->getKartProperties()->getMass()
                           / right_kart->getKartProperties()->getMass()
                         : 1.5f;
        f_right *= right_kart->getSpeed() > 0
                   ? left_kart->getSpeed()
                      / right_kart->getSpeed()
                   : 1.5f;
        if(f_right > 1.25f)
            f_right = 1.25f;
        else if(f_right< 0.8f)
            f_right = 0.8f;
        float f_left = 1/f_right;

        float vel_left  = left_kart->getVelocityLC().getX();
        float vel_right = right_kart->getVelocityLC().getX();
        float vel_diff = vel_right + vel_left;

        if(vel_diff<0)
        {
            if(fabsf(vel_left)>2.0f)
                f_left *= 1.0f - vel_diff/fabsf(vel_left);
            if(f_left > 2.0f)
                f_left = 2.0f;
        }
        else
        {
            if(fabsf(vel_right)>2.0f)
                f_right *= 1.0f + vel_diff/fabsf(vel_right);
            if(f_right > 2.0f)
                f_right = 2.0f;
        }

        f_left  = f_left  * f_left;
        f_right = f_right * f_right;

        if(right_kart->getVehicle()->getCentralImpulseTicks()<=0)
        {
            const KartProperties *kp = left_kart->getKartProperties();
            Vec3 impulse(kp->getCollisionImpulse()*f_right, 0, 0);
            impulse = right_kart->getTrans().getBasis() * impulse;
            right_kart->getVehicle()
                ->setTimedCentralImpulse(
                (uint16_t)stk_config->time2Ticks(kp->getCollisionImpulseTime()),
                impulse);
            right_kart ->getBody()->setAngularVelocity(btVector3(0,0,0));
        }

        if(left_kart->getVehicle()->getCentralImpulseTicks()<=0)
        {
            const KartProperties *kp = right_kart->getKartProperties();
            Vec3 impulse = Vec3(-kp->getCollisionImpulse()*f_left, 0, 0);
            impulse = left_kart->getTrans().getBasis() * impulse;
            left_kart->getVehicle()
                ->setTimedCentralImpulse(
                (uint16_t)stk_config->time2Ticks(kp->getCollisionImpulseTime()),
                impulse);
            left_kart->getBody()->setAngularVelocity(btVector3(0,0,0));
        }
        return;
    }

    if (kart_a->getVehicle()->getCentralImpulseTicks() > 0 ||
        kart_b->getVehicle()->getCentralImpulseTicks() > 0)
    {
        return;
    }

    const btVector3 world_point_a =
        kart_a->getBody()->getWorldTransform()(toBt(contact_point_a));
    const btVector3 world_point_b =
        kart_b->getBody()->getWorldTransform()(toBt(contact_point_b));
    btVector3 collision_normal = world_point_b - world_point_a;
    if (collision_normal.length2() <= btScalar(1.0e-6f))
    {
        collision_normal =
            kart_b->getBody()->getCenterOfMassPosition() -
            kart_a->getBody()->getCenterOfMassPosition();
    }
    collision_normal = normalizedOrDefault(
        collision_normal, btVector3(1.0f, 0.0f, 0.0f));

    const float c = Relativity::getConfiguredSpeedOfLight();
    const float mass_a = std::max(1.0f, kart_a->getKartProperties()->getMass());
    const float mass_b = std::max(1.0f, kart_b->getKartProperties()->getMass());
    float impulse_magnitude = Relativity::computeCollisionImpulseMagnitude(
        collision_normal, kart_a->getBody()->getLinearVelocity(), mass_a,
        kart_b->getBody()->getLinearVelocity(), mass_b, 0.05f, c);
    if (impulse_magnitude <= 0.0f)
        return;

    const float effective_mass_a = Relativity::getDirectionalEffectiveMass(
        mass_a, kart_a->getBody()->getLinearVelocity(), collision_normal, c);
    const float effective_mass_b = Relativity::getDirectionalEffectiveMass(
        mass_b, kart_b->getBody()->getLinearVelocity(), collision_normal, c);
    const float beta_a = (float)Relativity::betaForSpeed(
        (double)kart_a->getBody()->getLinearVelocity().length(), (double)c);
    const float beta_b = (float)Relativity::betaForSpeed(
        (double)kart_b->getBody()->getLinearVelocity().length(), (double)c);
    const float delta_v_limit = 3.0f + 6.0f * std::max(beta_a, beta_b);
    const float impulse_cap =
        delta_v_limit * std::min(effective_mass_a, effective_mass_b);
    if (impulse_cap > 0.0f)
        impulse_magnitude = std::min(impulse_magnitude, impulse_cap);

    const float collision_time = std::max(
        0.05f,
        0.5f * (kart_a->getKartProperties()->getCollisionImpulseTime() +
                kart_b->getKartProperties()->getCollisionImpulseTime()));
    const uint16_t collision_ticks =
        (uint16_t)std::max(1, stk_config->time2Ticks(collision_time));
    const btVector3 distributed_impulse =
        collision_normal * (impulse_magnitude / collision_time);

    kart_a->getVehicle()->setTimedCentralImpulse(collision_ticks,
                                                 -distributed_impulse);
    kart_b->getVehicle()->setTimedCentralImpulse(collision_ticks,
                                                 distributed_impulse);
    kart_a->getBody()->setAngularVelocity(
        kart_a->getBody()->getAngularVelocity() * 0.8f);
    kart_b->getBody()->setAngularVelocity(
        kart_b->getBody()->getAngularVelocity() * 0.8f);

}   // KartKartCollision

//-----------------------------------------------------------------------------
/** This function is called at each internal bullet timestep. It is used
 *  here to do the collision handling: using the contact manifolds after a
 *  physics time step might miss some collisions (when more than one internal
 *  time step was done, and the collision is added and removed). So this
 *  function stores all collisions in a list, which is then handled after the
 *  actual physics timestep. This list only stores a collision if it's not
 *  already in the list, so a collisions which is reported more than once is
 *  nevertheless only handled once.
 *  The list of collision
 *  Parameters: see bullet documentation for details.
 */
btScalar Physics::solveGroup(btCollisionObject** bodies, int numBodies,
                             btPersistentManifold** manifold,int numManifolds,
                             btTypedConstraint** constraints,
                             int numConstraints,
                             const btContactSolverInfo& info,
                             btIDebugDraw* debugDrawer,
                             btStackAlloc* stackAlloc,
                             btDispatcher* dispatcher)
{
    btScalar returnValue=
        btSequentialImpulseConstraintSolver::solveGroup(bodies, numBodies,
                                                        manifold, numManifolds,
                                                        constraints,
                                                        numConstraints, info,
                                                        debugDrawer,
                                                        stackAlloc,
                                                        dispatcher);
    int currentNumManifolds = m_dispatcher->getNumManifolds();
    // We can't explode a rocket in a loop, since a rocket might collide with
    // more than one object, and/or more than once with each object (if there
    // is more than one collision point). So keep a list of rockets that will
    // be exploded after the collisions
    for(int i=0; i<currentNumManifolds; i++)
    {
        btPersistentManifold* contact_manifold =
            m_dynamics_world->getDispatcher()->getManifoldByIndexInternal(i);

        const btCollisionObject* objA =
            static_cast<const btCollisionObject*>(contact_manifold->getBody0());
        const btCollisionObject* objB =
            static_cast<const btCollisionObject*>(contact_manifold->getBody1());

        unsigned int num_contacts = contact_manifold->getNumContacts();
        if(!num_contacts) continue;   // no real collision

        const UserPointer *upA = (UserPointer*)(objA->getUserPointer());
        const UserPointer *upB = (UserPointer*)(objB->getUserPointer());

        if(!upA || !upB) continue;

        // 1) object A is a track
        // =======================
        if(upA->is(UserPointer::UP_TRACK))
        {
            if(upB->is(UserPointer::UP_FLYABLE))   // 1.1 projectile hits track
                m_all_collisions.push_back(
                    upB, contact_manifold->getContactPoint(0).m_localPointB,
                    upA, contact_manifold->getContactPoint(0).m_localPointA);
            else if(upB->is(UserPointer::UP_KART))
            {
                AbstractKart *kart=upB->getPointerKart();
                applyRelativisticStaticContactCorrection(
                    kart, contact_manifold, false, info.m_timeStep);
                int n = contact_manifold->getContactPoint(0).m_index0;
                const Material *m
                    = n>=0 ? upA->getPointerTriangleMesh()->getMaterial(n)
                           : NULL;
                // I assume that the normal needs to be flipped in this case,
                // but  I can't verify this since it appears that bullet
                // always has the kart as object A, not B.
                const btVector3 &normal = -contact_manifold->getContactPoint(0)
                                                            .m_normalWorldOnB;
                kart->crashed(m, normal);
            }
            else if(upB->is(UserPointer::UP_PHYSICAL_OBJECT))
            {
                std::vector<int> used;
                for(int i=0; i< contact_manifold->getNumContacts(); i++)
                {
                    int n = contact_manifold->getContactPoint(i).m_index0;
                    // Make sure to call the callback function only once
                    // per triangle.
                    if(std::find(used.begin(), used.end(), n)!=used.end())
                        continue;
                    used.push_back(n);
                    const Material *m
                        = n >= 0 ? upB->getPointerTriangleMesh()->getMaterial(n)
                        : NULL;
                    const btVector3 &normal = contact_manifold->getContactPoint(i)
                        .m_normalWorldOnB;
                    upA->getPointerPhysicalObject()->hit(m, normal);
                }   // for i in getNumContacts()
            }   // upB is physical object
        }   // upA is track
        // 2) object a is a kart
        // =====================
        else if(upA->is(UserPointer::UP_KART))
        {
            if(upB->is(UserPointer::UP_TRACK))
            {
                AbstractKart *kart = upA->getPointerKart();
                applyRelativisticStaticContactCorrection(
                    kart, contact_manifold, true, info.m_timeStep);
                int n = contact_manifold->getContactPoint(0).m_index1;
                const Material *m
                    = n>=0 ? upB->getPointerTriangleMesh()->getMaterial(n)
                           : NULL;
                const btVector3 &normal = contact_manifold->getContactPoint(0)
                                                           .m_normalWorldOnB;
                kart->crashed(m, normal);   // Kart hit track
            }
            else if(upB->is(UserPointer::UP_FLYABLE))
                // 2.1 projectile hits kart
                m_all_collisions.push_back(
                    upB, contact_manifold->getContactPoint(0).m_localPointB,
                    upA, contact_manifold->getContactPoint(0).m_localPointA);
            else if(upB->is(UserPointer::UP_KART))
                // 2.2 kart hits kart
                m_all_collisions.push_back(
                    upA, contact_manifold->getContactPoint(0).m_localPointA,
                    upB, contact_manifold->getContactPoint(0).m_localPointB);
            else if(upB->is(UserPointer::UP_PHYSICAL_OBJECT))
            {
                // 2.3 kart hits physical object
                m_all_collisions.push_back(
                    upB, contact_manifold->getContactPoint(0).m_localPointB,
                    upA, contact_manifold->getContactPoint(0).m_localPointA);
                // If the object is a statical object (e.g. a door in
                // overworld) add a push back to avoid that karts get stuck
                if (objB->isStaticObject())
                {
                    AbstractKart *kart = upA->getPointerKart();
                    applyRelativisticStaticContactCorrection(
                        kart, contact_manifold, true, info.m_timeStep);
                    const btVector3 &normal = contact_manifold->getContactPoint(0)
                        .m_normalWorldOnB;
                    kart->crashed((Material*)NULL, normal);
                }   // isStatiObject
            }
            else if(upB->is(UserPointer::UP_ANIMATION))
                m_all_collisions.push_back(
                    upB, contact_manifold->getContactPoint(0).m_localPointB,
                    upA, contact_manifold->getContactPoint(0).m_localPointA);
        }
        // 3) object is a projectile
        // =========================
        else if(upA->is(UserPointer::UP_FLYABLE))
        {
            // 3.1) projectile hits track
            // 3.2) projectile hits projectile
            // 3.3) projectile hits physical object
            // 3.4) projectile hits kart
            if(upB->is(UserPointer::UP_TRACK          ) ||
               upB->is(UserPointer::UP_FLYABLE        ) ||
               upB->is(UserPointer::UP_PHYSICAL_OBJECT) ||
               upB->is(UserPointer::UP_KART           )   )
            {
                m_all_collisions.push_back(
                    upA, contact_manifold->getContactPoint(0).m_localPointA,
                    upB, contact_manifold->getContactPoint(0).m_localPointB);
            }
        }
        // Object is a physical object
        // ===========================
        else if(upA->is(UserPointer::UP_PHYSICAL_OBJECT))
        {
            if(upB->is(UserPointer::UP_FLYABLE))
                m_all_collisions.push_back(
                    upB, contact_manifold->getContactPoint(0).m_localPointB,
                    upA, contact_manifold->getContactPoint(0).m_localPointA);
            else if(upB->is(UserPointer::UP_KART))
            {
                m_all_collisions.push_back(
                    upA, contact_manifold->getContactPoint(0).m_localPointA,
                    upB, contact_manifold->getContactPoint(0).m_localPointB);
                if (objA->isStaticObject())
                {
                    applyRelativisticStaticContactCorrection(
                        upB->getPointerKart(), contact_manifold, false,
                        info.m_timeStep);
                }
            }
            else if(upB->is(UserPointer::UP_TRACK))
            {
                std::vector<int> used;
                for(int i=0; i< contact_manifold->getNumContacts(); i++)
                {
                    int n = contact_manifold->getContactPoint(i).m_index1;
                    // Make sure to call the callback function only once
                    // per triangle.
                    if(std::find(used.begin(), used.end(), n)!=used.end())
                        continue;
                    used.push_back(n);
                    const Material *m
                        = n >= 0 ? upB->getPointerTriangleMesh()->getMaterial(n)
                        : NULL;
                    const btVector3 &normal = contact_manifold->getContactPoint(i)
                                             .m_normalWorldOnB;
                    upA->getPointerPhysicalObject()->hit(m, normal);
                }   // for i in getNumContacts()
            }   // upB is track
        }   // upA is physical object
        else if (upA->is(UserPointer::UP_ANIMATION))
        {
            if(upB->is(UserPointer::UP_KART))
                m_all_collisions.push_back(
                    upA, contact_manifold->getContactPoint(0).m_localPointA,
                    upB, contact_manifold->getContactPoint(0).m_localPointB);
        }
        else
            assert("Unknown user pointer");           // 4) Should never happen
    }   // for i<numManifolds

    return returnValue;
}   // solveGroup

// ----------------------------------------------------------------------------
/** A debug draw function to show the track and all karts.
 */
void Physics::draw()
{
    if(!m_debug_drawer->debugEnabled() ||
        !World::getWorld()->isRacePhase()) return;

    video::SColor color(77,179,0,0);
    video::SMaterial material;
    material.Thickness = 2;
    material.AmbientColor = color;
    material.DiffuseColor = color;
    material.EmissiveColor= color;
    material.BackfaceCulling = false;
    material.setFlag(video::EMF_LIGHTING, false);
    irr_driver->getVideoDriver()->setMaterial(material);
    irr_driver->getVideoDriver()->setTransform(video::ETS_WORLD,
                                               core::IdentityMatrix);
    m_dynamics_world->debugDrawWorld();
    return;
}   // draw

// ----------------------------------------------------------------------------

/* EOF */
