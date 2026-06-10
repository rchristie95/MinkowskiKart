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

GEVulkanCameraUBO()
{
    // The float parameter blocks are only refreshed per frame during races;
    // make sure cameras that never get fed (RTT previews, menus) read all
    // effects as disabled instead of stack garbage.
    memset(m_relativity_params, 0, sizeof(float) * (4 * 5 + 16 + 4));
    memset(m_motion_blur, 0, sizeof(m_motion_blur));
    memset(m_compactification, 0, sizeof(m_compactification));
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
    void setPostFXData(const float* motion_blur4, const float* compact4)
    {
        memcpy(m_ubo_data.m_motion_blur, motion_blur4, 16);
        memcpy(m_ubo_data.m_compactification, compact4, 16);
    }
};   // GEVulkanCameraSceneNode

}

#endif
