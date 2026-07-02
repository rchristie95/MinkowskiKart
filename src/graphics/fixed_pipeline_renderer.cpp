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
#include "karts/abstract_kart.hpp"
#include "modes/world.hpp"
#include "physics/physics.hpp"
#include "relativity/relativity_math.hpp"
#include "tracks/track.hpp"
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
        SP::resetRelativityNodeCaches();
        GE::setNodeVelocityFunction(&SP::fillNodeRelativityVelocity);
        // Per-object glow colours (items, glowing track objects) for the GE
        // glow pass, mirroring SPMeshNode::getGlowColor under OpenGL.
        GE::setNodeGlowColorFunction(&SP::fillNodeGlowColor);
        // Sun shadow map settings, picked up when the race's draw calls
        // create their Vulkan data.
        GE::getGEConfig()->m_shadow_map_size =
            UserConfigParams::m_dynamic_lights ?
            (int)UserConfigParams::m_shadows_resolution : 0;
        GE::getGEConfig()->m_pcss = UserConfigParams::m_pcss;
        GE::getGEConfig()->m_glow = UserConfigParams::m_glow;
        // The driver's fog state persists across tracks (Track only calls
        // setFog when it uses fog); clear it so a fog-less track doesn't
        // inherit the previous track's haze.
        Track* fog_track = Track::getCurrentTrack();
        if (!(fog_track && fog_track->isFogEnabled()))
        {
            irr_driver->getVideoDriver()->setFog(video::SColor(0, 0, 0, 0),
                video::EFT_FOG_LINEAR, 0.0f, 0.0f, 0.0f);
        }
        // Relativistic warping moves vertices far outside their mesh
        // bounding boxes (aberration brings geometry from behind into
        // view), so frustum culling against unwarped boxes would hide
        // visible geometry (e.g. the sun, the far side of the Möbius
        // strip). SP disables culling the same way under OpenGL.
        GE::getGEConfig()->m_disable_frustum_culling =
            Relativity::isEnabled();
        // Screen-space post effects (motion blur, black hole / wormhole
        // lensing, compactification) are applied in the deferred displace
        // compose pass; force that path so they always work.
        GE::getGEConfig()->m_force_displace_compose =
            Relativity::isEnabled();
        // Route static geometry through the adaptively tessellated material
        // variants so large triangles (ocean planes etc.) subdivide and warp
        // smoothly instead of rigidly. NOT on Apple/MoltenVK: emulated
        // tessellation there needs a per-draw compute pre-pass that forces a
        // TBDR tile flush, crawling at ~0.3 fps regardless of subdivision
        // level. Coarse geometry is instead pre-subdivided on the CPU at load
        // (GESPMBuffer::subdivideForRelativity), so the per-vertex warp is just
        // as smooth at full framerate.
#if defined(__APPLE__)
        GE::getGEConfig()->m_adaptive_tessellation = false;
#else
        GE::getGEConfig()->m_adaptive_tessellation =
            Relativity::isEnabled();
#endif
    }
    m_boost_time.clear();
    m_boost_time.resize(Camera::getNumCameras(), 0.0f);
}

