#ifndef HEADER_GE_VULKAN_CAMERA_SCENE_NODE_HPP
#define HEADER_GE_VULKAN_CAMERA_SCENE_NODE_HPP

#include "../source/Irrlicht/CCameraSceneNode.h"

#include <array>
#include <cstring>

namespace GE
{
struct GEVulkanCameraUBO
{
irr::core::matrix4 m_view_matrix;
irr::core::matrix4 m_projection_matrix;
irr::core::matrix4 m_inverse_view_matrix;
irr::core::matrix4 m_inverse_projection_matrix;
irr::core::matrix4 m_projection_view_matrix;
irr::core::matrix4 m_inverse_projection_view_matrix;
irr::core::rectf   m_viewport;
irr::core::rectf   m_screensize;
// Relativistic visual parameters (match buildRelativityUBOTail() layout)
float m_relativity_params[4];       // [item_active, doppler_active, gamma, inv_gamma]
float m_relativity_beta[4];         // [bx, by, bz, c_light]
float m_relativity_observer_pos[4]; // [ox, oy, oz, scanner_active]
float m_relativity_bubble[4];       // [bubble.x, bubble.y, bubble.z, warp_radius]
float m_black_holes[16];            // 4x [wx, wy, wz, radius] (radius=0 = slot inactive)
float m_wormhole[4];                // [wx, wy, wz, radius] (radius=0 = inactive)
float m_grav_wave[4];               // [origin.xyz, radius] (radius<=0 = inactive)
// Screen-space post effect parameters (displace_color.frag)
irr::core::matrix4 m_previous_pv_matrix; // previous frame projection*view
float m_motion_blur[4];             // [boost_amount, center_x, center_y, mask_radius]
float m_compactification[4];        // [strength, 0, 0, 0]
// Track god rays / light shafts (matches the SP/OpenGL renderGodRays sun)
float m_godrays_pos[4];             // [x, y, z, opacity] (opacity=0 = inactive)
float m_godrays_color[4];           // [r, g, b, world_radius]
// Post effect toggles mirroring the SP/OpenGL advanced pipeline options
float m_postfx_flags[4];            // [bloom, ssao, dof, antialias]
// Sun shadow mapping (filled by GEVulkanDrawCall::uploadDynamicData):
// world position -> shadow atlas [uv.xy, depth01] (already bias-mapped).
// Near cascade matrix/params, then the far cascade.
irr::core::matrix4 m_sun_shadow_matrix;
float m_shadow_params[4];           // [depth range (0=off), pcss, texel, penumbra]
// Second post effect toggle block: [glow, scatter_density, 0, 0]
float m_postfx_flags2[4];
irr::core::matrix4 m_sun_shadow_matrix_far;
float m_shadow_params_far[4];       // [depth range, split distance, texel, penumbra]
// Post-processing style knobs: [exposure, saturation, vignette, sharpness]
float m_beauty_params[4];

GEVulkanCameraUBO()
{
    // The float parameter blocks are only refreshed per frame during races;
    // make sure cameras that never get fed (RTT previews, menus) read all
    // effects as disabled instead of stack garbage.
    // Contiguous relativity block: params/beta/observer/bubble (4 vec4 = 16) +
    // black_holes (16) + wormhole (4) + grav_wave (4) = 40 floats.
    memset(m_relativity_params, 0, sizeof(float) * (4 * 4 + 16 + 4 + 4));
    memset(m_motion_blur, 0, sizeof(m_motion_blur));
    memset(m_compactification, 0, sizeof(m_compactification));
    memset(m_godrays_pos, 0, sizeof(m_godrays_pos));
    memset(m_godrays_color, 0, sizeof(m_godrays_color));
    memset(m_postfx_flags, 0, sizeof(m_postfx_flags));
    memset(m_shadow_params, 0, sizeof(m_shadow_params));
    memset(m_postfx_flags2, 0, sizeof(m_postfx_flags2));
    memset(m_shadow_params_far, 0, sizeof(m_shadow_params_far));
    // Sane style defaults for cameras that are never fed per frame
    // (RTT previews, menus)
    m_beauty_params[0] = 2.2f;
    m_beauty_params[1] = 1.06f;
    m_beauty_params[2] = 0.22f;
    m_beauty_params[3] = 0.3f;
}
};

class GEVulkanCameraSceneNode : public irr::scene::CCameraSceneNode
{
private:
    GEVulkanCameraUBO m_ubo_data;

