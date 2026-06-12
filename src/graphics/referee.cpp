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

#include "graphics/referee.hpp"
#include "config/stk_config.hpp"
#include "graphics/central_settings.hpp"
#include "graphics/irr_driver.hpp"
#include "graphics/light.hpp"
#include "graphics/material.hpp"
#include "graphics/mesh_tools.hpp"
#include "graphics/sp/sp_base.hpp"
#include "graphics/sp/sp_mesh_buffer.hpp"
#include "graphics/sp/sp_mesh_node.hpp"
#include "karts/abstract_kart.hpp"
#include "io/file_manager.hpp"
#include "io/xml_node.hpp"
#include "modes/world.hpp"
#include "utils/constants.hpp"
#include "utils/log.hpp"
#include "utils/string_utils.hpp"

#include <ISceneManager.h>
#include <ILightSceneNode.h>
#include <ITexture.h>
#include <IVideoDriver.h>

int                   Referee::m_st_first_start_frame  = 1;
int                   Referee::m_st_last_start_frame   = 1;
int                   Referee::m_st_first_rescue_frame = 1;
int                   Referee::m_st_last_rescue_frame  = 1;
int                   Referee::m_st_traffic_buffer     = -1;
Vec3                  Referee::m_st_start_offset       = Vec3(-2, 2, 2);
Vec3                  Referee::m_st_start_rotation     = Vec3(0, 180, 0);
Vec3                  Referee::m_st_start_scale        = Vec3(1, 1, 1);
Vec3                  Referee::m_st_start_light_offset = Vec3(0, 1.1f, -0.42f);
Vec3                  Referee::m_st_start_light_rotation = Vec3(0, 0, 0);
Vec3                  Referee::m_st_start_light_scale  = Vec3(1, 1, 1);
Vec3                  Referee::m_st_rescue_offset      = Vec3(0, 0.6f, 0);
Vec3                  Referee::m_st_rescue_rotation    = Vec3(0, 0, 0);
Vec3                  Referee::m_st_rescue_scale       = Vec3(1, 1, 1);
Vec3                  Referee::m_st_rescue_rotor_offset = Vec3(0, 1.1124f, 0.1773f);
Vec3                  Referee::m_st_rescue_rotor_rotation = Vec3(0, 0, 0);
Vec3                  Referee::m_st_rescue_rotor_scale = Vec3(1, 1, 1);
float                 Referee::m_height                = 0.0f;
scene::IAnimatedMesh *Referee::m_st_start_mesh         = NULL;
scene::IAnimatedMesh *Referee::m_st_start_light_mesh   = NULL;
scene::IAnimatedMesh *Referee::m_st_rescue_mesh        = NULL;
scene::IAnimatedMesh *Referee::m_st_rescue_rotor_mesh  = NULL;

namespace
{
scene::IAnimatedMesh* loadRefereeMesh(const std::string& model_filename,
                                      const std::string& fallback_filename)
{
    const std::string filename = model_filename.empty()
        ? fallback_filename : model_filename;
    if (filename.empty())
        return NULL;

    scene::IAnimatedMesh* mesh = irr_driver->getAnimatedMesh(
        file_manager->getAsset(FileManager::MODEL, filename));
    if (!mesh)
    {
        Log::fatal("referee", "Can't find referee model '%s', aborting.",
                   filename.c_str());
    }
    return mesh;
}   // loadRefereeMesh

int findTrafficBuffer(scene::IAnimatedMesh* mesh)
{
    if (!mesh)
        return -1;

    for (unsigned int i = 0; i < mesh->getMeshBufferCount(); i++)
    {
        scene::IMeshBuffer *mb = mesh->getMeshBuffer(i);
        SP::SPMeshBuffer* spmb = dynamic_cast<SP::SPMeshBuffer*>(mb);
        if (spmb)
        {
            auto ret = spmb->getAllSTKMaterials();
            for (unsigned j = 0; j < ret.size(); j++)
            {
                std::string name =
                    StringUtils::getBasename(ret[j]->getSamplerPath(0));
                if (name == "traffic_light.png")
                {
                    spmb->enableTextureMatrix(j);
                    return (int)i;
                }
            }
            continue;
        }

        video::SMaterial &irrMaterial = mb->getMaterial();
        video::ITexture* t = irrMaterial.getTexture(0);
        if (!t) continue;

        std::string name = StringUtils::getBasename(t->getName()
            .getInternalName().c_str());
        if (name == "traffic_light.png")
            return (int)i;

        irrMaterial.MaterialType = video::EMT_TRANSPARENT_ALPHA_CHANNEL_REF;
    }
    return -1;
}   // findTrafficBuffer

void removeMeshOnce(scene::IAnimatedMesh*& mesh,
                    scene::IAnimatedMesh* already_removed_a,
                    scene::IAnimatedMesh* already_removed_b,
                    scene::IAnimatedMesh* already_removed_c)
{
    if (!mesh || mesh == already_removed_a || mesh == already_removed_b ||
        mesh == already_removed_c)
    {
        mesh = NULL;
        return;
    }
    irr_driver->removeMeshFromCache(mesh);
    mesh = NULL;
}   // removeMeshOnce
}

