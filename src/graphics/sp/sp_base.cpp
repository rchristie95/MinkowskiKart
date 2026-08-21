//  MinkowskiKart - a fun racing game with go-kart
//  Copyright (C) 2018 MinkowskiKart-Team
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

#include "graphics/sp/sp_base.hpp"
#include "config/stk_config.hpp"
#include "config/user_config.hpp"
#include "graphics/central_settings.hpp"
#include "graphics/camera/camera.hpp"
#include "graphics/frame_buffer.hpp"
#include "graphics/irr_driver.hpp"
#include "graphics/material.hpp"
#include "graphics/material_manager.hpp"
#include "graphics/shader_based_renderer.hpp"
#include "graphics/shared_gpu_objects.hpp"
#include "graphics/shader_based_renderer.hpp"
#include "graphics/post_processing.hpp"
#include "graphics/rtts.hpp"
#include "graphics/shaders.hpp"
#include "graphics/skybox.hpp"
#include "graphics/sp/sp_dynamic_draw_call.hpp"
#include "graphics/sp/sp_instanced_data.hpp"
#include "graphics/sp/sp_per_object_uniform.hpp"
#include "graphics/sp/sp_mesh.hpp"
#include "graphics/sp/sp_mesh_buffer.hpp"
#include "graphics/sp/sp_mesh_node.hpp"
#include "graphics/sp/sp_shader.hpp"
#include "graphics/sp/sp_shader_manager.hpp"
#include "graphics/sp/sp_texture.hpp"
#include "graphics/sp/sp_texture_manager.hpp"
#include "graphics/sp/sp_uniform_assigner.hpp"
#include "guiengine/engine.hpp"
#include "io/file_manager.hpp"
#include "karts/abstract_kart.hpp"
#include "karts/kart.hpp"
#include "modes/world.hpp"
#include "relativity/observer_snapshot.hpp"
#include "relativity/relativistic_state.hpp"
#include "relativity/relativity_math.hpp"
#include "tracks/track.hpp"
#include "utils/log.hpp"
#include "utils/profiler.hpp"
#include "utils/string_utils.hpp"
#include "utils/time.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <functional>
#include <string>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <ge_main.hpp>
#include <ge_render_info.hpp>
#include <IFileSystem.h>
#include <ISceneManager.h>
#include <ge_vulkan_camera_scene_node.hpp>

#include <IrrlichtDevice.h>

using namespace GE;

