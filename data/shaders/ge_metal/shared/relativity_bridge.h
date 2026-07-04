// relativity_bridge.h  (MSL port of ge_shaders/utils/relativity_bridge.glsl)
//
// Maps the SP-pipeline standalone uniform names used by the shared relativity
// utility headers (ge_metal/common/relativity_visual.h and
// ge_metal/common/relativity_color.h) onto the fields of the GE CameraBuffer.
//
// In the Vulkan/GLSL pipeline these were plain preprocessor aliases onto a
// std140 uniform block instance named u_camera. MSL has no global uniform
// blocks: the camera UBO is a `constant CameraBuffer&` argument bound at the
// camera buffer index (set=1, binding=0 in GLSL). The relativity helpers take
// that buffer as an explicit `thread const CameraBuffer& u_camera` parameter;
// the field aliases below then resolve against that parameter exactly like the
// GLSL originals resolved against the global block. Usage:
//
//     #include "../shared/ge_metal_bindings.h"
//     #include "../shared/relativity_bridge.h"
//     #include "../common/relativity_visual.h"
//     ...
//     vertex VOut my_vertex(...,
//         constant CameraBuffer& cam [[buffer(GE_MTL_BUF_CAMERA)]])
//     {
//         float4 p = applyRelativisticVisualPosition(cam, world_pos, vel, fade);
//         ...
//     }
//
// No globals are involved, so the math is identical to the GLSL that read
// u_camera directly.

#ifndef GE_METAL_RELATIVITY_BRIDGE_H
#define GE_METAL_RELATIVITY_BRIDGE_H

#include <metal_stdlib>
using namespace metal;

// ---------------------------------------------------------------------------
// CameraBuffer: MSL mirror of the std140 GLSL CameraBuffer block
// (utils/camera.glsl) and the C++ GEVulkanCameraUBO (matches
// buildRelativityUBOTail() layout). Field order/size must stay byte-identical
// to that struct so the same UBO upload feeds both backends.
//
// std140 packs every member here on a 16-byte boundary already (mat4 and vec4
// only), so the natural MSL layout of float4x4/float4 matches without padding.
// ---------------------------------------------------------------------------
struct CameraBuffer
{
    float4x4 m_view_matrix;
    float4x4 m_projection_matrix;
    float4x4 m_inverse_view_matrix;
    float4x4 m_inverse_projection_matrix;
    float4x4 m_projection_view_matrix;
    float4x4 m_inverse_projection_view_matrix;
    float4   m_viewport;
    float2   m_screensize;
    float2   m_padding;
    // Relativistic visual parameters (match buildRelativityUBOTail())
    float4   m_relativity_params;       // [item_active, doppler_active, gamma, inv_gamma]
    float4   m_relativity_beta;         // [bx, by, bz, c_light]
    float4   m_relativity_observer_pos; // [ox, oy, oz, scanner_active]
    float4   m_relativity_bubble;       // [bubble.xyz, warp_radius]
    float4   m_black_holes[4];          // [wx, wy, wz, radius] (radius=0 = slot inactive)
    float4   m_wormhole;                // [wx, wy, wz, radius] (radius=0 = inactive)
    float4   m_grav_wave;               // [origin.xyz, radius] (radius<=0 = inactive)
    // Screen-space post effect parameters
    float4x4 m_previous_pv_matrix;      // previous frame projection*view
    float4   m_motion_blur;             // [boost_amount, center_x, center_y, mask_radius]
    float4   m_compactification;        // [strength, 0, 0, 0]
    float4   m_godrays_pos;             // [x, y, z, opacity] (opacity=0 = inactive)
    float4   m_godrays_color;           // [r, g, b, world_radius]
    float4   m_postfx_flags;            // [bloom, ssao, dof, antialias]
    float4x4 m_sun_shadow_matrix;
    float4   m_shadow_params;           // [depth range (0=off), pcss, texel, penumbra]
    float4   m_postfx_flags2;           // [glow, scatter_density, lens_flare, time_s]
    float4x4 m_sun_shadow_matrix_far;
    float4   m_shadow_params_far;       // [depth range, split distance, texel, penumbra]
    float4   m_beauty_params;           // [exposure, saturation, vignette, sharpness]
};

// ---------------------------------------------------------------------------
// Field aliases. GLSL used e.g. `#define u_relativity_params
// u_camera.m_relativity_params`; those cannot work in MSL because there is no
// implicit global u_camera. Instead the aliases expand to member accesses on a
// caller-supplied reference named `u_camera`. The relativity helper functions
// declare a `thread const CameraBuffer& u_camera` parameter, so within those
// functions the aliases below resolve exactly like the GLSL originals.
// ---------------------------------------------------------------------------
#define u_relativity_params       u_camera.m_relativity_params
#define u_relativity_beta         u_camera.m_relativity_beta
#define u_relativity_observer_pos u_camera.m_relativity_observer_pos
#define u_relativity_bubble       u_camera.m_relativity_bubble
#define u_black_holes             u_camera.m_black_holes
#define u_wormhole                u_camera.m_wormhole

// Matrix aliases used by tonemap/post-process relativity shaders
#define u_projection_view_matrix  u_camera.m_projection_view_matrix
#define u_view_matrix             u_camera.m_view_matrix

// Screen-size alias used by relativity_color.h (dopplerScannerRadius)
#define u_screen                  u_camera.m_screensize

#endif // GE_METAL_RELATIVITY_BRIDGE_H