// ----------------------------------------------------------------------------
/** Loads the static mesh.
 */
void Referee::init()
{
    assert(!m_st_start_mesh);
    const std::string filename=file_manager->getAssetChecked(FileManager::MODEL,
                                                             "referee.xml", true);
    XMLNode *node = file_manager->createXMLTree(filename);
    if(!node)
    {
        Log::fatal("referee", "Can't read XML file referee.xml, aborting.");
    }
    if(node->getName()!="referee")
    {
        Log::fatal("referee", "The file referee.xml does not contain a referee"
               "node, aborting.");
    }
    std::string model_filename;
    std::string start_model_filename;
    std::string start_light_model_filename;
    std::string rescue_model_filename;
    std::string rescue_rotor_model_filename;
    node->get("model", &model_filename);
    node->get("start-model", &start_model_filename);
    node->get("start-light-model", &start_light_model_filename);
    node->get("rescue-model", &rescue_model_filename);
    node->get("rescue-rotor-model", &rescue_rotor_model_filename);

    m_st_start_mesh = loadRefereeMesh(start_model_filename, model_filename);
    m_st_rescue_mesh = loadRefereeMesh(rescue_model_filename, model_filename);
    m_st_start_light_mesh = loadRefereeMesh(start_light_model_filename, "");
    m_st_rescue_rotor_mesh = loadRefereeMesh(rescue_rotor_model_filename, "");

    node->get("first-rescue-frame", &m_st_first_rescue_frame);
    node->get("last-rescue-frame",  &m_st_last_rescue_frame );
    node->get("first-start-frame",  &m_st_first_start_frame );
    node->get("last-start-frame",   &m_st_last_start_frame  );
    node->get("start-offset",       &m_st_start_offset      );
    node->get("scale",              &m_st_start_scale       );
    node->get("start-scale",        &m_st_start_scale       );
    node->get("start-rotation",     &m_st_start_rotation    );
    node->get("start-light-offset", &m_st_start_light_offset);
    node->get("start-light-rotation", &m_st_start_light_rotation);
    node->get("start-light-scale",  &m_st_start_light_scale );
    node->get("rescue-offset",      &m_st_rescue_offset     );
    node->get("rescue-rotation",    &m_st_rescue_rotation   );
    node->get("rescue-scale",       &m_st_rescue_scale      );
    node->get("rescue-rotor-offset", &m_st_rescue_rotor_offset);
    node->get("rescue-rotor-rotation", &m_st_rescue_rotor_rotation);
    node->get("rescue-rotor-scale", &m_st_rescue_rotor_scale);

    float angle_to_kart = atan2(m_st_start_offset.getX(),
                                m_st_start_offset.getZ())
                        * RAD_TO_DEGREE;
    m_st_start_rotation.setY(m_st_start_rotation.getY()+angle_to_kart);

    m_st_traffic_buffer = findTrafficBuffer(m_st_start_light_mesh);
    if (m_st_traffic_buffer < 0)
        m_st_traffic_buffer = findTrafficBuffer(m_st_start_mesh);

    delete node;
}   // init

// ----------------------------------------------------------------------------
/** Frees the static mesh.
 */
void Referee::cleanup()
{
    scene::IAnimatedMesh* removed_a = m_st_start_mesh;
    removeMeshOnce(m_st_start_mesh, NULL, NULL, NULL);
    scene::IAnimatedMesh* removed_b = m_st_start_light_mesh;
    removeMeshOnce(m_st_start_light_mesh, removed_a, NULL, NULL);
    scene::IAnimatedMesh* removed_c = m_st_rescue_mesh;
    removeMeshOnce(m_st_rescue_mesh, removed_a, removed_b, NULL);
    removeMeshOnce(m_st_rescue_rotor_mesh, removed_a, removed_b, removed_c);
    m_st_traffic_buffer = -1;
}   // cleanup

// ----------------------------------------------------------------------------
/** Creates an instance of the referee, using the static values to initialise
 *  it. This is the constructor used when a start referee is needed.
 */