namespace SP
{

extern unsigned sp_cur_player;

namespace
{
const size_t SP_MATRIX_UBO_BASE_FLOATS = 16 * 9 + 2;
const size_t SP_MATRIX_UBO_FLOATS = 188;   // +16 black_holes[4], +4 wormhole, +4 grav_wave
const size_t SP_RELATIVITY_UBO_FLOAT_OFFSET = 146;
const size_t SP_RELATIVITY_UBO_FLOAT_COUNT = 42; // +16 black_holes[4], +4 wormhole, +4 grav_wave

struct RelativityMotionState
{
    bool             m_has_sample = false;
    unsigned int     m_last_time_ms = 0;
    core::vector3df  m_last_position = core::vector3df(0.0f, 0.0f, 0.0f);
    core::vector3df  m_velocity = core::vector3df(0.0f, 0.0f, 0.0f);
};   // struct RelativityMotionState

std::unordered_map<const scene::ISceneNode*, RelativityMotionState>
    g_relativity_motion_states;

// Scene nodes belonging to animated track objects (ThreeDAnimation-driven).
// Their per-frame Bezier position deltas are not real translational velocities,
// so we always return zero velocity for them to avoid relativistic stutter.
std::unordered_set<const scene::ISceneNode*> g_animated_track_nodes;

// Camera-anchored presentation nodes (e.g. the start referee, which is
// repositioned per camera every frame by RaceGUIBase::preRenderCallback).
// Their graphics deltas are presentation-only, so they render with zero
// velocity like static scenery instead of feeding the relativistic warp.
std::unordered_set<const scene::ISceneNode*> g_presentation_nodes;

// Per-camera cache: kart root scene node -> visual velocity. Every kart uses
// its true coordinate velocity so the whole kart (body/wheels/headlights)
// shares one consistent retarded-position transform instead of noisy per-node
// graphics-delta estimates.
std::unordered_map<const scene::ISceneNode*, core::vector3df>
    g_kart_root_velocities;

// Explicit per-node velocity overrides for objects that have a true physical
// velocity available (flyables -> rigid-body velocity; raptor props ->
// analytic velocity). Fed each frame so they share the karts' smooth, accurate
// retarded-position transform instead of the noisy graphics-delta estimator
// (which jitters/ghosts under non-constant velocity).
std::unordered_map<const scene::ISceneNode*, core::vector3df>
    g_node_velocity_overrides;

// Live black holes that lens the screen, keyed by their BlackHole flyable so
// several can be active at once. Values are (world position, world radius).
std::map<const void*, std::pair<core::vector3df, float> >
    g_black_hole_lenses;

// Single active time-dilation gravitational wave: expanding ripple centre and
// current world radius (radius <= 0 means inactive). Published each frame by
// RelativisticVFXManager and consumed by displace_color.frag.
core::vector3df g_grav_wave_pos(0.0f, 0.0f, 0.0f);
float           g_grav_wave_radius = 0.0f;

bool isFiniteVector(const core::vector3df& v)
{
    return std::isfinite((double)v.X) &&
           std::isfinite((double)v.Y) &&
           std::isfinite((double)v.Z);
}   // isFiniteVector

bool isItemPickupNode(const scene::ISceneNode* node)
{
    const scene::ISceneNode* current = node;
    while (current)
    {
        const std::string name = current->getName();
        if (name == "item" ||
            name.rfind("item_", 0) == 0 ||
            name.rfind("item_lo_", 0) == 0 ||
            name.rfind("item:", 0) == 0)
        {
            return true;
        }
        current = current->getParent();
    }
    return false;
}   // isItemPickupNode

core::vector3df estimateNodeVelocity(const scene::ISceneNode* node,
                                     const core::vector3df& position)
{
    if (!Relativity::isEnabled() || !node || !isFiniteVector(position))
        return core::vector3df(0.0f, 0.0f, 0.0f);

    // Animated track objects (balloons, etc.) are stationary in the world
    // frame; their Bezier-driven position deltas are visual-only and must not
    // be fed into the relativistic shader as real velocities. Walk the
    // ancestors too: an animated library or LOD object registers its root
    // node, but the mesh nodes actually rendered are its children, and they
    // inherit the same non-physical motion (visible as warp popping on
    // slow-moving environmental props).
    // Presentation objects (start referee etc.) are repositioned per camera
    // per frame; their motion is not a physical velocity either.
    for (const scene::ISceneNode* cur = node; cur; cur = cur->getParent())
    {
        if (g_animated_track_nodes.count(cur) ||
            g_presentation_nodes.count(cur))
            return core::vector3df(0.0f, 0.0f, 0.0f);
    }

    // Item pickups rotate, respawn-scale, and swap LODs as gameplay markers,
    // not as physical moving bodies. Keep their shader velocity anchored to
    // the track so those presentation transforms do not produce lateral drift.
    if (isItemPickupNode(node))
        return core::vector3df(0.0f, 0.0f, 0.0f);

    RelativityMotionState& state = g_relativity_motion_states[node];
    const unsigned int now_ms = (unsigned int)irr_driver->getRealTime();
    if (!state.m_has_sample)
    {
        state.m_has_sample = true;
        state.m_last_time_ms = now_ms;
        state.m_last_position = position;
        state.m_velocity = core::vector3df(0.0f, 0.0f, 0.0f);
        return state.m_velocity;
    }
    // A node is sampled once per mesh buffer per camera each frame; repeat
    // calls within the same millisecond must not disturb the estimator
    // state, or the smoothed velocity gets zeroed every frame for
    // multi-material nodes (visible as metre-scale warp popping).
    if (now_ms <= state.m_last_time_ms)
        return state.m_velocity;

    const float dt = (float)(now_ms - state.m_last_time_ms) * 0.001f;
    state.m_last_time_ms = now_ms;
    if (dt <= 1.0e-4f || dt > 0.5f)
    {
        state.m_last_position = position;
        state.m_velocity = core::vector3df(0.0f, 0.0f, 0.0f);
        return state.m_velocity;
    }

    const core::vector3df delta = position - state.m_last_position;
    state.m_last_position = position;

    const float max_expected_speed = std::max(
        60.0f, Relativity::getCurrentCLight() * 2.0f);
    const float max_expected_delta = max_expected_speed * dt * 1.5f;
    if (!isFiniteVector(delta) || delta.getLengthSQ() >
        max_expected_delta * max_expected_delta)
    {
        state.m_velocity = core::vector3df(0.0f, 0.0f, 0.0f);
        return state.m_velocity;
    }

    core::vector3df raw_velocity = delta / dt;
    if (!isFiniteVector(raw_velocity))
        raw_velocity = core::vector3df(0.0f, 0.0f, 0.0f);

    const float blend = std::min(1.0f, std::max(0.25f, dt * 10.0f));
    state.m_velocity = state.m_velocity * (1.0f - blend) +
                       raw_velocity * blend;
    return state.m_velocity;
}   // estimateNodeVelocity

// If 'node' is a descendant of any kart's scene root, return the cached kart
// visual velocity. This prevents per-node graphics-delta estimates from making
// body/wheels/headlights drift apart. Non-observer karts use the same v=0
// retarded-position path as static track geometry.
bool findKartVelocityForNode(const scene::ISceneNode* node,
                             core::vector3df& out_velocity)
{
    if (g_kart_root_velocities.empty() || !node)
        return false;
    const scene::ISceneNode* current = node;
    while (current)
    {
        auto it = g_kart_root_velocities.find(current);
        if (it != g_kart_root_velocities.end())
        {
            out_velocity = it->second;
            return true;
        }
        current = current->getParent();
    }
    return false;
}   // findKartVelocityForNode

// Mirrors findKartVelocityForNode for the explicit override map (flyables /
// raptor props): returns true and the velocity if the node or any ancestor has
// a registered physical velocity.
bool findNodeOverrideVelocity(const scene::ISceneNode* node,
                              core::vector3df& out_velocity)
{
    if (g_node_velocity_overrides.empty() || !node)
        return false;
    const scene::ISceneNode* current = node;
    while (current)
    {
        auto it = g_node_velocity_overrides.find(current);
        if (it != g_node_velocity_overrides.end())
        {
            out_velocity = it->second;
            return true;
        }
        current = current->getParent();
    }
    return false;
}   // findNodeOverrideVelocity

// Texture -> no-relativity-warp flag of its STK material. The GE Vulkan
// draw call asks once per object per frame, and resolving the STK material
// walks every loaded material doing string compares, so cache by texture.
// Cleared on world load (texture pointers are recycled between tracks).
std::unordered_map<const video::ITexture*, bool> g_no_warp_texture_cache;

bool materialHasNoRelativityWarp(const video::SMaterial* m)
{
    video::ITexture* t = m->getTexture(0);
    if (!t)
        return false;
    auto it = g_no_warp_texture_cache.find(t);
    if (it != g_no_warp_texture_cache.end())
        return it->second;
    // Resolve the STK material the same way MaterialManager::
    // setAllMaterialFlags does at load time: getMaterialSPM lower-cases and
    // normalises the path, unlike getMaterialFor(t) whose case-sensitive
    // full-path compare never matches on Windows.
    io::path fp =
        file_manager->getFileSystem()->getAbsolutePath(t->getName());
    video::ITexture* t1 = m->getTexture(1);
    Material* stk_material = material_manager->getMaterialSPM(fp.c_str(),
        t1 ? StringUtils::getBasename(t1->getFullPath().c_str()) : "", "");
    const bool no_warp =
        stk_material && stk_material->isNoRelativityWarp();
    g_no_warp_texture_cache[t] = no_warp;
    return no_warp;
}   // materialHasNoRelativityWarp

bool shouldDisableRelativityVisualsForNode(const scene::ISceneNode* node)
{
    (void)node;
    return false;
}   // shouldDisableRelativityVisualsForNode

std::array<float, SP_RELATIVITY_UBO_FLOAT_COUNT> buildRelativityUBOTail(
    unsigned player_index)
{
    std::array<float, SP_RELATIVITY_UBO_FLOAT_COUNT> tail = {{ 0.0f }};

    if (player_index >= Camera::getNumCameras())
        return tail;

    Camera* camera = Camera::getCamera(player_index);
    if (!camera || !camera->getKart())
        return tail;

    const core::vector3df& pos = camera->getCameraSceneNode()->getPosition();
    const btVector3 observer_position(pos.X, pos.Y, pos.Z);
    const Relativity::ObserverVisualState visual_state =
        Relativity::buildObserverVisualState(camera->getKart(),
                                             observer_position);
    if (!visual_state.m_valid)
        return tail;

    tail[2] = visual_state.m_item_active ? 1.0f : 0.0f;
    tail[3] = visual_state.m_doppler_active ? 1.0f : 0.0f;
    tail[4] = visual_state.m_gamma;
    tail[5] = visual_state.m_inverse_gamma;
    tail[6] = visual_state.m_beta_vector.x();
    tail[7] = visual_state.m_beta_vector.y();
    tail[8] = visual_state.m_beta_vector.z();
    tail[9] = visual_state.m_c_light;
    tail[10] = visual_state.m_observer_position.x();
    tail[11] = visual_state.m_observer_position.y();
    tail[12] = visual_state.m_observer_position.z();
    // u_relativity_observer_pos.w: repurposed as the "radio-wave scanner"
    // Doppler-exemption flag so the centre of the player's view stays clear
    // while they are burning nitro.
    tail[13] = visual_state.m_scanner_center_active ? 1.0f : 0.0f;
    const Vec3& bubble_center = camera->getKart()->getSmoothedXYZ();
    tail[14] = bubble_center.getX();
    tail[15] = bubble_center.getY();
    tail[16] = bubble_center.getZ();
    tail[17] = Relativity::getWarpBubbleRadius();
    // u_black_holes[4]: world-space position (xyz) + radius (w) per hole.
    // w = 0 means the slot is inactive. Several black holes can be live at
    // the same time; each registers itself in g_black_hole_lenses.
    unsigned bh_slot = 0;
    for (auto& p : g_black_hole_lenses)
    {
        if (bh_slot >= 4)
            break;
        tail[18 + bh_slot * 4 + 0] = p.second.first.X;
        tail[18 + bh_slot * 4 + 1] = p.second.first.Y;
        tail[18 + bh_slot * 4 + 2] = p.second.first.Z;
        tail[18 + bh_slot * 4 + 3] = p.second.second;
        bh_slot++;
    }
    // u_wormhole: world-space position (xyz) + world-space radius (w).
    // A non-zero radius implicitly marks the wormhole as active; the
    // tonemap post-process uses this radius to project the mouth
    // silhouette into screen space for proper Interstellar-style lensing.
    tail[34] = sp_wormhole_world_pos.X;
    tail[35] = sp_wormhole_world_pos.Y;
    tail[36] = sp_wormhole_world_pos.Z;
    tail[37] = sp_wormhole_active ? sp_wormhole_radius : 0.0f;
    // u_grav_wave: time-dilation expanding ripple, world-space centre (xyz) +
    // current radius (w). w <= 0 marks it inactive.
    tail[38] = g_grav_wave_pos.X;
    tail[39] = g_grav_wave_pos.Y;
    tail[40] = g_grav_wave_pos.Z;
    tail[41] = g_grav_wave_radius;
    return tail;
}   // buildRelativityUBOTail

}   // anonymous namespace

// ----------------------------------------------------------------------------
/** Public wrapper so the GE (Vulkan) renderer can build the same relativity
 *  UBO tail that uploadAll() writes for the SP/OpenGL pipeline. */
std::array<float, 42> getRelativityUBOTail(unsigned player_index)
{
    static_assert(SP_RELATIVITY_UBO_FLOAT_COUNT == 42,
                  "Relativity UBO tail size changed");
    return buildRelativityUBOTail(player_index);
}   // getRelativityUBOTail

// ----------------------------------------------------------------------------
/** Registers or refreshes the screen-space lensing data of one live black
 *  hole. Keyed by owner so several black holes can lens at the same time. */
void setBlackHoleLens(const void* owner, const irr::core::vector3df& pos,
                      float radius)
{
    g_black_hole_lenses[owner] = std::make_pair(pos, radius);
}   // setBlackHoleLens

// ----------------------------------------------------------------------------
void removeBlackHoleLens(const void* owner)
{
    g_black_hole_lenses.erase(owner);
}   // removeBlackHoleLens

// ----------------------------------------------------------------------------
/** Publishes the current state of the time-dilation gravitational wave so the
 *  screen-space ripple in displace_color.frag can follow it. */
void setGravWave(const irr::core::vector3df& pos, float radius)
{
    g_grav_wave_pos = pos;
    g_grav_wave_radius = radius;
}   // setGravWave

// ----------------------------------------------------------------------------
void clearGravWave()
{
    g_grav_wave_radius = 0.0f;
}   // clearGravWave

// ----------------------------------------------------------------------------
/** Refreshes the per-camera kart root -> visual velocity cache. Called from
 *  prepareDrawCalls() for the SP pipeline and from FixedPipelineRenderer for
 *  the GE Vulkan pipeline (which does not run prepareDrawCalls). */
void updateRelativityKartVelocities(unsigned player_index)
{
    (void)player_index;
    g_kart_root_velocities.clear();
    if (!Relativity::isEnabled())
        return;
    World* world = World::getWorld();
    if (!world)
        return;
    // Every kart renders with its true coordinate velocity. Contact is a
    // spacetime event, so a wheel touching the road in world space also
    // touches it in the retarded-position image; rendering karts with v=0
    // (as this used to do for non-observer karts) instead displaces them
    // relative to the road by ~v*d/c.
    const unsigned num_karts = world->getNumKarts();
    for (unsigned i = 0; i < num_karts; i++)
    {
        AbstractKart* abstract_kart = world->getKart(i);
        if (!abstract_kart)
            continue;
        scene::ISceneNode* root = abstract_kart->getNode();
        if (!root)
            continue;
        core::vector3df visual_velocity(0.0f, 0.0f, 0.0f);
        Kart* kart = dynamic_cast<Kart*>(abstract_kart);
        if (kart)
        {
            const btVector3& v =
                kart->getRelativisticState().m_coordinate_velocity;
            visual_velocity = core::vector3df(
                (float)v.x(), (float)v.y(), (float)v.z());
            if (!isFiniteVector(visual_velocity))
                visual_velocity = core::vector3df(0.0f, 0.0f, 0.0f);
        }
        g_kart_root_velocities[root] = visual_velocity;
    }
}   // updateRelativityKartVelocities

// ----------------------------------------------------------------------------
/** Fills out[0..2] with the world-space velocity to use for relativistic
 *  warping of the given scene node, and out[3] with the "disable relativity
 *  visuals" flag (1.0 = do not warp, used for materials flagged
 *  no-relativity-warp, e.g. huge water sheets).
 *  Registered as GE::setNodeVelocityFunction so the Vulkan draw call fills
 *  the same per-object data the SP instance buffer carries under OpenGL. */
void fillNodeRelativityVelocity(const irr::scene::ISceneNode* node,
                                const irr::video::SMaterial* irr_material,
                                float* out)
{
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    if (!Relativity::isEnabled() || !node)
        return;
    core::vector3df velocity;
    // Prefer a true physical velocity (kart coordinate velocity, then explicit
    // flyable/prop override); only fall back to the graphics-delta estimator
    // for nodes that have neither.
    if (!findKartVelocityForNode(node, velocity) &&
        !findNodeOverrideVelocity(node, velocity))
    {
        const core::matrix4& model_matrix =
            node->getAbsoluteTransformation();
        const core::vector3df position(model_matrix[12], model_matrix[13],
                                       model_matrix[14]);
        velocity = estimateNodeVelocity(node, position);
    }
    out[0] = velocity.X;
    out[1] = velocity.Y;
    out[2] = velocity.Z;
    bool disable_relativity_visual =
        shouldDisableRelativityVisualsForNode(node);
    // Same per-mesh-buffer exemption the SP/OpenGL instance buffer applies
    // via Material::isNoRelativityWarp (see uploadAll).
    if (!disable_relativity_visual && irr_material)
        disable_relativity_visual = materialHasNoRelativityWarp(irr_material);
    out[3] = disable_relativity_visual ? 1.0f : 0.0f;
}   // fillNodeRelativityVelocity

// ----------------------------------------------------------------------------
/** Registers/refreshes a node's true physical velocity so the relativistic
 *  warp uses it (like a kart's) instead of the noisy graphics-delta estimator.
 *  Call each frame with the current world-space velocity. */
void setNodeRelativityVelocity(const irr::scene::ISceneNode* node,
                               const irr::core::vector3df& velocity)
{
    if (node)
        g_node_velocity_overrides[node] = velocity;
}   // setNodeRelativityVelocity

// ----------------------------------------------------------------------------
void clearNodeRelativityVelocity(const irr::scene::ISceneNode* node)
{
    if (node)
        g_node_velocity_overrides.erase(node);
}   // clearNodeRelativityVelocity

// ----------------------------------------------------------------------------
ShaderBasedRenderer* g_stk_sbr = NULL;
// ----------------------------------------------------------------------------
std::array<float, 16>* g_joint_ptr = NULL;
// ----------------------------------------------------------------------------
// Wormhole world position for gravitational lensing in tonemap.frag. Set by
// the Wormhole flyable while alive; cleared on destruction.
irr::core::vector3df sp_wormhole_world_pos(0.0f, 0.0f, 0.0f);
bool sp_wormhole_active = false;
float sp_wormhole_radius = 0.0f;
// ----------------------------------------------------------------------------
bool sp_culling = true;
// ----------------------------------------------------------------------------
bool sp_apitrace = false;
// ----------------------------------------------------------------------------
bool sp_debug_view = false;
// ----------------------------------------------------------------------------
bool g_handle_shadow = false;
// ----------------------------------------------------------------------------
SPShader* g_normal_visualizer = NULL;
// ----------------------------------------------------------------------------
SPShader* g_glow_shader = NULL;
// ----------------------------------------------------------------------------
// std::string is layer_1 and layer_2 texture name combined
typedef std::unordered_map<SPShader*, std::unordered_map<std::string,
    std::unordered_set<SPMeshBuffer*> > > DrawCall;

DrawCall g_draw_calls[DCT_FOR_VAO];
// ----------------------------------------------------------------------------
std::vector<std::pair<SPShader*, std::vector<std::pair<std::array<GLuint, 6>,
    std::vector<std::pair<SPMeshBuffer*, int/*material_id*/> > > > > >
    g_final_draw_calls[DCT_FOR_VAO];
// ----------------------------------------------------------------------------
std::unordered_map<unsigned, std::pair<core::vector3df,
    std::unordered_set<SPMeshBuffer*> > > g_glow_meshes;
// ----------------------------------------------------------------------------
std::unordered_set<SPMeshBuffer*> g_instances;
// ----------------------------------------------------------------------------
std::array<GLuint, ST_COUNT> g_samplers = {{ }};
// ----------------------------------------------------------------------------
// Check sp_shader.cpp for the name
std::array<GLuint, 1> sp_prefilled_tex;
// ----------------------------------------------------------------------------
std::atomic<uint32_t> sp_max_texture_size(2048);
// ----------------------------------------------------------------------------
std::vector<float> g_bounding_boxes;
// ----------------------------------------------------------------------------
std::vector<std::shared_ptr<SPDynamicDrawCall> > g_dy_dc;
// ----------------------------------------------------------------------------
float g_frustums[5][24] = { { } };
// ----------------------------------------------------------------------------
unsigned sp_solid_poly_count = 0;
// ----------------------------------------------------------------------------
unsigned sp_shadow_poly_count = 0;
// ----------------------------------------------------------------------------
unsigned sp_cur_player = 0;
// ----------------------------------------------------------------------------
unsigned sp_cur_buf_id[MAX_PLAYER_COUNT] = {};
// ----------------------------------------------------------------------------
unsigned g_skinning_offset = 0;
// ----------------------------------------------------------------------------
std::vector<SPMeshNode*> g_skinning_mesh;
// ----------------------------------------------------------------------------
bool g_skinning_use_tbo = false;
// ----------------------------------------------------------------------------
int sp_cur_shadow_cascade = 0;
// ----------------------------------------------------------------------------
void initSTKRenderer(ShaderBasedRenderer* sbr)
{
    g_stk_sbr = sbr;
}   // initSTKRenderer
// ----------------------------------------------------------------------------
GLuint sp_mat_ubo[MAX_PLAYER_COUNT][3] = {};
// ----------------------------------------------------------------------------
GLuint sp_fog_ubo = 0;
// ----------------------------------------------------------------------------
core::vector3df sp_wind_dir;
// ----------------------------------------------------------------------------
GLuint g_skinning_tex;
// ----------------------------------------------------------------------------
GLuint g_skinning_buf;
// ----------------------------------------------------------------------------
unsigned g_skinning_size;
// ----------------------------------------------------------------------------
ShaderBasedRenderer* getRenderer()
{
    return g_stk_sbr;
}   // getRenderer

// ----------------------------------------------------------------------------
void displaceShaderInit(SPShader* shader)
{
    shader->addShaderFile("sp_pass.vert", GL_VERTEX_SHADER, RP_1ST);
    shader->addShaderFile("sp_displace_ssr.frag", GL_FRAGMENT_SHADER, RP_1ST);
    shader->linkShaderFiles(RP_1ST);
    shader->use(RP_1ST);
    shader->addBasicUniforms(RP_1ST);
    shader->addAllUniforms(RP_1ST);
    shader->setUseFunction([]()->void
        {
            assert(g_stk_sbr->getRTTs() != NULL);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glDisable(GL_CULL_FACE);
            glDisable(GL_BLEND);
            glClear(GL_STENCIL_BUFFER_BIT);
            glEnable(GL_STENCIL_TEST);
            glStencilFunc(GL_ALWAYS, 1, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
            g_stk_sbr->getRTTs()->getFBO(FBO_DISPLACE_SSR).bind(),
            glClear(GL_COLOR_BUFFER_BIT);
        }, RP_1ST);
    shader->addCustomPrefilledTextures(ST_BILINEAR,
        GL_TEXTURE_2D, "u_displace_color", []()->GLuint
        {
            return g_stk_sbr->getRTTs()->getFBO(FBO_COLORS).getRTT()[0];
        }, RP_1ST);
    shader->addCustomPrefilledTextures(ST_SHADOW,
        GL_TEXTURE_2D, "u_depth", []()->GLuint
        {
            return g_stk_sbr->getRTTs()->getDepthStencilTexture();
        }, RP_1ST);
    shader->addCustomPrefilledTextures(ST_TRILINEAR_CLAMPED,
        GL_TEXTURE_CUBE_MAP, "u_skybox_texture", []()->GLuint
        {
            return g_stk_sbr->getSkybox() ?
                g_stk_sbr->getSkybox()->getCubeMap() : 0;
        }, RP_1ST);
    shader->addShaderFile("sp_pass.vert", GL_VERTEX_SHADER, RP_RESERVED);
    shader->addShaderFile("sp_displace.frag", GL_FRAGMENT_SHADER, RP_RESERVED);
    shader->linkShaderFiles(RP_RESERVED);
    shader->use(RP_RESERVED);
    shader->addBasicUniforms(RP_RESERVED);
    shader->addAllUniforms(RP_RESERVED);
    shader->setUseFunction([]()->void
        {
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glDisable(GL_CULL_FACE);
            glDisable(GL_BLEND);
            glEnable(GL_STENCIL_TEST);
            glStencilFunc(GL_ALWAYS, 1, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
            g_stk_sbr->getRTTs()->getFBO(FBO_TMP1_WITH_DS).bind(),
            glClear(GL_COLOR_BUFFER_BIT);
        }, RP_RESERVED);
    SPShaderManager::addPrefilledTexturesToShader(shader,
        {std::make_tuple("displacement_tex", "displace.png", false/*srgb*/,
        ST_BILINEAR)}, RP_RESERVED);
    shader->addCustomPrefilledTextures(ST_BILINEAR,
        GL_TEXTURE_2D, "mask_tex", []()->GLuint
        {
            return g_stk_sbr->getRTTs()->getFBO(FBO_DISPLACE_SSR).getRTT()[0];
        }, RP_RESERVED);
    shader->addCustomPrefilledTextures(ST_BILINEAR,
        GL_TEXTURE_2D, "color_tex", []()->GLuint
        {
            return g_stk_sbr->getRTTs()->getFBO(FBO_COLORS).getRTT()[0];
        }, RP_RESERVED);
    shader->addCustomPrefilledTextures(ST_BILINEAR,
        GL_TEXTURE_2D, "ssr_tex", []()->GLuint
        {
            auto& r = g_stk_sbr->getRTTs()->getFBO(FBO_DISPLACE_SSR).getRTT();
            if (r.size() > 1)
                return r[1];
            return 0;
        }, RP_RESERVED);
    shader->addAllTextures(RP_RESERVED);
    shader->setUnuseFunction([]()->void
        {
            g_stk_sbr->getRTTs()->getFBO(FBO_COLORS).bind();
            glStencilFunc(GL_EQUAL, 1, 0xFF);
            g_stk_sbr->getPostProcessing()->renderPassThrough
                (g_stk_sbr->getRTTs()->getFBO(FBO_TMP1_WITH_DS).getRTT()[0],
                g_stk_sbr->getRTTs()->getFBO(FBO_COLORS).getWidth(),
                g_stk_sbr->getRTTs()->getFBO(FBO_COLORS).getHeight());
            glDisable(GL_STENCIL_TEST);
        }, RP_RESERVED);
    static_cast<SPPerObjectUniform*>(shader)
        ->addAssignerFunction("direction", [](SP::SPUniformAssigner* ua)->void
        {
            ua->setValue(GE::getDisplaceDirection());
        });
    static_cast<SPPerObjectUniform*>(shader)
        ->addAssignerFunction("u_ssr", [](SP::SPUniformAssigner* ua)->void
        {
            ua->setValue(GE::getGEConfig()->m_screen_space_reflection_type !=
                GE::GSSRT_DISABLED ? 1 : 0);
        });
}   // displaceShaderInit

// ----------------------------------------------------------------------------
void resizeSkinning(unsigned number)
{
    const irr::core::matrix4 m;
    g_skinning_size = number;

    if (!skinningUseTBO())
    {
        glBindTexture(GL_TEXTURE_2D, g_skinning_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 4, number, 0, GL_RGBA,
            GL_FLOAT, NULL);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 1, GL_RGBA, GL_FLOAT,
            m.pointer());
        glBindTexture(GL_TEXTURE_2D, 0);
        static std::vector<std::array<float, 16> >
            tmp_buf(stk_config->m_max_skinning_bones);
        g_joint_ptr = tmp_buf.data();
    }
    else
    {
#ifndef USE_GLES2
        glBindBuffer(GL_TEXTURE_BUFFER, g_skinning_buf);
        if (CVS->isARBBufferStorageUsable())
        {
            glBufferStorage(GL_TEXTURE_BUFFER, number << 6, NULL,
                GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
            g_joint_ptr = (std::array<float, 16>*)glMapBufferRange(
                GL_TEXTURE_BUFFER, 0, 64,
                GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
            if (g_joint_ptr)
                memcpy(g_joint_ptr, m.pointer(), 64);
            glUnmapBuffer(GL_TEXTURE_BUFFER);
            g_joint_ptr = (std::array<float, 16>*)glMapBufferRange(
                GL_TEXTURE_BUFFER, 64, (number - 1) << 6,
                GL_MAP_WRITE_BIT | GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT);
        }
        else
        {
            glBufferData(GL_TEXTURE_BUFFER, number << 6, NULL, GL_DYNAMIC_DRAW);
            glBufferSubData(GL_TEXTURE_BUFFER, 0, 64, m.pointer());
        }
        glBindTexture(GL_TEXTURE_BUFFER, g_skinning_tex);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, g_skinning_buf);
        glBindTexture(GL_TEXTURE_BUFFER, 0);
#endif
    }

}   // resizeSkinning

// ----------------------------------------------------------------------------
void initSkinning()
{
    static_assert(sizeof(std::array<float, 16>) == 64, "No padding");

    int max_size = 0;

    if (!skinningUseTBO())
    {
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
    
        if (stk_config->m_max_skinning_bones > (unsigned)max_size)
        {
            Log::warn("SharedGPUObjects", "Too many bones for skinning, max: %d",
                      max_size);
            stk_config->m_max_skinning_bones = max_size;
        }
        Log::info("SharedGPUObjects", "Hardware Skinning enabled, method: %u"
                  " (max bones) * 16 RGBA float texture",
                  stk_config->m_max_skinning_bones);
    }
    else
    {
#ifndef USE_GLES2
        int skinning_tbo_limit;
        glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &skinning_tbo_limit);
        if (stk_config->m_max_skinning_bones << 6 > (unsigned)skinning_tbo_limit)
        {
            Log::warn("SharedGPUObjects", "Too many bones for skinning, max: %d",
                      skinning_tbo_limit >> 6);
            stk_config->m_max_skinning_bones = skinning_tbo_limit >> 6;
        }
        Log::info("SharedGPUObjects", "Hardware Skinning enabled, method: TBO, "
                  "max bones: %u", stk_config->m_max_skinning_bones);
#endif
    }


    // Reserve 1 identity matrix for non-weighted vertices
    // All buffer / skinning texture start with 2 bones for power of 2 increase
    const irr::core::matrix4 m;
    glGenTextures(1, &g_skinning_tex);
#ifndef USE_GLES2
    if (skinningUseTBO())
    {
        glGenBuffers(1, &g_skinning_buf);
    }
#endif
    resizeSkinning(stk_config->m_max_skinning_bones);

    sp_prefilled_tex[0] = g_skinning_tex;
}   // initSkinning

// ----------------------------------------------------------------------------
void loadShaders()
{
    SPShaderManager::get()->loadSPShaders(file_manager->getShadersDir());

    // Displace shader is not specifiable in XML due to complex callback
    std::shared_ptr<SPShader> sps;
    if (CVS->isDeferredEnabled())
    {
        // This displace shader will be drawn the last in transparent pass
        sps = std::make_shared<SPShader>("displace", displaceShaderInit,
            true/*transparent_shader*/, 999/*drawing_priority*/,
            true/*use_alpha_channel*/);
        SPShaderManager::get()->addSPShader(sps->getName(), sps);
    }
    else
    {
        // Fallback shader
        SPShaderManager::get()->addSPShader("displace",
            SPShaderManager::get()->getSPShader("alphablend"));
    }

    // ========================================================================
    // Glow shader
    // ========================================================================
    if (CVS->isDeferredEnabled())
    {
        sps = std::make_shared<SPShader>
            ("sp_glow_shader", [](SPShader* shader)
            {
                shader->addShaderFile("sp_pass.vert", GL_VERTEX_SHADER,
                    RP_1ST);
                shader->addShaderFile("colorize.frag", GL_FRAGMENT_SHADER,
                    RP_1ST);
                shader->linkShaderFiles(RP_1ST);
                shader->use(RP_1ST);
                shader->addBasicUniforms(RP_1ST);
                shader->addAllUniforms(RP_1ST);
            });
        SPShaderManager::get()->addSPShader(sps->getName(), sps);
        g_glow_shader = sps.get();

        // ====================================================================
        // Normal visualizer
        // ====================================================================
#ifndef USE_GLES2
        if (CVS->isARBGeometryShadersUsable())
        {
            sps = std::make_shared<SPShader>
                ("sp_normal_visualizer", [](SPShader* shader)
                {
                    shader->addShaderFile("sp_normal_visualizer.vert",
                        GL_VERTEX_SHADER, RP_1ST);
                    shader->addShaderFile("sp_normal_visualizer.geom",
                        GL_GEOMETRY_SHADER, RP_1ST);
                    shader->addShaderFile("sp_normal_visualizer.frag",
                        GL_FRAGMENT_SHADER, RP_1ST);
                    shader->linkShaderFiles(RP_1ST);
                    shader->use(RP_1ST);
                    shader->addBasicUniforms(RP_1ST);
                    shader->addAllUniforms(RP_1ST);
                    shader->addAllTextures(RP_1ST);
                });
            SPShaderManager::get()->addSPShader(sps->getName(), sps);
            g_normal_visualizer = sps.get();
        }
#endif
    }

    SPShaderManager::get()->setOfficialShaders();

}   // loadShaders

// ----------------------------------------------------------------------------
void resetEmptyFogColor()
{
    if (GUIEngine::isNoGraphics())
    {
        return;
    }
    glBindBuffer(GL_UNIFORM_BUFFER, sp_fog_ubo);
    std::vector<float> fog_empty;
    fog_empty.resize(8, 0.0f);
    glBufferData(GL_UNIFORM_BUFFER, 8 * sizeof(float), fog_empty.data(),
        GL_DYNAMIC_DRAW);
}   // resetEmptyFogColor

// ----------------------------------------------------------------------------
void init()
{
    if (GUIEngine::isNoGraphics())
    {
        return;
    }

    if (CVS->isARBTextureBufferObjectUsable())
    {
        int skinning_tbo_limit;
        glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE_ARB, &skinning_tbo_limit);
        g_skinning_use_tbo = (unsigned)skinning_tbo_limit >= stk_config->m_max_skinning_bones << 6;
    }
    else
    {
        g_skinning_use_tbo = false;
    }

    initSkinning();
    for (unsigned i = 0; i < MAX_PLAYER_COUNT; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            glGenBuffers(1, &sp_mat_ubo[i][j]);
            glBindBuffer(GL_UNIFORM_BUFFER, sp_mat_ubo[i][j]);
            glBufferData(GL_UNIFORM_BUFFER,
                SP_MATRIX_UBO_FLOATS * sizeof(float), NULL, GL_DYNAMIC_DRAW);
        }
    }