    irr::core::rect<irr::s32> m_viewport;
public:
    // ------------------------------------------------------------------------
    GEVulkanCameraSceneNode(irr::scene::ISceneNode* parent,
                            irr::scene::ISceneManager* mgr, irr::s32 id,
          const irr::core::vector3df& position = irr::core::vector3df(0, 0, 0),
         const irr::core::vector3df& lookat = irr::core::vector3df(0, 0, 100));
    // ------------------------------------------------------------------------
    ~GEVulkanCameraSceneNode();
    // ------------------------------------------------------------------------
    virtual void render();
    // ------------------------------------------------------------------------
    void setViewPort(const irr::core::rect<irr::s32>& area)
                                                         { m_viewport = area; }
    // ------------------------------------------------------------------------
    const irr::core::rect<irr::s32>& getViewPort() const { return m_viewport; }
    // ------------------------------------------------------------------------
    irr::core::matrix4 getPVM() const;
    // ------------------------------------------------------------------------
    const GEVulkanCameraUBO* const getUBOData() const   { return &m_ubo_data; }
    // ------------------------------------------------------------------------
    // Feed the 42-float array produced by buildRelativityUBOTail() into the
    // camera UBO so Vulkan shaders can read the same relativistic parameters
    // that the SP/OpenGL pipeline writes to its own UBO.
    void setRelativityData(const float* tail)
    {
        // tail[2..5]  = relativity_params
        // tail[6..9]  = relativity_beta
        // tail[10..13]= relativity_observer_pos
        // tail[14..17]= relativity_bubble
        // tail[18..33]= black_holes[4]
        // tail[34..37]= wormhole
        // tail[38..41]= grav_wave
        memcpy(m_ubo_data.m_relativity_params,       tail + 2,  16);
        memcpy(m_ubo_data.m_relativity_beta,          tail + 6,  16);
        memcpy(m_ubo_data.m_relativity_observer_pos,  tail + 10, 16);
        memcpy(m_ubo_data.m_relativity_bubble,         tail + 14, 16);
        memcpy(m_ubo_data.m_black_holes,               tail + 18, 64);
        memcpy(m_ubo_data.m_wormhole,                  tail + 34, 16);
        memcpy(m_ubo_data.m_grav_wave,                 tail + 38, 16);
    }
    // ------------------------------------------------------------------------
    // Per-camera screen-space post effect parameters, applied by
    // displace_color.frag: motion blur [boost, center.xy, mask_radius] and
    // compactification [strength, 0, 0, 0].
    void setPostFXData(const float* motion_blur4, const float* compact4,
                       const float* postfx_flags4,
                       const float* postfx_flags2_4 = NULL,
                       const float* beauty_params4 = NULL)
    {
        memcpy(m_ubo_data.m_motion_blur, motion_blur4, 16);
        memcpy(m_ubo_data.m_compactification, compact4, 16);
        memcpy(m_ubo_data.m_postfx_flags, postfx_flags4, 16);
        if (postfx_flags2_4)
            memcpy(m_ubo_data.m_postfx_flags2, postfx_flags2_4, 16);
        if (beauty_params4)
            memcpy(m_ubo_data.m_beauty_params, beauty_params4, 16);
    }
    // ------------------------------------------------------------------------
    // Sun shadow data (near + far cascade), computed per frame by
    // GEVulkanDrawCall before the camera UBO is uploaded.
    void setSunShadowData(const irr::core::matrix4& world_to_shadow_uv,
                          const float* params4,
                          const irr::core::matrix4& world_to_shadow_uv_far,
                          const float* params4_far)
    {
        m_ubo_data.m_sun_shadow_matrix = world_to_shadow_uv;
        memcpy(m_ubo_data.m_shadow_params, params4, 16);
        m_ubo_data.m_sun_shadow_matrix_far = world_to_shadow_uv_far;
        memcpy(m_ubo_data.m_shadow_params_far, params4_far, 16);
    }
    // ------------------------------------------------------------------------
    // Track god rays / light shafts, applied by displace_color.frag.
    // data8 = [pos.xyz, opacity, color.rgb, world_radius]; opacity 0 disables.
    void setGodRaysData(const float* data8)
    {
        memcpy(m_ubo_data.m_godrays_pos, data8, 16);
        memcpy(m_ubo_data.m_godrays_color, data8 + 4, 16);
    }
    // ------------------------------------------------------------------------
    // Stores the current projection*view as last frame's matrix for
    // reprojection-based effects. Call exactly once per frame, before the
    // scene render recomputes the current matrix.
    void updatePreviousPVMatrix()
    {
        m_ubo_data.m_previous_pv_matrix = m_ubo_data.m_projection_view_matrix;
    }
};   // GEVulkanCameraSceneNode

}

#endif