Referee::Referee()
{
    assert(m_st_start_mesh);
    m_scene_node = irr_driver->addAnimatedMesh(NULL, "referee");
    m_scene_node->setMesh(m_st_start_mesh);
    m_scene_node->grab();
    m_scene_node->setRotation(m_st_start_rotation.toIrrVector());
    m_scene_node->setScale(m_st_start_scale.toIrrVector());
    m_body_node = m_scene_node;
    m_body_node->setFrameLoop(m_st_first_start_frame, m_st_last_start_frame);

    if (m_st_start_light_mesh)
    {
        m_start_light_node = irr_driver->addAnimatedMesh(
            m_st_start_light_mesh, "referee_start_lights", m_scene_node);
        m_start_light_node->setPosition(m_st_start_light_offset.toIrrVector());
        m_start_light_node->setRotation(m_st_start_light_rotation.toIrrVector());
        m_start_light_node->setScale(m_st_start_light_scale.toIrrVector());
    }
    else
    {
        m_start_light_node = NULL;
    }
    m_rescue_rotor_node = NULL;
#ifndef SERVER_ONLY
    // The referee is repositioned per camera every frame (a presentation
    // object); without this the relativistic warp treats the descent and
    // the camera-relative repositioning as physical velocity and Einstein
    // pops around during the start countdown.
    SP::registerPresentationNode(m_scene_node);
    if ((CVS->isGLSL() && CVS->isDeferredEnabled()) ||
        irr_driver->getVideoDriver()->getDriverType() == video::EDT_VULKAN)
    {
        scene::ISceneNode* light_parent = m_start_light_node
            ? (scene::ISceneNode*)m_start_light_node : (scene::ISceneNode*)m_scene_node;
        m_light = irr_driver->addLight(core::vector3df(0.0f, 0.0f, 0.6f), 0.7f, 2.0f,
            0.7f /* r */, 0.0 /* g */, 0.0f /* b */, false /* sun */, light_parent);
    }
    else
#endif
    {
        m_light = NULL;
    }
}   // Referee

// ----------------------------------------------------------------------------
/** Creates an instance of the referee, using the static values to initialise
 *  it. This is the constructor used when a rescue referee is needed.
 *  \param kart The kart which the referee should rescue.
 */
Referee::Referee(const AbstractKart &kart)
{
    assert(m_st_rescue_mesh);
    m_scene_node = irr_driver->addAnimatedMesh(NULL, "referee");
    m_scene_node->setMesh(m_st_rescue_mesh);
    m_scene_node->grab();
    m_scene_node->setScale(m_st_rescue_scale.toIrrVector());
    m_scene_node->setRotation(m_st_rescue_rotation.toIrrVector());
    m_scene_node->setPosition(core::vector3df(0, kart.getKartHeight() + 0.4f, 0) +
                              m_st_rescue_offset.toIrrVector());
    m_body_node = m_scene_node;
    m_body_node->setFrameLoop(m_st_first_rescue_frame, m_st_last_rescue_frame);
    m_start_light_node = NULL;
    m_light = NULL;
#ifndef SERVER_ONLY
    SP::registerPresentationNode(m_scene_node);
#endif

    if (m_st_rescue_rotor_mesh)
    {
        m_rescue_rotor_node = irr_driver->addAnimatedMesh(
            m_st_rescue_rotor_mesh, "referee_rescue_rotor", m_scene_node);
        m_rescue_rotor_node->setPosition(m_st_rescue_rotor_offset.toIrrVector());
        m_rescue_rotor_node->setRotation(m_st_rescue_rotor_rotation.toIrrVector());
        m_rescue_rotor_node->setScale(m_st_rescue_rotor_scale.toIrrVector());
    }
    else
    {
        m_rescue_rotor_node = NULL;
    }

}   // Referee

// ----------------------------------------------------------------------------
Referee::~Referee()
{
#ifndef SERVER_ONLY
    SP::unregisterPresentationNode(m_scene_node);
#endif
    if(m_scene_node->getParent())
        irr_driver->removeNode(m_scene_node);
    m_scene_node->drop();
}   // ~Referee

// ----------------------------------------------------------------------------
/** Make sure that this referee is attached to the scene graph. This is used
 *  for the start referee, which is removed from scene graph once the ready-
 *  set-go phase is over (it is kept in case of a restart of the race).
 */
void Referee::attachToSceneNode()
{
    if(!m_scene_node->getParent())
        m_scene_node->setParent(irr_driver->getSceneManager()
                                          ->getRootSceneNode());

    if (m_light != NULL)
        m_light->setVisible(true);
}   // attachToSceneNode

// ----------------------------------------------------------------------------
/** Removes the referee's scene node from the scene graph, but still keeps
 *  the scene node in memory. This is used for the start referee, so that
 *  it is quickly available in case of a restart.
 */
