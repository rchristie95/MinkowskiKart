//
//  Minkowski Kart - Swatter powerup
//

#ifndef HEADER_ANTI_KARTICLE_HPP
#define HEADER_ANTI_KARTICLE_HPP

#include "config/stk_config.hpp"
#include "graphics/irr_driver.hpp"
#include "graphics/relativistic_vfx.hpp"
#include "guiengine/engine.hpp"
#include "items/flyable.hpp"
#include "io/xml_node.hpp"
#include "karts/abstract_kart.hpp"
#include "karts/controller/kart_control.hpp"
#include "karts/kart_model.hpp"
#include "karts/kart_properties.hpp"
#include "modes/world.hpp"
#include "physics/physical_object.hpp"

#include <ge_render_info.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

/**
 * Projectile-like mirrored kart clone spawned by the anti-karticle powerup.
 */
class AntiKarticle : public Flyable
{
private:
    float m_visual_distance;
    float m_last_speed;
    float m_last_steer;

#ifndef SERVER_ONLY
    std::unique_ptr<KartModel> m_clone_model;
#endif

    void updateMirroredMotion(int ticks);
    void createCloneVisual();
    static float& lifetime()
    {
        static float value = 10.0f;
        return value;
    }

public:
    AntiKarticle(AbstractKart *kart);
    virtual ~AntiKarticle();

    static void init(const XMLNode &node, scene::IMesh *model);

    virtual void onFireFlyable() OVERRIDE;
    virtual bool updateAndDelete(int ticks) OVERRIDE;
    virtual void updateGraphics(float dt) OVERRIDE;
    virtual bool hit(AbstractKart *kart, PhysicalObject *obj = NULL) OVERRIDE;
};

// ----------------------------------------------------------------------------
inline AntiKarticle::AntiKarticle(AbstractKart *kart)
        : Flyable(kart, PowerupManager::POWERUP_ANTI_KARTICLE, 50.0f),
          m_visual_distance(0.0f),
          m_last_speed(0.0f),
          m_last_steer(0.0f)
{
    createCloneVisual();
}   // AntiKarticle

// ----------------------------------------------------------------------------
inline AntiKarticle::~AntiKarticle()
{
#ifndef SERVER_ONLY
    m_clone_model.reset();
#endif
}   // ~AntiKarticle

// ----------------------------------------------------------------------------
inline void AntiKarticle::init(const XMLNode &node, scene::IMesh *model)
{
    Flyable::init(node, model, PowerupManager::POWERUP_ANTI_KARTICLE);
    float configured_lifetime = 10.0f;
    node.get("lifetime", &configured_lifetime);
    lifetime() = configured_lifetime;
}   // init

// ----------------------------------------------------------------------------
inline void AntiKarticle::createCloneVisual()
{
#ifndef SERVER_ONLY
    if (GUIEngine::isNoGraphics())
        return;

    scene::ISceneNode *placeholder = getNode();
    if (placeholder)
        irr_driver->removeNode(placeholder);
    setNode(NULL);

    if (!m_owner || !m_owner->getKartProperties())
        return;

    std::shared_ptr<GE::GERenderInfo> render_info =
        std::make_shared<GE::GERenderInfo>(0.0f, true);
    render_info->setInvertColor(true);

    m_clone_model.reset(
        m_owner->getKartProperties()->getKartModelCopy(render_info));
    if (!m_clone_model)
        return;

    m_clone_model->setKart(m_owner);
    scene::ISceneNode *clone_node =
        m_clone_model->attachModel(true/*animated*/, false/*human_player*/);
    if (!clone_node)
    {
        m_clone_model.reset();
        return;
    }
    m_clone_model->setDefaultSuspension();
    setNode(clone_node);
#endif
}   // createCloneVisual