void FixedPipelineRenderer::onUnloadWorld()
{
#ifndef SERVER_ONLY
    // Scene node pointers are recycled between tracks.
    SP::clearVulkanGlowNodes();
#endif
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
            // Debug hooks to force the screen-space post effects on, so
            // they can be tested without the corresponding gameplay items:
            // MK_TEST_FX (black hole + wormhole), MK_TEST_BLUR,
            // MK_TEST_COMPACT.
            static const bool test_fx = getenv("MK_TEST_FX") != NULL;
            static const bool test_blur = getenv("MK_TEST_BLUR") != NULL;
            static const bool test_compact =
                getenv("MK_TEST_COMPACT") != NULL;
            static const bool test_wave = getenv("MK_TEST_WAVE") != NULL;

            SP::sp_cur_player = i;
            SP::updateRelativityKartVelocities(i);
            std::array<float, 42> relativity_tail =
                SP::getRelativityUBOTail(i);
            if (test_wave && camera->getKart())
            {
                // Force a static time-dilation wave ring centred on the player
                // so the screen-space effect can be inspected without firing
                // the powerup.
                const Vec3& kp = camera->getKart()->getSmoothedXYZ();
                relativity_tail[38] = kp.getX();
                relativity_tail[39] = kp.getY();
                relativity_tail[40] = kp.getZ();
                relativity_tail[41] = 35.0f; // mid-expansion radius
            }
            if (test_fx && camera->getKart())
            {
                const Vec3& kp = camera->getKart()->getSmoothedXYZ();
                // Two test black holes (slots 0 and 1) and a wormhole.
                relativity_tail[18] = kp.getX() - 8.0f;
                relativity_tail[19] = kp.getY() + 4.0f;
                relativity_tail[20] = kp.getZ();
                relativity_tail[21] = 3.0f;
                relativity_tail[22] = kp.getX() - 14.0f;
                relativity_tail[23] = kp.getY() + 6.0f;
                relativity_tail[24] = kp.getZ() + 8.0f;
                relativity_tail[25] = 2.0f;
                relativity_tail[34] = kp.getX() + 10.0f;
                relativity_tail[35] = kp.getY() + 5.0f;
                relativity_tail[36] = kp.getZ() - 6.0f;
                relativity_tail[37] = 4.0f;
            }
            auto* cam_node = dynamic_cast<GE::GEVulkanCameraSceneNode*>(
                camera->getCameraSceneNode());
            if (cam_node)
            {
                // (debug) measure camera movement before the previous-PV
                // capture below collapses the two matrices.
                static const bool test_zip_dbg = getenv("MK_TEST_ZIP") != NULL;
                float pv_delta = 0.0f;
                if (test_zip_dbg)
                {
                    const GE::GEVulkanCameraUBO* ubo = cam_node->getUBOData();
                    for (int m = 0; m < 16; m++)
                    {
                        pv_delta += fabsf(
                            ubo->m_projection_view_matrix[m] -
                            ubo->m_previous_pv_matrix[m]);
                    }
                }
                cam_node->updatePreviousPVMatrix();
                cam_node->setRelativityData(relativity_tail.data());

                // Screen-space post effect parameters, mirroring the
                // SP/OpenGL post processing chain: boost motion blur
                // (PostProcessing::renderMotionBlur uses boost_time * 10,
                // centre (0.5, 0.5) and mask radius 0.15) and the
                // compactification screen warp strength.
                if (i < m_boost_time.size() && m_boost_time[i] > 0.0f)
                {
                    m_boost_time[i] -= dt;
                    if (m_boost_time[i] < 0.0f)
                        m_boost_time[i] = 0.0f;
                }
                const bool motion_blur_enabled =
                    UserConfigParams::m_motionblur || test_blur;
                float motion_blur[4] =
                {
                    motion_blur_enabled && i < m_boost_time.size() ?
                        m_boost_time[i] * 10.0f : 0.0f,
                    0.5f, 0.5f, 0.15f
                };
                if (test_blur)
                    motion_blur[0] = 3.0f;
                // MK_TEST_ZIP: simulate a zipper boost every 4 seconds to
                // exercise the full giveBoost -> decay -> blur path.
                static const bool test_zip = getenv("MK_TEST_ZIP") != NULL;
                if (test_zip)
                {
                    static float zip_timer = 0.0f;
                    zip_timer += dt;
                    if (zip_timer > 4.0f)
                    {
                        zip_timer = 0.0f;
                        giveBoost(i);
                        printf("MK_TEST_ZIP: giveBoost(%u)\n", i);
                    }
                    if (i < m_boost_time.size() && m_boost_time[i] > 0.0f)
                    {
                        printf("MK_TEST_ZIP: boost=%f blur_amount=%f "
                               "pv_delta=%f\n",
                               m_boost_time[i], motion_blur[0], pv_delta);
                    }
                }
                // The compactification debuff only flattens the affected
                // kart's model; the full-screen compression for the affected
                // player was removed on purpose. The shader path stays for
                // debugging via MK_TEST_COMPACT.
                float compactification[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                if (test_compact)
                    compactification[0] = 0.7f;
                // Screen-space ports of the SP/OpenGL advanced pipeline
                // options (bloom, depth of field, anti-aliasing), applied by
                // displace_color.frag. Vulkan AO is handled in deferred PBR.
                float postfx_flags[4] =
                {
                    UserConfigParams::m_bloom ? 1.0f : 0.0f,
                    // Ambient occlusion is not offered under Vulkan (greyed
                    // out in the settings dialog).
                    0.0f,
                    UserConfigParams::m_dof   ? 1.0f : 0.0f,
                    UserConfigParams::m_mlaa  ? 1.0f : 0.0f
                };
                // Second block: per-object glow outlines and volumetric
                // light scattering (density = 1 / (40 * fog start), like
                // LightingPasses::renderLightsScatter; 0 disables).
                Track* fx_track = Track::getCurrentTrack();
                float scatter_density = 0.0f;
                if (UserConfigParams::m_light_scatter && fx_track &&
                    fx_track->isFogEnabled())
                {
                    scatter_density =
                        1.0f / (40.0f * (fx_track->getFogStart() + 0.001f));
                }
                float lens_flare_strength =
                    (float)UserConfigParams::m_vk_flare * 0.01f;
                if (fx_track && fx_track->hasGodRays() &&
                    UserConfigParams::m_light_shaft)
                {
                    const float track_flare =
                        fx_track->getGodRaysOpacity() * 0.85f;
                    if (lens_flare_strength < track_flare)
                        lens_flare_strength = track_flare;
                }
                float postfx_flags2[4] =
                {
                    UserConfigParams::m_glow ? 1.0f : 0.0f,
                    scatter_density,
                    // Sun lens flare strength, with a floor for tracks that
                    // explicitly declare a bright lightshaft source.
                    lens_flare_strength,
                    // Animation clock (seconds, wraps at 3600s) for the
                    // swirling Kerr accretion disk.
                    (float)(GE::getMonoTimeMs() % 3600000) * 0.001f
                };
                // Post-processing style knobs from the settings gauges
                float beauty_params[4] =
                {
                    (float)UserConfigParams::m_vk_exposure * 0.1f,
                    (float)UserConfigParams::m_vk_saturation * 0.01f,
                    (float)UserConfigParams::m_vk_vignette * 0.01f,
                    (float)UserConfigParams::m_vk_sharpness * 0.01f
                };
                // Keep the per-frame GE toggles in sync so the glow pass is
                // recorded / the shadow pass keeps running when the user
                // changes settings mid-race (the shadow map resolution
                // itself applies from the next race).
                GE::getGEConfig()->m_glow = UserConfigParams::m_glow;
                // Vulkan AO is hidden behind a developer toggle and is
                // applied in deferred lighting, not in displace_color.frag.
                GE::getGEConfig()->m_ssao =
                    UserConfigParams::m_dynamic_lights &&
                    UserConfigParams::m_vk_debug_ao;
                GE::getGEConfig()->m_pcss = UserConfigParams::m_pcss;
                GE::getGEConfig()->m_shadow_map_size =
                    UserConfigParams::m_dynamic_lights ?
                    (int)UserConfigParams::m_shadows_resolution : 0;
                cam_node->setPostFXData(motion_blur, compactification,
                    postfx_flags, postfx_flags2, beauty_params);

                // Track god rays / light shafts, mirroring the SP/OpenGL
                // PostProcessing::renderGodRays sun (a world-radius-20 glow
                // sphere at the track's lightshaft position, additively
                // blended at the track's opacity).
                float godrays[8] =
                    { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
                Track* gr_track = Track::getCurrentTrack();
                if (gr_track && gr_track->hasGodRays() &&
                    UserConfigParams::m_light_shaft)
                {
                    const core::vector3df gr_pos =
                        gr_track->getGodRaysPosition();
                    const video::SColorf gr_col(gr_track->getGodRaysColor());
                    godrays[0] = gr_pos.X;
                    godrays[1] = gr_pos.Y;
                    godrays[2] = gr_pos.Z;
                    godrays[3] = gr_track->getGodRaysOpacity();
                    godrays[4] = gr_col.r;
                    godrays[5] = gr_col.g;
                    godrays[6] = gr_col.b;
                    godrays[7] = 20.0f;  // sun interposer world radius
                }
                cam_node->setGodRaysData(godrays);
            }
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

void FixedPipelineRenderer::giveBoost(unsigned int cam_index)
{
    // Same boost duration as PostProcessing::giveBoost under OpenGL.
    if (cam_index >= m_boost_time.size())
        m_boost_time.resize(cam_index + 1, 0.0f);
    m_boost_time[cam_index] = 0.75f;
}   // giveBoost

std::unique_ptr<RenderTarget> FixedPipelineRenderer::createRenderTarget(const irr::core::dimension2du &dimension,
                                                                        const std::string &name)
{
    return std::unique_ptr<RenderTarget>(new GL1RenderTarget(dimension, name));
}
#endif
