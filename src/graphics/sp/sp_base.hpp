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

#ifndef HEADER_SP_BASE_HPP
#define HEADER_SP_BASE_HPP

#include "graphics/gl_headers.hpp"
#include "utils/constants.hpp"
#include "utils/no_copy.hpp"

#include "irrMath.h"
#include "vector3d.h"

#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <ostream>
#include <memory>
#include <string>
#include <vector>

namespace irr
{
    namespace scene { class ICameraSceneNode; class IMesh; class ISceneNode; }
    namespace video { class SColor; class SColorf; class SMaterial; }
}

class ShaderBasedRenderer;

namespace SP
{
class SPMesh;

enum DrawCallType: unsigned int
{
    DCT_NORMAL = 0,
    DCT_SHADOW1,
    DCT_SHADOW2,
    DCT_SHADOW3,
    DCT_SHADOW4,
    DCT_TRANSPARENT,
    DCT_FOR_VAO
};

inline std::ostream& operator<<(std::ostream& os, const DrawCallType& dct)
{
    switch (dct)
    {
        case DCT_NORMAL:
            return os << "normal";
        case DCT_TRANSPARENT:
            return os << "transparent";
        case DCT_SHADOW1:
            return os << "shadow cam 1";
        case DCT_SHADOW2:
            return os << "shadow cam 2";
        case DCT_SHADOW3:
            return os << "shadow cam 3";
        case DCT_SHADOW4:
            return os << "shadow cam 4";
        default:
            return os;
    }
}

enum SamplerType: unsigned int;
enum RenderPass: unsigned int;
class SPDynamicDrawCall;
class SPMaterial;
class SPMeshNode;
class SPShader;
class SPMeshBuffer;

extern GLuint sp_mat_ubo[MAX_PLAYER_COUNT][3];
extern GLuint sp_fog_ubo;
extern std::array<GLuint, 1> sp_prefilled_tex;
extern std::atomic<uint32_t> sp_max_texture_size;
extern unsigned sp_solid_poly_count;
extern unsigned sp_shadow_poly_count;
extern int sp_cur_shadow_cascade;
extern bool sp_culling;
extern bool sp_debug_view;
extern bool sp_apitrace;
extern unsigned sp_cur_player;
extern unsigned sp_cur_buf_id[MAX_PLAYER_COUNT];
extern irr::core::vector3df sp_wind_dir;
extern irr::core::vector3df sp_wormhole_world_pos;
extern bool sp_wormhole_active;
// World-space visual radius of the wormhole mouth currently used for
// lensing. Non-zero only while sp_wormhole_active is true.
extern float sp_wormhole_radius;
// ----------------------------------------------------------------------------
void init();
// ----------------------------------------------------------------------------
void destroy();
// ----------------------------------------------------------------------------
GLuint getSampler(SamplerType);
// ----------------------------------------------------------------------------
SPShader* getNormalVisualizer();
// ----------------------------------------------------------------------------
SPShader* getGlowShader();
// ----------------------------------------------------------------------------
bool skinningUseTBO();
// ----------------------------------------------------------------------------
void prepareDrawCalls();
// ----------------------------------------------------------------------------
// Relativity bridge for the GE (Vulkan) renderer. These let the fixed
// pipeline renderer feed the same relativistic parameters into the GE camera
// UBO / per-object data that the SP pipeline uploads under OpenGL.
std::array<float, 42> getRelativityUBOTail(unsigned player_index);
// ----------------------------------------------------------------------------
// Registers / removes one live black hole for screen-space lensing. Keyed by
// owner so several black holes can lens the screen at the same time.
void setBlackHoleLens(const void* owner, const irr::core::vector3df& pos,
                      float radius);
// ----------------------------------------------------------------------------
void removeBlackHoleLens(const void* owner);
// ----------------------------------------------------------------------------
// Publishes / clears the time-dilation gravitational wave (expanding ring)
// for the screen-space effect in displace_color.frag.
void setGravWave(const irr::core::vector3df& pos, float radius);
// ----------------------------------------------------------------------------
void clearGravWave();
// ----------------------------------------------------------------------------
void updateRelativityKartVelocities(unsigned player_index);
// ----------------------------------------------------------------------------
void fillNodeRelativityVelocity(const irr::scene::ISceneNode* node,
                                const irr::video::SMaterial* irr_material,
                                float* out);
// ----------------------------------------------------------------------------
// Register/refresh a node's true world-space velocity so the relativistic warp
// uses it instead of the graphics-delta estimator (call each frame); clear on
// teardown. Used for flyables (rigid-body velocity) and the raptor props.
void setNodeRelativityVelocity(const irr::scene::ISceneNode* node,
                               const irr::core::vector3df& velocity);
void clearNodeRelativityVelocity(const irr::scene::ISceneNode* node);
// ----------------------------------------------------------------------------
// Per-node glow colours for the GE Vulkan renderer. Under OpenGL the glow
// colour lives on SPMeshNode; under Vulkan the game registers it here and
// the GE draw call queries it via GE::setNodeGlowColorFunction.
void setVulkanNodeGlowColor(const irr::scene::ISceneNode* node,
                            const irr::video::SColorf& color);
// ----------------------------------------------------------------------------
void clearVulkanGlowNodes();
// ----------------------------------------------------------------------------
void fillNodeGlowColor(const irr::scene::ISceneNode* node, float* out);
// ----------------------------------------------------------------------------
void draw(RenderPass, DrawCallType dct = DCT_NORMAL);
// ----------------------------------------------------------------------------
void drawGlow();
// ----------------------------------------------------------------------------
void drawSPDebugView();
// ----------------------------------------------------------------------------
void addObject(SPMeshNode*);
// ----------------------------------------------------------------------------
void initSTKRenderer(ShaderBasedRenderer*);
// ----------------------------------------------------------------------------
void initSamplers();
// ----------------------------------------------------------------------------
void prepareScene();
// ----------------------------------------------------------------------------
void handleDynamicDrawCall();
// ----------------------------------------------------------------------------
void addDynamicDrawCall(std::shared_ptr<SPDynamicDrawCall>);
// ----------------------------------------------------------------------------
void updateModelMatrix();
// ----------------------------------------------------------------------------
void uploadAll();
// ----------------------------------------------------------------------------
void resetEmptyFogColor();
// ----------------------------------------------------------------------------
void drawBoundingBoxes();
// ----------------------------------------------------------------------------
void loadShaders();
// ----------------------------------------------------------------------------
// Register a scene node belonging to an animated track object so that
// estimateNodeVelocity always returns zero for it and all its descendants
// (animated track objects such as balloons have large per-frame Bezier
// deltas that are not real translational velocities, causing stutter in the
// relativistic shader; animated library/LOD objects register their root
// node while the rendered mesh nodes are children of it).
void registerAnimatedTrackNode(const irr::scene::ISceneNode* node);
// ----------------------------------------------------------------------------
void unregisterAnimatedTrackNode(const irr::scene::ISceneNode* node);
// ----------------------------------------------------------------------------
// True if the node or an ancestor is registered as an animated track node,
// i.e. it carries the zero-velocity relativity exemption. Lets per-frame
// physics velocity feeds (PhysicalObject::updateGraphics) skip exempted
// objects instead of re-adding them to the override map each frame.
bool isAnimatedTrackNode(const irr::scene::ISceneNode* node);
// ----------------------------------------------------------------------------
// Register a camera-anchored presentation node (e.g. the start referee) so
// that estimateNodeVelocity returns zero for it and all its descendants:
// its per-frame repositioning is presentation-only, not physical motion.
void registerPresentationNode(const irr::scene::ISceneNode* node);
// ----------------------------------------------------------------------------
void unregisterPresentationNode(const irr::scene::ISceneNode* node);
// ----------------------------------------------------------------------------
// Drops per-texture / per-node relativity caches whose keys (texture and
// scene node pointers) are recycled between tracks. Call on world load.
void resetRelativityNodeCaches();
// ----------------------------------------------------------------------------
SPMesh* convertEVTStandard(irr::scene::IMesh* mesh,
                           const irr::video::SColor* color = NULL);
// ----------------------------------------------------------------------------
void uploadSPM(irr::scene::IMesh* mesh);
// ----------------------------------------------------------------------------
#ifdef SERVER_ONLY
inline void setMaxTextureSize() {}
#else
void setMaxTextureSize();
#endif
// ----------------------------------------------------------------------------
inline void unsetMaxTextureSize()          { sp_max_texture_size.store(2048); }
// ----------------------------------------------------------------------------
inline uint8_t srgbToLinear(float color_srgb)
{
    int ret;
    if (color_srgb <= 0.04045f)
    {
        ret = (int)(255.0f * (color_srgb / 12.92f));
    }
    else
    {
        ret = (int)(255.0f * (powf((color_srgb + 0.055f) / 1.055f, 2.4f)));
    }
    return uint8_t(irr::core::clamp(ret, 0, 255));
}
// ----------------------------------------------------------------------------
inline uint8_t linearToSrgb(float color_linear)
{
    if (color_linear <= 0.0031308f)
    {
        color_linear = color_linear * 12.92f;
    }
    else
    {
        color_linear = 1.055f * powf(color_linear, 1.0f / 2.4f) - 0.055f;
    }
    return uint8_t(irr::core::clamp(int(color_linear * 255.0f), 0, 255));
}
// ----------------------------------------------------------------------------
ShaderBasedRenderer* getRenderer();
}


#endif