    glGenBuffers(1, &sp_fog_ubo);
    resetEmptyFogColor();
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, sp_fog_ubo);

    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    initSamplers();
}   // init

// ----------------------------------------------------------------------------
void initSamplers()
{
    if (std::all_of(g_samplers.begin(), g_samplers.end() - 1,
        [](int value) { return value != 0; }))
    {
        glDeleteSamplers((unsigned)g_samplers.size() - 1, g_samplers.data());
        g_samplers.fill(0);
    }

    for (unsigned st = ST_NEAREST; st < ST_COUNT; st++)
    {
        if ((SamplerType)st == ST_TEXTURE_BUFFER)
        {
            g_samplers[ST_TEXTURE_BUFFER] = 0;
            continue;
        }
        switch ((SamplerType)st)
        {
            case ST_NEAREST:
            {
                unsigned id;
                glGenSamplers(1, &id);
                glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glSamplerParameteri(id, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glSamplerParameteri(id, GL_TEXTURE_WRAP_T, GL_REPEAT);
                if (CVS->isEXTTextureFilterAnisotropicUsable())
                    glSamplerParameterf(id, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.);
                g_samplers[ST_NEAREST] = id;
                break;
            }
            case ST_NEAREST_CLAMPED:
            {
                unsigned id;
                glGenSamplers(1, &id);
                glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glSamplerParameteri(id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glSamplerParameteri(id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                if (CVS->isEXTTextureFilterAnisotropicUsable())
                    glSamplerParameterf(id, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.);
                g_samplers[ST_NEAREST_CLAMPED] = id;
                break;
            }
            case ST_TRILINEAR:
            {
                unsigned id;
                glGenSamplers(1, &id);
                glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glSamplerParameteri(id,
                    GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glSamplerParameteri(id, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glSamplerParameteri(id, GL_TEXTURE_WRAP_T, GL_REPEAT);
                if (CVS->isEXTTextureFilterAnisotropicUsable())
                {
                    int aniso = UserConfigParams::m_anisotropic;
                    if (aniso == 0) aniso = 1;
                    glSamplerParameterf(id, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                        (float)aniso);
                }
                g_samplers[ST_TRILINEAR] = id;
                break;
            }
            case ST_TRILINEAR_CLAMPED:
            {
                unsigned id;
                glGenSamplers(1, &id);
                glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glSamplerParameteri(id,
                    GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glSamplerParameteri(id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glSamplerParameteri(id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glSamplerParameteri(id, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
                if (CVS->isEXTTextureFilterAnisotropicUsable())
                {
                    int aniso = UserConfigParams::m_anisotropic;
                    if (aniso == 0) aniso = 1;
                    glSamplerParameterf(id, GL_TEXTURE_MAX_ANISOTROPY_EXT,
                        (float)aniso);
                }
                g_samplers[ST_TRILINEAR_CLAMPED] = id;
                break;
            }
            case ST_BILINEAR:
            {
                unsigned id;
                glGenSamplers(1, &id);
                glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glSamplerParameteri(id, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glSamplerParameteri(id, GL_TEXTURE_WRAP_T, GL_REPEAT);
                if (CVS->isEXTTextureFilterAnisotropicUsable())
                    glSamplerParameterf(id, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.);
                g_samplers[ST_BILINEAR] = id;
                break;
            }
            case ST_BILINEAR_CLAMPED:
            {
                unsigned id;
                glGenSamplers(1, &id);
                glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glSamplerParameteri(id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glSamplerParameteri(id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                if (CVS->isEXTTextureFilterAnisotropicUsable())
                    glSamplerParameterf(id, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.);
                g_samplers[ST_BILINEAR_CLAMPED] = id;
                break;
            }
            case ST_SEMI_TRILINEAR:
            {
                unsigned id;
                glGenSamplers(1, &id);
                glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_NEAREST);
                glSamplerParameteri(id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glSamplerParameteri(id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                if (CVS->isEXTTextureFilterAnisotropicUsable())
                    glSamplerParameterf(id, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.);
                g_samplers[ST_SEMI_TRILINEAR] = id;
                break;
            }
            case ST_SHADOW:
            {
                unsigned id;
                glGenSamplers(1, &id);
                glSamplerParameteri(id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glSamplerParameteri(id, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
                glSamplerParameteri(id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glSamplerParameteri(id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glSamplerParameterf(id, GL_TEXTURE_COMPARE_MODE,
                    GL_COMPARE_REF_TO_TEXTURE);
                glSamplerParameterf(id, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
                g_samplers[ST_SHADOW] = id;
                break;
            }
            default:
                break;
        }
    }
}   // initSamplers

// ----------------------------------------------------------------------------
void destroy()
{
    g_dy_dc.clear();
    SPTextureManager::get()->stopThreads();
    SPShaderManager::destroy();
    g_glow_shader = NULL;
    g_normal_visualizer = NULL;
    SPTextureManager::destroy();

#ifndef USE_GLES2
    if (skinningUseTBO() &&
        CVS->isARBBufferStorageUsable())
    {
        glBindBuffer(GL_TEXTURE_BUFFER, g_skinning_buf);
        glUnmapBuffer(GL_TEXTURE_BUFFER);
        glBindBuffer(GL_TEXTURE_BUFFER, 0);
    }
    glDeleteBuffers(1, &g_skinning_buf);
#endif
    glDeleteTextures(1, &g_skinning_tex);

    for (unsigned i = 0; i < MAX_PLAYER_COUNT; i++)
    {
        for (int j = 0; j < 3; j++)
        {
            glDeleteBuffers(1, &sp_mat_ubo[i][j]);
        }
    }
    glDeleteBuffers(1, &sp_fog_ubo);
    glDeleteSamplers((unsigned)g_samplers.size() - 1, g_samplers.data());
    g_samplers.fill(0);
}   // destroy

// ----------------------------------------------------------------------------
GLuint getSampler(SamplerType st)
{
    assert(st < ST_COUNT);
    return g_samplers[st];
}   // getSampler

// ----------------------------------------------------------------------------
SPShader* getGlowShader()
{
    return g_glow_shader;
}   // getGlowShader

// ----------------------------------------------------------------------------
SPShader* getNormalVisualizer()
{
    return g_normal_visualizer;
}   // getNormalVisualizer

// ----------------------------------------------------------------------------
bool skinningUseTBO()
{
    return g_skinning_use_tbo;
}


// ----------------------------------------------------------------------------
inline core::vector3df getCorner(const core::aabbox3df& bbox, unsigned n)
{
    switch (n)
    {
    case 0:
        return irr::core::vector3df(bbox.MinEdge.X, bbox.MinEdge.Y,
        bbox.MinEdge.Z);
    case 1:
        return irr::core::vector3df(bbox.MaxEdge.X, bbox.MinEdge.Y,
        bbox.MinEdge.Z);
    case 2:
        return irr::core::vector3df(bbox.MinEdge.X, bbox.MaxEdge.Y,
        bbox.MinEdge.Z);
    case 3:
        return irr::core::vector3df(bbox.MaxEdge.X, bbox.MaxEdge.Y,
        bbox.MinEdge.Z);
    case 4:
        return irr::core::vector3df(bbox.MinEdge.X, bbox.MinEdge.Y,
        bbox.MaxEdge.Z);
    case 5:
        return irr::core::vector3df(bbox.MaxEdge.X, bbox.MinEdge.Y,
        bbox.MaxEdge.Z);
    case 6:
        return irr::core::vector3df(bbox.MinEdge.X, bbox.MaxEdge.Y,
        bbox.MaxEdge.Z);
    case 7:
        return irr::core::vector3df(bbox.MaxEdge.X, bbox.MaxEdge.Y,
        bbox.MaxEdge.Z);
    default:
        assert(false);
        return irr::core::vector3df(0);
    }
}   // getCorner

// ----------------------------------------------------------------------------
void addEdgeForViz(const core::vector3df& p0, const core::vector3df& p1)
{
    g_bounding_boxes.push_back(p0.X);
    g_bounding_boxes.push_back(p0.Y);
    g_bounding_boxes.push_back(p0.Z);
    g_bounding_boxes.push_back(p1.X);
    g_bounding_boxes.push_back(p1.Y);
    g_bounding_boxes.push_back(p1.Z);
}   // addEdgeForViz

// ----------------------------------------------------------------------------
void prepareDrawCalls()
{
    g_bounding_boxes.clear();
    sp_wind_dir = core::vector3df(1.0f, 0.0f, 0.0f) *
        (irr_driver->getDevice()->getTimer()->getTime() / 1000.0f) * 1.5f;
    sp_solid_poly_count = sp_shadow_poly_count = 0;
    // 1st one is identity
    g_skinning_offset = 1;
    g_skinning_mesh.clear();
    mathPlaneFrustumf(g_frustums[0], irr_driver->getProjViewMatrix());
    g_handle_shadow = Track::getCurrentTrack() &&
        Track::getCurrentTrack()->hasShadows() && CVS->isDeferredEnabled() &&
        CVS->isShadowEnabled();

    if (g_handle_shadow)
    {
        mathPlaneFrustumf(g_frustums[1],
            g_stk_sbr->getShadowMatrices()->getSunOrthoMatrices()[0]);
        mathPlaneFrustumf(g_frustums[2],
            g_stk_sbr->getShadowMatrices()->getSunOrthoMatrices()[1]);
        mathPlaneFrustumf(g_frustums[3],
            g_stk_sbr->getShadowMatrices()->getSunOrthoMatrices()[2]);
        mathPlaneFrustumf(g_frustums[4],
            g_stk_sbr->getShadowMatrices()->getSunOrthoMatrices()[3]);
    }

    for (auto& p : g_draw_calls)
    {
        p.clear();
    }
    for (auto& p : g_final_draw_calls)
    {
        p.clear();
    }
    g_glow_meshes.clear();
    g_instances.clear();

    updateRelativityKartVelocities(sp_cur_player);
}

// ----------------------------------------------------------------------------
void addObject(SPMeshNode* node)
{
    if (node->getSPM() == NULL)
    {
        return;
    }

    const core::matrix4& model_matrix = node->getAbsoluteTransformation();
    const bool disable_relativistic_culling =
        Relativity::isEnabled() || !sp_culling;
    const core::vector3df node_position(model_matrix[12], model_matrix[13],
                                        model_matrix[14]);
    core::vector3df node_velocity;
    // Same lookup order as fillNodeRelativityVelocity (GE/Vulkan): true
    // physical velocities (kart, then flyable/prop override) before falling
    // back to the graphics-delta estimator.
    if (!findKartVelocityForNode(node, node_velocity) &&
        !findNodeOverrideVelocity(node, node_velocity))
    {
        node_velocity = estimateNodeVelocity(node, node_position);
    }
    const bool disable_relativity_visual_base =
        shouldDisableRelativityVisualsForNode(node);
    bool added_for_skinning = false;
    for (unsigned m = 0; m < node->getSPM()->getMeshBufferCount(); m++)
    {
        SPMeshBuffer* mb = node->getSPM()->getSPMeshBuffer(m);
        if (!mb)
        {
            continue;
        }
        const Material* mat = mb->getSTKMaterial();
        const bool disable_relativity_visual =
            disable_relativity_visual_base ||
            (mat && mat->isNoRelativityWarp());
        SPShader* shader = node->getShader(m);
        if (shader == NULL)
        {
            continue;
        }
        core::aabbox3df bb = mb->getBoundingBox();
        model_matrix.transformBoxEx(bb);
        std::vector<bool> discard;
        const bool handle_shadow = node->isInShadowPass() &&
            g_handle_shadow && shader->hasShader(RP_SHADOW);
        discard.resize((handle_shadow ? 5 : 1), false);

        for (int dc_type = 0; dc_type < (handle_shadow ? 5 : 1) &&
             !disable_relativistic_culling; dc_type++)
        {
            for (int i = 0; i < 24; i += 4)
            {
                bool outside = true;
                for (int j = 0; j < 8; j++)
                {
                    const float dist =
                        getCorner(bb, j).X * g_frustums[dc_type][i] +
                        getCorner(bb, j).Y * g_frustums[dc_type][i + 1] +
                        getCorner(bb, j).Z * g_frustums[dc_type][i + 2] +
                        g_frustums[dc_type][i + 3];
                    outside = outside && dist < 0.0f;
                    if (!outside)
                    {
                        break;
                    }
                }
                if (outside)
                {
                    discard[dc_type] = true;
                    break;
                }
            }
        }
        if (handle_shadow ?
            (discard[0] && discard[1] && discard[2] && discard[3] &&
            discard[4]) : discard[0])
        {
            continue;
        }

        if (irr_driver->getBoundingBoxesViz())
        {
            addEdgeForViz(getCorner(bb, 0), getCorner(bb, 1));
            addEdgeForViz(getCorner(bb, 1), getCorner(bb, 5));
            addEdgeForViz(getCorner(bb, 5), getCorner(bb, 4));
            addEdgeForViz(getCorner(bb, 4), getCorner(bb, 0));
            addEdgeForViz(getCorner(bb, 2), getCorner(bb, 3));
            addEdgeForViz(getCorner(bb, 3), getCorner(bb, 7));
            addEdgeForViz(getCorner(bb, 7), getCorner(bb, 6));
            addEdgeForViz(getCorner(bb, 6), getCorner(bb, 2));
            addEdgeForViz(getCorner(bb, 0), getCorner(bb, 2));
            addEdgeForViz(getCorner(bb, 1), getCorner(bb, 3));
            addEdgeForViz(getCorner(bb, 5), getCorner(bb, 7));
            addEdgeForViz(getCorner(bb, 4), getCorner(bb, 6));
        }

        mb->uploadGLMesh();
        // For first frame only need the vbo to be initialized
        if (!added_for_skinning && node->getAnimationState())
        {
            added_for_skinning = true;
            int skinning_offset = g_skinning_offset + node->getTotalJoints();
            if (skinning_offset > int(stk_config->m_max_skinning_bones))
            {
                Log::error("SPBase", "No enough space to render skinned"
                    " mesh %s! Max joints can hold: %d",
                    node->getName(), stk_config->m_max_skinning_bones);
                return;
            }
            node->setSkinningOffset(g_skinning_offset);
            g_skinning_mesh.push_back(node);
            g_skinning_offset = skinning_offset;
        }

        float hue = node->getRenderInfo(m) ?
            node->getRenderInfo(m)->getHue() : 0.0f;
        SPInstancedData id = SPInstancedData
            (node->getAbsoluteTransformation(), node_velocity,
            node->getTextureMatrix(m)[0], node->getTextureMatrix(m)[1], hue,
            (short)node->getSkinningOffset(), disable_relativity_visual);

        for (int dc_type = 0; dc_type < (handle_shadow ? 5 : 1); dc_type++)
        {
            if (discard[dc_type])
            {
                continue;
            }
            if (dc_type == 0)
            {
                sp_solid_poly_count += mb->getIndexCount() / 3;
            }
            else
            {
                sp_shadow_poly_count += mb->getIndexCount() / 3;
            }
            if (shader->isTransparent())
            {
                // Transparent shader should always uses mesh samplers
                // All transparent draw calls go DCT_TRANSPARENT
                if (dc_type == 0)
                {
                    auto& ret = g_draw_calls[DCT_TRANSPARENT][shader];
                    for (auto& p : mb->getTextureCompare())
                    {
                        ret[p.first].insert(mb);
                    }
                    mb->addInstanceData(id, DCT_TRANSPARENT);
                }
                else
                {
                    continue;
                }
            }
            else
            {
                // Check if shader for render pass uses mesh samplers
                const RenderPass check_pass =
                    dc_type == DCT_NORMAL ? RP_1ST : RP_SHADOW;
                const bool sampler_less = shader->samplerLess(check_pass);
                auto& ret = g_draw_calls[dc_type][shader];
                if (sampler_less)
                {
                    ret[""].insert(mb);
                }
                else
                {
                    for (auto& p : mb->getTextureCompare())
                    {
                        ret[p.first].insert(mb);
                    }
                }
                mb->addInstanceData(id, (DrawCallType)dc_type);
                if (UserConfigParams::m_glow && node->hasGlowColor() &&
                    CVS->isDeferredEnabled() && dc_type == DCT_NORMAL)
                {
                    video::SColorf gc = node->getGlowColor();
                    unsigned key = gc.toSColor().color;
                    auto ret = g_glow_meshes.find(key);
                    if (ret == g_glow_meshes.end())
                    {
                        g_glow_meshes[key] = std::make_pair(
                            core::vector3df(gc.r, gc.g, gc.b),
                            std::unordered_set<SPMeshBuffer*>());
                    }
                    g_glow_meshes.at(key).second.insert(mb);
                }
            }
            g_instances.insert(mb);
        }
    }
}

// ----------------------------------------------------------------------------
void handleDynamicDrawCall()
{
    for (unsigned dc_num = 0; dc_num < g_dy_dc.size(); dc_num++)
    {
        SPDynamicDrawCall* dydc = g_dy_dc[dc_num].get();
        if (!dydc->isRemoving())
        {
            // They need to be updated independent of culling result
            // otherwise some data will be missed if offset update is used
            g_instances.insert(dydc);
        }
        if (!dydc->isVisible() || dydc->notReadyFromDrawing() ||
            dydc->isRemoving())
        {
            continue;
        }

        SPShader* shader = dydc->getShader();
        core::aabbox3df bb = dydc->getBoundingBox();
        dydc->getAbsoluteTransformation().transformBoxEx(bb);
        std::vector<bool> discard;
        const bool handle_shadow =
            g_handle_shadow && shader->hasShader(RP_SHADOW);
        discard.resize((handle_shadow ? 5 : 1), false);
        const bool disable_relativistic_culling =
            Relativity::isEnabled() || !sp_culling;
        for (int dc_type = 0; dc_type < (handle_shadow ? 5 : 1) &&
             !disable_relativistic_culling; dc_type++)
        {
            for (int i = 0; i < 24; i += 4)
            {
                bool outside = true;
                for (int j = 0; j < 8; j++)
                {
                    const float dist =
                        getCorner(bb, j).X * g_frustums[dc_type][i] +
                        getCorner(bb, j).Y * g_frustums[dc_type][i + 1] +
                        getCorner(bb, j).Z * g_frustums[dc_type][i + 2] +
                        g_frustums[dc_type][i + 3];
                    outside = outside && dist < 0.0f;
                    if (!outside)
                    {
                        break;
                    }
                }
                if (outside)
                {
                    discard[dc_type] = true;
                    break;
                }
            }
        }
        if (handle_shadow ?
            (discard[0] && discard[1] && discard[2] && discard[3] &&
            discard[4]) : discard[0])
        {
            continue;
        }

        if (irr_driver->getBoundingBoxesViz())
        {
            addEdgeForViz(getCorner(bb, 0), getCorner(bb, 1));
            addEdgeForViz(getCorner(bb, 1), getCorner(bb, 5));
            addEdgeForViz(getCorner(bb, 5), getCorner(bb, 4));
            addEdgeForViz(getCorner(bb, 4), getCorner(bb, 0));
            addEdgeForViz(getCorner(bb, 2), getCorner(bb, 3));
            addEdgeForViz(getCorner(bb, 3), getCorner(bb, 7));
            addEdgeForViz(getCorner(bb, 7), getCorner(bb, 6));
            addEdgeForViz(getCorner(bb, 6), getCorner(bb, 2));
            addEdgeForViz(getCorner(bb, 0), getCorner(bb, 2));
            addEdgeForViz(getCorner(bb, 1), getCorner(bb, 3));
            addEdgeForViz(getCorner(bb, 5), getCorner(bb, 7));
            addEdgeForViz(getCorner(bb, 4), getCorner(bb, 6));
        }

        for (int dc_type = 0; dc_type < (handle_shadow ? 5 : 1); dc_type++)
        {
            if (discard[dc_type])
            {
                continue;
            }
            if (dc_type == 0)
            {
                sp_solid_poly_count += dydc->getVertexCount();
            }
            else
            {
                sp_shadow_poly_count += dydc->getVertexCount();
            }
            if (shader->isTransparent())
            {
                // Transparent shader should always uses mesh samplers
                // All transparent draw calls go DCT_TRANSPARENT
                if (dc_type == 0)
                {
                    auto& ret = g_draw_calls[DCT_TRANSPARENT][shader];
                    for (auto& p : dydc->getTextureCompare())
                    {
                        ret[p.first].insert(dydc);
                    }
                }
                else
                {
                    continue;
                }
            }
            else
            {
                // Check if shader for render pass uses mesh samplers
                const RenderPass check_pass =
                    dc_type == DCT_NORMAL ? RP_1ST : RP_SHADOW;
                const bool sampler_less = shader->samplerLess(check_pass);
                auto& ret = g_draw_calls[dc_type][shader];
                if (sampler_less)
                {
                    ret[""].insert(dydc);
                }
                else
                {
                    for (auto& p : dydc->getTextureCompare())
                    {
                        ret[p.first].insert(dydc);
                    }
                }
            }
        }
    }
}

// ----------------------------------------------------------------------------
void updateModelMatrix()
{
    // Make sure all textures (with handles) are loaded
    SPTextureManager::get()->checkForGLCommand(true/*before_scene*/);
    if (!sp_culling)
    {
        return;
    }
    irr_driver->setSkinningJoint(g_skinning_offset - 1);

    for (unsigned i = 0; i < DCT_FOR_VAO; i++)
    {
        DrawCall* dc = &g_draw_calls[(DrawCallType)i];
        // Sort dc based on the drawing priority of shaders
        // The larger the drawing priority int, the last it will be drawn
        using DrawCallPair = std::pair<SPShader*,
            std::unordered_map<std::string,
            std::unordered_set<SPMeshBuffer*> > >;
        std::vector<DrawCallPair> sorted_dc;
        for (auto& p : *dc)
        {
            sorted_dc.push_back(p);
        }
        std::sort(sorted_dc.begin(), sorted_dc.end(),
            [](const DrawCallPair& a, const DrawCallPair& b)->bool
            {
                return a.first->getDrawingPriority() <
                    b.first->getDrawingPriority();
            });
        for (unsigned dc = 0; dc < sorted_dc.size(); dc++)
        {
            auto& p = sorted_dc[dc];
            g_final_draw_calls[i].emplace_back(p.first,
            std::vector<std::pair<std::array<GLuint, 6>,
                std::vector<std::pair<SPMeshBuffer*, int> > > >());

            unsigned texture = 0;
            for (auto& q : p.second)
            {
                if (q.second.empty())
                {
                    continue;
                }
                std::array<GLuint, 6> texture_names =
                    {{ 0, 0, 0, 0, 0, 0 }};
                int material_id =
                    (*(q.second.begin()))->getMaterialID(q.first);

                if (material_id != -1)
                {
                    const std::array<std::shared_ptr<SPTexture>, 6>& textures =
                        (*(q.second.begin()))->getSPTexturesByMaterialID
                        (material_id);
                    texture_names =
                        {{
                            textures[0]->getTextureHandler(),
                            textures[1]->getTextureHandler(),
                            textures[2]->getTextureHandler(),
                            textures[3]->getTextureHandler(),
                            textures[4]->getTextureHandler(),
                            textures[5]->getTextureHandler()
                        }};
                }
                g_final_draw_calls[i][dc].second.emplace_back
                    (texture_names,
                    std::vector<std::pair<SPMeshBuffer*, int> >());
                for (SPMeshBuffer* spmb : q.second)
                {
                    g_final_draw_calls[i][dc].second[texture].second.push_back
                        (std::make_pair(spmb, material_id == -1 ?
                        -1 : spmb->getMaterialID(q.first)));
                }
                texture++;
            }
        }
    }
}

// ----------------------------------------------------------------------------
void uploadSkinningMatrices()
{
    if (g_skinning_mesh.empty())
    {
        return;
    }

    unsigned buffer_offset = 0;
#ifndef USE_GLES2
    if (skinningUseTBO() &&
        !CVS->isARBBufferStorageUsable())
    {
        glBindBuffer(GL_TEXTURE_BUFFER, g_skinning_buf);
        g_joint_ptr = (std::array<float, 16>*)
            glMapBufferRange(GL_TEXTURE_BUFFER, 64, (g_skinning_offset - 1) * 64,
            GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT |
            GL_MAP_INVALIDATE_RANGE_BIT);
    }
#endif

    if (g_joint_ptr)
    {
        for (unsigned i = 0; i < g_skinning_mesh.size(); i++)
        {
            memcpy(g_joint_ptr + buffer_offset,
                g_skinning_mesh[i]->getSkinningMatrices(),
                g_skinning_mesh[i]->getTotalJoints() * 64);
            buffer_offset += g_skinning_mesh[i]->getTotalJoints();
        }
    
        if (!skinningUseTBO())
        {
            glBindTexture(GL_TEXTURE_2D, g_skinning_tex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 1, 4, buffer_offset, GL_RGBA,
                GL_FLOAT, g_joint_ptr);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }
    
#ifndef USE_GLES2
    if (skinningUseTBO() &&
        !CVS->isARBBufferStorageUsable())
    {
        glUnmapBuffer(GL_TEXTURE_BUFFER);
        glBindBuffer(GL_TEXTURE_BUFFER, 0);
    }
#endif
}

// ----------------------------------------------------------------------------
void uploadAll()
{
    uploadSkinningMatrices();
    glBindBuffer(GL_UNIFORM_BUFFER,
        sp_mat_ubo[sp_cur_player][sp_cur_buf_id[sp_cur_player]]);
    /*void* ptr = glMapBufferRange(GL_UNIFORM_BUFFER, 0,
        (16 * 9 + 2) * sizeof(float), GL_MAP_WRITE_BIT |
        GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    memcpy(ptr, g_stk_sbr->getShadowMatrices()->getMatricesData(),
        (16 * 9 + 2) * sizeof(float));
    glUnmapBuffer(GL_UNIFORM_BUFFER);*/
    glBufferSubData(GL_UNIFORM_BUFFER, 0,
        SP_MATRIX_UBO_BASE_FLOATS * sizeof(float),
        g_stk_sbr->getShadowMatrices()->getMatricesData());
    const std::array<float, SP_RELATIVITY_UBO_FLOAT_COUNT> relativity_tail =
        buildRelativityUBOTail(sp_cur_player);
    glBufferSubData(GL_UNIFORM_BUFFER,
        SP_RELATIVITY_UBO_FLOAT_OFFSET * sizeof(float),
        SP_RELATIVITY_UBO_FLOAT_COUNT * sizeof(float),
        relativity_tail.data());
    glBindBuffer(GL_UNIFORM_BUFFER, 0);

    // Also feed relativity data into the GE Vulkan camera UBO so that
    // Vulkan shaders get the same relativistic parameters as SP/OpenGL.
    if (GE::getDriver()->getDriverType() == irr::video::EDT_VULKAN)
    {
        auto* cam_node = dynamic_cast<GE::GEVulkanCameraSceneNode*>(
            irr_driver->getSceneManager()->getActiveCamera());
        if (cam_node)
            cam_node->setRelativityData(relativity_tail.data());
    }

    for (SPMeshBuffer* spmb : g_instances)
    {
        spmb->uploadInstanceData();
    }

    g_dy_dc.erase(std::remove_if(g_dy_dc.begin(), g_dy_dc.end(),
        [] (std::shared_ptr<SPDynamicDrawCall> dc)
        {
            return dc->isRemoving();
        }), g_dy_dc.end());
}

// ----------------------------------------------------------------------------
void drawSPDebugView()
{
    if (g_normal_visualizer == NULL)
    {
        return;
    }
    g_normal_visualizer->use();
    g_normal_visualizer->bindPrefilledTextures();
    for (unsigned i = 0; i < g_final_draw_calls[0].size(); i++)
    {
        auto& p = g_final_draw_calls[0][i];
        for (unsigned j = 0; j < p.second.size(); j++)
        {
            for (unsigned k = 0; k < p.second[j].second.size(); k++)
            {
                // Make sure tangents and joints are not drawn undefined
                glVertexAttrib4f(5, 0.0f, 0.0f, 0.0f, 0.0f);
                glVertexAttribI4i(6, 0, 0, 0, 0);
                glVertexAttrib4f(7, 0.0f, 0.0f, 0.0f, 0.0f);
                p.second[j].second[k].first->draw(DCT_NORMAL,
                    -1/*material_id*/);
            }
        }
    }
    for (unsigned i = 0; i < g_final_draw_calls[5].size(); i++)
    {
        auto& p = g_final_draw_calls[5][i];
        for (unsigned j = 0; j < p.second.size(); j++)
        {
            for (unsigned k = 0; k < p.second[j].second.size(); k++)
            {
                // Make sure tangents and joints are not drawn undefined
                glVertexAttrib4f(5, 0.0f, 0.0f, 0.0f, 0.0f);
                glVertexAttribI4i(6, 0, 0, 0, 0);
                glVertexAttrib4f(7, 0.0f, 0.0f, 0.0f, 0.0f);
                p.second[j].second[k].first->draw(DCT_TRANSPARENT,
                    -1/*material_id*/);
            }
        }
    }
    g_normal_visualizer->unuse();
}

// ----------------------------------------------------------------------------
void drawGlow()
{
    if (g_glow_meshes.empty())
    {
        return;
    }
    g_glow_shader->use();
    SPUniformAssigner* glow_color_assigner =
        g_glow_shader->getUniformAssigner("col");
    assert(glow_color_assigner != NULL);
    for (auto& p : g_glow_meshes)
    {
        glow_color_assigner->setValue(p.second.first);
        for (SPMeshBuffer* spmb : p.second.second)
        {
            spmb->draw(DCT_NORMAL, -1/*material_id*/);
        }
    }
    g_glow_shader->unuse();
}

// ----------------------------------------------------------------------------
void draw(RenderPass rp, DrawCallType dct)
{
    std::stringstream profiler_name;
    profiler_name << "SP::Draw " << dct << " with " << rp;
    PROFILER_PUSH_CPU_MARKER(profiler_name.str().c_str(),
        (uint8_t)(float(dct + rp + 2) / float(DCT_FOR_VAO + RP_COUNT) * 255.0f),
        (uint8_t)(float(dct + 1) / (float)DCT_FOR_VAO * 255.0f) ,
        (uint8_t)(float(rp + 1) / (float)RP_COUNT * 255.0f));

    assert(dct < DCT_FOR_VAO);
    for (unsigned i = 0; i < g_final_draw_calls[dct].size(); i++)
    {
        auto& p = g_final_draw_calls[dct][i];
        if (!p.first->hasShader(rp))
        {
            continue;
        }
        p.first->use(rp);
        static std::vector<SPUniformAssigner*> shader_uniforms;
        p.first->setUniformsPerObject(static_cast<SPPerObjectUniform*>
            (p.first), &shader_uniforms, rp);
        p.first->bindPrefilledTextures(rp);
        for (unsigned j = 0; j < p.second.size(); j++)
        {
            p.first->bindTextures(p.second[j].first, rp);
            for (unsigned k = 0; k < p.second[j].second.size(); k++)
            {
                static std::vector<SPUniformAssigner*> draw_call_uniforms;
                p.first->setUniformsPerObject(static_cast<SPPerObjectUniform*>
                    (p.second[j].second[k].first), &draw_call_uniforms, rp);
                p.second[j].second[k].first->draw(dct,
                    p.second[j].second[k].second/*material_id*/);
                if (p.first->getName().rfind("ghost", 0) == 0)
                {
                    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                    glEnable(GL_BLEND);
                    glBlendEquation(GL_FUNC_ADD);
                    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                    glDepthMask(GL_FALSE);
                    p.second[j].second[k].first->draw(dct,
                        p.second[j].second[k].second/*material_id*/);
                    p.first->use(rp);
                }
                for (SPUniformAssigner* ua : draw_call_uniforms)
                {
                    ua->reset();
                }
                draw_call_uniforms.clear();
            }
        }
        for (SPUniformAssigner* ua : shader_uniforms)
        {
            ua->reset();
        }
        shader_uniforms.clear();
        p.first->unuse(rp);
    }
    PROFILER_POP_CPU_MARKER();
}   // draw

// ----------------------------------------------------------------------------
void drawBoundingBoxes()
{
    Shaders::ColoredLine *line = Shaders::ColoredLine::getInstance();
    line->use();
    line->bindVertexArray();
    line->bindBuffer();
    line->setUniforms(irr::video::SColor(255, 255, 0, 0));
    const float *tmp = g_bounding_boxes.data();
    for (unsigned int i = 0; i < g_bounding_boxes.size(); i += 1024 * 6)
    {
        unsigned count = std::min((unsigned)g_bounding_boxes.size() - i,
            (unsigned)1024 * 6);
        glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(float), &tmp[i]);
        glDrawArrays(GL_LINES, 0, count / 3);
    }
}   // drawBoundingBoxes

// ----------------------------------------------------------------------------
void addDynamicDrawCall(std::shared_ptr<SPDynamicDrawCall> dy_dc)
{
    g_dy_dc.push_back(dy_dc);
}   // addDynamicDrawCall

// ----------------------------------------------------------------------------
SPMesh* convertEVTStandard(irr::scene::IMesh* mesh,
                           const irr::video::SColor* color)
{
    SPMesh* spm = new SPMesh();
    Material* material = material_manager->getDefaultSPMaterial("solid");
    for (unsigned i = 0; i < mesh->getMeshBufferCount(); i++)
    {
        std::vector<video::S3DVertexSkinnedMesh> vertices;
        scene::IMeshBuffer* mb = mesh->getMeshBuffer(i);
        if (!mb)
        {
            continue;
        }
        assert(mb->getVertexType() == video::EVT_STANDARD);
        video::S3DVertex* v_ptr = (video::S3DVertex*)mb->getVertices();
        for (unsigned j = 0; j < mb->getVertexCount(); j++)
        {
            video::S3DVertexSkinnedMesh sp;
            sp.m_position = v_ptr[j].Pos;
            sp.m_normal = MiniGLM::compressVector3(v_ptr[j].Normal);
            sp.m_color = color ? *color : v_ptr[j].Color;
            sp.m_all_uvs[0] = MiniGLM::toFloat16(v_ptr[j].TCoords.X);
            sp.m_all_uvs[1] = MiniGLM::toFloat16(v_ptr[j].TCoords.Y);
            vertices.push_back(sp);
        }
        uint16_t* idx_ptr = mb->getIndices();
        std::vector<uint16_t> indices(idx_ptr, idx_ptr + mb->getIndexCount());
        SPMeshBuffer* buffer = new SPMeshBuffer();
        buffer->setSPMVertices(vertices);
        buffer->setIndices(indices);
        buffer->setSTKMaterial(material);
        spm->addSPMeshBuffer(buffer);
    }
    mesh->drop();
    spm->updateBoundingBox();
    return spm;
}   // convertEVTStandard

// ----------------------------------------------------------------------------
void uploadSPM(irr::scene::IMesh* mesh)
{
    if (!CVS->isGLSL())
    {
        return;
    }
    SP::SPMesh* spm = dynamic_cast<SP::SPMesh*>(mesh);
    if (spm)
    {
        for (u32 i = 0; i < spm->getMeshBufferCount(); i++)
        {
            SP::SPMeshBuffer* mb = spm->getSPMeshBuffer(i);
            if (!mb)
            {
                continue;
            }
            mb->uploadGLMesh();
        }
    }
}   // uploadSPM

// ----------------------------------------------------------------------------
void setMaxTextureSize()
{
    const unsigned max =
        (UserConfigParams::m_high_definition_textures & 0x01) == 0 ?
        UserConfigParams::m_max_texture_size : 2048;
    sp_max_texture_size.store(max);
}   // setMaxTextureSize

// ----------------------------------------------------------------------------
void registerAnimatedTrackNode(const scene::ISceneNode* node)
{
    if (node)
        g_animated_track_nodes.insert(node);
}   // registerAnimatedTrackNode

// ----------------------------------------------------------------------------
void unregisterAnimatedTrackNode(const scene::ISceneNode* node)
{
    if (node)
        g_animated_track_nodes.erase(node);
}   // unregisterAnimatedTrackNode

// ----------------------------------------------------------------------------
bool isAnimatedTrackNode(const scene::ISceneNode* node)
{
    if (g_animated_track_nodes.empty())
        return false;
    for (const scene::ISceneNode* cur = node; cur; cur = cur->getParent())
    {
        if (g_animated_track_nodes.count(cur))
            return true;
    }
    return false;
}   // isAnimatedTrackNode

// ----------------------------------------------------------------------------
void resetRelativityNodeCaches()
{
    g_no_warp_texture_cache.clear();
    g_relativity_motion_states.clear();
    g_node_velocity_overrides.clear();
}   // resetRelativityNodeCaches

// ----------------------------------------------------------------------------
// Per-node glow colours for the GE Vulkan renderer (see sp_base.hpp).
std::unordered_map<const scene::ISceneNode*, video::SColorf>
    g_vulkan_glow_nodes;
// ----------------------------------------------------------------------------
void setVulkanNodeGlowColor(const scene::ISceneNode* node,
                            const video::SColorf& color)
{
    if (!node)
        return;
    if (color.r == 0.0f && color.g == 0.0f && color.b == 0.0f)
        g_vulkan_glow_nodes.erase(node);
    else
        g_vulkan_glow_nodes[node] = color;
}   // setVulkanNodeGlowColor

// ----------------------------------------------------------------------------
void clearVulkanGlowNodes()
{
    g_vulkan_glow_nodes.clear();
}   // clearVulkanGlowNodes

// ----------------------------------------------------------------------------
/** Fills out[0..2] with the node's glow colour and out[3] with 1.0 when the
 *  node glows. Registered as GE::setNodeGlowColorFunction so the Vulkan draw
 *  call fills the same per-object data SPMeshNode carries under OpenGL. */
void fillNodeGlowColor(const scene::ISceneNode* node, float* out)
{
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    if (!node || !UserConfigParams::m_glow || g_vulkan_glow_nodes.empty())
        return;
    auto it = g_vulkan_glow_nodes.find(node);
    if (it == g_vulkan_glow_nodes.end())
        return;
    out[0] = it->second.r;
    out[1] = it->second.g;
    out[2] = it->second.b;
    out[3] = 1.0f;
}   // fillNodeGlowColor

// ----------------------------------------------------------------------------
void registerPresentationNode(const scene::ISceneNode* node)
{
    if (node)
        g_presentation_nodes.insert(node);
}   // registerPresentationNode

// ----------------------------------------------------------------------------
void unregisterPresentationNode(const scene::ISceneNode* node)
{
    if (node)
        g_presentation_nodes.erase(node);
}   // unregisterPresentationNode

}

#endif
