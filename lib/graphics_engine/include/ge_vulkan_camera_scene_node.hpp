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
// world position -> shadow map [uv.xy, depth01] (already bias-mapped).
irr::core::matrix4 m_sun_shadow_matrix;
float m_shadow_params[4];           // [enabled, pcss, 1/resolution, penumbra]
// Second post effect toggle block: [glow, scatter_density, 0, 0]
float m_postfx_flags2[4];

GEVulkanCameraUBO()
{
    // The float parameter blocks are only refreshed per frame during races;
    // make sure cameras that never get fed (RTT previews, menus) read all
    // effects as disabled instead of stack garbage.
    memset(m_relativity_params, 0, sizeof(float) * (4 * 5 + 16 + 4));
    memset(m_motion_blur, 0, sizeof(m_motion_blur));
    memset(m_compactification, 0, sizeof(m_compactification));
    memset(m_godrays_pos, 0, sizeof(m_godrays_pos));
    memset(m_godrays_color, 0, sizeof(m_godrays_color));
    memset(m_postfx_flags, 0, sizeof(m_postfx_flags));
    memset(m_shadow_params, 0, sizeof(m_shadow_params));
    memset(m_postfx_flags2, 0, sizeof(m_postfx_flags2));
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
    // Feed the 38-float array produced by buildRelativityUBOTail() into the
    // camera UBO so Vulkan shaders can read the same relativistic parameters
    // that the SP/OpenGL pipeline writes to its own UBO.
    void setRelativityData(const float* tail38)
    {
        // tail[2..5]  = relativity_params
        // tail[6..9]  = relativity_beta
        // tail[10..13]= relativity_observer_pos
        // tail[14..17]= relativity_bubble
        // tail[18..33]= black_holes[4]
        // tail[34..37]= wormhole
        memcpy(m_ubo_data.m_relativity_params,       tail38 + 2,  16);
        memcpy(m_ubo_data.m_relativity_beta,          tail38 + 6,  16);
        memcpy(m_ubo_data.m_relativity_observer_pos,  tail38 + 10, 16);
        memcpy(m_ubo_data.m_relativity_bubble,         tail38 + 14, 16);
        memcpy(m_ubo_data.m_black_holes,               tail38 + 18, 64);
        memcpy(m_ubo_data.m_wormhole,                  tail38 + 34, 16);
    }
    // ------------------------------------------------------------------------
    // Per-camera screen-space post effect parameters, applied by
    // displace_color.frag: motion blur [boost, center.xy, mask_radius] and
    // compactification [strength, 0, 0, 0].
    void setPostFXData(const float* motion_blur4, const float* compact4,
                       const float* postfx_flags4,
                       const float* postfx_flags2_4 = NULL)
    {
        memcpy(m_ubo_data.m_motion_blur, motion_blur4, 16);
        memcpy(m_ubo_data.m_compactification, compact4, 16);
        memcpy(m_ubo_data.m_postfx_flags, postfx_flags4, 16);
        if (postfx_flags2_4)
            memcpy(m_ubo_data.m_postfx_flags2, postfx_flags2_4, 16);
    }
    // ------------------------------------------------------------------------
    // Sun shadow data, computed per frame by GEVulkanDrawCall before the
    // camera UBO is uploaded.
    void setSunShadowData(const irr::core::matrix4& world_to_shadow_uv,
                          const float* params4)
    {
        m_ubo_data.m_sun_shadow_matrix = world_to_shadow_uv;
        memcpy(m_ubo_data.m_shadow_params, params4, 16);
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