// ----------------------------------------------------------------------------
inline void AntiKarticle::onFireFlyable()
{
    Flyable::onFireFlyable();

    m_visual_distance = 0.0f;
    m_last_speed = std::fabs(m_owner->getSpeed());
    m_last_steer = -m_owner->getControls().getSteer();

    m_extend = Vec3(m_owner->getKartWidth(), m_owner->getKartHeight(),
                    m_owner->getKartLength());
    m_max_height = std::max(1.0f, m_owner->getKartHeight() * 0.55f);
    m_min_height = 0.05f;
    m_average_height = m_owner->getKartHeight() * 0.35f;

    const float half_width = std::max(0.35f, m_owner->getKartWidth() * 0.45f);
    const float half_height = std::max(0.25f, m_owner->getKartHeight() * 0.35f);
    const float half_length = std::max(0.45f, m_owner->getKartLength() * 0.45f);
    const float spawn_offset = m_owner->getKartLength() * 0.5f + half_length;

    createPhysics(spawn_offset, btVector3(0.0f, 0.0f, m_last_speed),
                  new btBoxShape(btVector3(half_width, half_height, half_length)),
                  0.4f /*restitution*/,
                  -70.0f * m_owner->getNormal() /*gravity*/,
                  false /*rotates*/,
                  true /*turn around*/);

    setAdjustUpVelocity(false);
    m_body->setActivationState(DISABLE_DEACTIVATION);
    m_body->setAngularFactor(0.0f);
    m_body->clearForces();

    m_max_lifespan = stk_config->time2Ticks(lifetime());

    if (relativistic_vfx_manager)
    {
        const btVector3 forward =
            m_owner->getTrans().getBasis().getColumn(2).normalized();
        const uint32_t seed =
            (uint32_t)(World::getWorld()->getTicksSinceStart() * 1103515245u) ^
            (uint32_t)(m_owner->getWorldKartId() * 2654435761u);
        relativistic_vfx_manager->triggerPairProduction(
            getXYZ(), forward, m_owner->getNormal(), seed);
    }
}   // onFireFlyable

// ----------------------------------------------------------------------------
inline void AntiKarticle::updateMirroredMotion(int ticks)
{
    if (!m_body || !m_owner || m_owner->isEliminated())
        return;

    const float dt = stk_config->ticks2Time(ticks);
    m_last_speed = std::fabs(m_owner->getSpeed());
    m_last_steer = -m_owner->getControls().getSteer();

    btTransform trans = getTrans();
    Vec3 forward = trans.getBasis().getColumn(2);
    if (forward.length2() < 0.0001f)
        forward = -m_owner->getTrans().getBasis().getColumn(2);
    forward.normalize();

    Vec3 up = TerrainInfo::getNormal();
    if (up.length2() < 0.0001f)
        up = m_owner->getNormal();
    up.normalize();

    const float wheel_base = std::max(1.0f, m_owner->getKartLength());
    const float max_steer = m_owner->getMaxSteerAngle();
    const float yaw_delta =
        (m_last_speed / wheel_base) * std::tan(m_last_steer * max_steer) * dt;
    if (std::isfinite((double)yaw_delta) && std::fabs(yaw_delta) > 0.00001f)
        forward = forward.rotate(up, yaw_delta);
    forward.normalize();

    Vec3 right = up.cross(forward);
    if (right.length2() < 0.0001f)
        right = btVector3(1.0f, 0.0f, 0.0f);
    right.normalize();
    up = forward.cross(right);
    up.normalize();

    btMatrix3x3 basis(right.getX(), up.getX(), forward.getX(),
                      right.getY(), up.getY(), forward.getY(),
                      right.getZ(), up.getZ(), forward.getZ());
    trans.setBasis(basis);

    m_body->setCenterOfMassTransform(trans);
    m_motion_state->setWorldTransform(trans);
    setTrans(trans);
    setVelocity(forward * m_last_speed);
    m_body->activate(true);
}   // updateMirroredMotion

// ----------------------------------------------------------------------------
inline bool AntiKarticle::updateAndDelete(int ticks)
{
    const bool can_be_deleted = Flyable::updateAndDelete(ticks);
    if (can_be_deleted)
        return true;

    updateMirroredMotion(ticks);
    return false;
}   // updateAndDelete

// ----------------------------------------------------------------------------
inline void AntiKarticle::updateGraphics(float dt)
{
    Flyable::updateGraphics(dt);

#ifndef SERVER_ONLY
    if (m_clone_model)
    {
        const float distance = m_last_speed * dt;
        m_visual_distance += distance;
        m_clone_model->update(dt, -distance, m_last_steer, -m_last_speed,
                              0.0f);
    }
#endif
}   // updateGraphics

// ----------------------------------------------------------------------------
inline bool AntiKarticle::hit(AbstractKart *kart, PhysicalObject *obj)
{
    if (kart && !isOwnerImmunity(kart) && m_has_server_state &&
        !hasAnimation())
    {
        if (kart->isShielded())
        {
            kart->decreaseShieldTime();
            return true;
        }
        if (!kart->getKartAnimation())
            explode(kart, obj, false/*secondary_hits*/);
        return true;
    }

    if (!kart && !obj && m_max_lifespan > -1 &&
        (int)m_ticks_since_thrown > m_max_lifespan)
    {
        return Flyable::hit(NULL, NULL);
    }

    return false;
}   // hit

#endif