void Referee::removeFromSceneGraph()
{
    if(isAttached())
        irr_driver->removeNode(m_scene_node);
    if (m_light != NULL)
        m_light->setVisible(false);
}   // removeFromSceneGraph

// ----------------------------------------------------------------------------
/** Selects one of the states 'ready', 'set', or 'go' to be displayed by
 *  the referee.
 *  \param rsg 0=ready, 1=set, 2=go.
 */
void Referee::selectReadySetGo(int rsg)
{
    if (m_st_traffic_buffer < 0)
        return;

    scene::IAnimatedMeshSceneNode* traffic_node = m_start_light_node
        ? m_start_light_node : m_body_node;
    if (!traffic_node)
        return;

    SP::SPMeshNode* spmn = dynamic_cast<SP::SPMeshNode*>(traffic_node);
    if (spmn)
    {
        spmn->setTextureMatrix(m_st_traffic_buffer, {{ 0.0f, rsg * 0.333f }});
    }
    else
    {
        video::SMaterial &m = traffic_node->getMaterial(m_st_traffic_buffer);
        core::matrix4* matrix = &m.getTextureMatrix(0);
        matrix->setTextureTranslate(0.0f, rsg*0.333f);
        // disable lighting, we need to see the traffic light even if facing away
        // from the sun
        m.AmbientColor  = video::SColor(255, 255, 255, 255);
        m.DiffuseColor  = video::SColor(255, 255, 255, 255);
        m.EmissiveColor = video::SColor(255, 255, 255, 255);
        m.SpecularColor = video::SColor(255, 255, 255, 255);
    }

    LightNode* lnode = dynamic_cast<LightNode*>(m_light);
    if (lnode != NULL)
    {
        if (rsg == 0)
        {
            lnode->setColor(0.6f, 0.0f, 0.0f);
        }
        else if (rsg == 1)
        {
            lnode->setColor(0.7f, 0.23f, 0.0f);
        }
        else if (rsg == 2)
        {
            lnode->setColor(0.0f, 0.6f, 0.0f);
        }
        return;
    }
    scene::ILightSceneNode* irr_node = dynamic_cast<scene::ILightSceneNode*>(
        m_light);
    if (irr_node != NULL)
    {
        video::SLight& data = irr_node->getLightData();
        if (rsg == 0)
        {
            data.DiffuseColor = video::SColorf(0.6f, 0.0f, 0.0f);
        }
        else if (rsg == 1)
        {
            data.DiffuseColor = video::SColorf(0.7f, 0.23f, 0.0f);
        }
        else if (rsg == 2)
        {
            data.DiffuseColor = video::SColorf(0.0f, 0.6f, 0.0f);
        }
    }
}   // selectReadySetGo

// ----------------------------------------------------------------------------
/** Set the referee animation frame with created ticks of \ref RescueAnimation,
 *  so that it's synchronized with world ticks, and can be rewound easily.
 */
void Referee::setAnimationFrameWithCreatedTicks(int created_ticks)
{
    if (!m_body_node)
        return;

    float dur = stk_config->ticks2Time(
        World::getWorld()->getTicksSinceStart() - created_ticks);
    dur *= 25.0f;
    float ref_dur = (float)(m_st_last_rescue_frame - m_st_first_rescue_frame);
    float frame = std::fmod(dur, ref_dur);
    frame += (float)m_st_first_rescue_frame;
    m_body_node->setCurrentFrame(frame);
}   // setAnimationFrameWithCreatedTicks

// ----------------------------------------------------------------------------
void Referee::updateRescueVisuals(int created_ticks)
{
    setAnimationFrameWithCreatedTicks(created_ticks);
    if (!m_rescue_rotor_node)
        return;

    const int ticks = World::getWorld()->getTicksSinceStart() - created_ticks;
    const float rotor_angle = std::fmod((float)ticks * 45.0f, 360.0f);
    Vec3 rotation = m_st_rescue_rotor_rotation;
    rotation.setY(rotation.getY() + rotor_angle);
    m_rescue_rotor_node->setRotation(rotation.toIrrVector());
}   // updateRescueVisuals

// ----------------------------------------------------------------------------
/** Moves the referee to the specified position. */
void Referee::setPosition(const Vec3 &xyz)
{
    m_scene_node->setPosition(xyz.toIrrVector());
}   // setPosition

// ----------------------------------------------------------------------------
/** Sets the rotation of the scene node (in degrees).
 *  \param hpr Rotation in degrees. */
void Referee::setRotation(const Vec3 &hpr)
{
    m_scene_node->setRotation(hpr.toIrrVector());
}   // setRotation

// ----------------------------------------------------------------------------
/** Returns true if this referee is attached to the scene graph. */
bool Referee::isAttached() const
{
    return m_scene_node->getParent() != NULL;
}   // isAttached
