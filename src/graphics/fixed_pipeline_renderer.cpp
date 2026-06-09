//  MinkowskiKart - a fun racing game with go-kart
//  Copyright (C) 2015 MinkowskiKart-Team
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

#ifndef SERVER_ONLY
#include "graphics/fixed_pipeline_renderer.hpp"
#include "config/user_config.hpp"
#include "graphics/camera/camera.hpp"
#include "graphics/irr_driver.hpp"
#include "graphics/render_target.hpp"
#include "graphics/sp/sp_base.hpp"
#include "modes/world.hpp"
#include "physics/physics.hpp"
#include "relativity/relativity_math.hpp"
#include "utils/profiler.hpp"

#include <ISceneManager.h>
#include <IVideoDriver.h>

#include <ge_main.hpp>
#include <ge_vulkan_camera_scene_node.hpp>

void FixedPipelineRenderer::onLoadWorld()
{
    // Under the GE Vulkan renderer, fill the per-object relativistic
    // velocity in the object buffer the same way the SP instance buffer
    // carries it under OpenGL.
    if (irr_driver->getVideoDriver()->getDriverType() == video::EDT_VULKAN)
    {
        GE::setNodeVelocityFunction(&SP::fillNodeRelativityVelocity);
        // Relativistic warping moves vertices far outside their mesh
        // bounding boxes (aberration brings geometry from behind into
        // view), so frustum culling against unwarped boxes would hide
        // visible geometry (e.g. the sun, the far side of the Möbius
        // strip). SP disables culling the same way under OpenGL.
        GE::getGEConfig()->m_disable_frustum_culling =
            Relativity::isEnabled();
    }
}

void FixedPipelineRenderer::onUnloadWorld()
{
    
}

void FixedPipelineRenderer::render(float dt, bool is_loading)
{
    World *world = World::getWorld(); // Never NULL.

    irr_driver->getVideoDriver()->beginScene(/*backBuffer clear*/ true,
                                             /*zBuffer*/ true,
                                             irr_driver->getClearColor());
    
    irr_driver->getVideoDriver()->enableMaterial2D();

    RaceGUIBase *rg = world->getRaceGUI();
    if (rg) rg->update(dt);


    for(unsigned int i=0; i<Camera::getNumCameras(); i++)
    {
        Camera *camera = Camera::getCamera(i);

        std::ostringstream oss;
        oss << "drawAll() for kart " << i;
        PROFILER_PUSH_CPU_MARKER(oss.str().c_str(), (i+1)*60,
                                 0x00, 0x00);
        camera->activate();
        rg->preRenderCallback(camera);   // adjusts start referee

        // Feed the relativistic observer parameters for this camera into
        // the GE Vulkan camera UBO (the SP pipeline does the equivalent in
        // SP::uploadAll, which never runs under Vulkan).
        if (irr_driver->getVideoDriver()->getDriverType() == video::EDT_VULKAN)
        {
            SP::sp_cur_player = i;
            SP::updateRelativityKartVelocities(i);
            const std::array<float, 26> relativity_tail =
                SP::getRelativityUBOTail(i);
            auto* cam_node = dynamic_cast<GE::GEVulkanCameraSceneNode*>(
                camera->getCameraSceneNode());
            if (cam_node)
                cam_node->setRelativityData(relativity_tail.data());
        }

        irr_driver->getSceneManager()->drawAll();

        PROFILER_POP_CPU_MARKER();

        // Note that drawAll must be called before rendering
        // the bullet debug view, since otherwise the camera
        // is not set up properly. This is only used for
        // the bullet debug view.
        if (UserConfigParams::m_artist_debug_mode)
            Physics::get()->draw();
    }   // for i<world->getNumKarts()

    // For GEVulkanDriver
    irr_driver->getSceneManager()->setActiveCamera(NULL);

    // Set the viewport back to the full screen for race gui
    irr_driver->getVideoDriver()->setViewPort(core::recti(0, 0,
                                              UserConfigParams::m_width,
                                              UserConfigParams::m_height));

    for(unsigned int i=0; i<Camera::getNumCameras(); i++)
    {
        Camera *camera = Camera::getCamera(i);
        irr_driver->getSceneManager()->setActiveCamera(
            camera->getCameraSceneNode());
        std::ostringstream oss;
        oss << "renderPlayerView() for kart " << i;

        PROFILER_PUSH_CPU_MARKER(oss.str().c_str(), 0x00, 0x00, (i+1)*60);
        rg->renderPlayerView(camera, dt);
        PROFILER_POP_CPU_MARKER();

    }  // for i<getNumKarts

    // Either render the gui, or the global elements of the race gui.
    GUIEngine::render(dt, is_loading);

    if (irr_driver->getRenderNetworkDebug() && !is_loading)
        irr_driver->renderNetworkDebug();

    // Render the profiler
    if(UserConfigParams::m_profiler_enabled)
    {
        PROFILER_DRAW();
    }

#ifdef DEBUG
    drawDebugMeshes();
#endif

    irr_driver->getVideoDriver()->endScene();
    
}

std::unique_ptr<RenderTarget> FixedPipelineRenderer::createRenderTarget(const irr::core::dimension2du &dimension,
                                                                        const std::string &name)
{
    return std::unique_ptr<RenderTarget>(new GL1RenderTarget(dimension, name));
}
#endif
