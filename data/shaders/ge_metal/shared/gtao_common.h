#ifndef HEADER_GE_METAL_GTAO_COMMON_H
#define HEADER_GE_METAL_GTAO_COMMON_H

// ============================================================================
// MSL port of data/shaders/ge_shaders/utils/gtao_common.glsl
//
// Screen-space GTAO shared helpers: view-space reconstruction from device
// depth, octahedral normal decode, view-normal reconstruction, and the noise
// used for slice/step jitter and temporal reprojection. The reconstruction and
// noise math is numerically identical to the GLSL source; only the resource
// access syntax (texture2d.read / constant struct) differs.
//
// Resource mapping (GEVulkanGTAOPass single descriptor set 0..7 -> Metal):
//   GLSL b0 u_depth        -> texture2d<float> [[texture(GTAO_TEX_DEPTH)]]
//   GLSL b1 u_normal       -> texture2d<float> [[texture(GTAO_TEX_NORMAL)]]
//   GLSL b2 u_linear_depth -> texture2d<float> [[texture(GTAO_TEX_LINEAR_DEPTH)]]
//   GLSL b3 u_input        -> texture2d<float> [[texture(GTAO_TEX_INPUT)]]
//   GLSL b4 u_history      -> texture2d<float> [[texture(GTAO_TEX_HISTORY)]]
//   GLSL b5 u_out0 (image) -> texture2d<float, access::write> [[texture(GTAO_TEX_OUT0)]]
//   GLSL b6 u_out1 (image) -> texture2d<float, access::write> [[texture(GTAO_TEX_OUT1)]]
//   GLSL b7 GTAOConstants  -> constant GTAOConstants& [[buffer(GTAO_BUF_CONSTANTS)]]
//
// The per-stage kernels (gtao_prefilter_depth / gtao_main / gtao_denoise /
// gtao_upsample) declare these resources in their [[kernel]] signature and pass
// them into the helpers below by reference, since MSL has no global resource
// scope like GLSL's descriptor bindings.
// ============================================================================

#include <metal_stdlib>
using namespace metal;

#include "ge_metal_bindings.h"

// ---- GTAO binding slots -----------------------------------------------------
// The GTAO pass is a self-contained compute pass with its own descriptor set
// (GEVulkanGTAOPass, GLSL bindings 0..7), distinct from the main-render "set=1"
// engine descriptors named in ge_metal_bindings.h. So its texture slots are
// defined locally here, keeping the GLSL binding numbers 0..6 verbatim (the CPU
// GEMetalGTAOPass binds its 7 textures at these indices). The GTAOConstants UBO
// rides the shared push-constant buffer slot so it never collides with the
// texture argument table.
#define GTAO_TEX_DEPTH         0   // u_depth        (GLSL b0)
#define GTAO_TEX_NORMAL        1   // u_normal       (GLSL b1)
#define GTAO_TEX_LINEAR_DEPTH  2   // u_linear_depth (GLSL b2)
#define GTAO_TEX_INPUT         3   // u_input        (GLSL b3)
#define GTAO_TEX_HISTORY       4   // u_history      (GLSL b4)
#define GTAO_TEX_OUT0          5   // u_out0  (writeonly image, GLSL b5)
#define GTAO_TEX_OUT1          6   // u_out1  (writeonly image, GLSL b6)
#define GTAO_BUF_CONSTANTS     GE_MTL_BUF_PUSH_CONSTANT  // GTAOConstants (GLSL b7)

// ---- std140 GTAOConstants (matches the CPU GTAOConstants struct) ------------
// Layout mirrors GEVulkanGTAOPass' std140 block byte-for-byte:
//   4x float4x4 (64 B each) + 4x float4 (16 B each) = 320 B, tight in std140.
struct GTAOConstants
{
    float4x4 m_inverse_projection;
    float4x4 m_inverse_view;
    float4x4 m_projection_view;
    float4x4 m_previous_projection_view;
    float4   m_viewport;
    float4   m_screen;
    float4   m_params0; // radius, intensity, temporal blend, frame index
    float4   m_params1; // half width, half height, reset history, unused
};

// Bundle of the read textures used by the reconstruction helpers, so kernels
// can forward their [[texture(...)]] bindings without a long argument list.
struct GTAOTextures
{
    texture2d<float> depth;         // GTAO_TEX_DEPTH
    texture2d<float> normal;        // GTAO_TEX_NORMAL
    texture2d<float> linear_depth;  // GTAO_TEX_LINEAR_DEPTH
    texture2d<float> input;         // GTAO_TEX_INPUT
    texture2d<float> history;       // GTAO_TEX_HISTORY
};

// ----------------------------------------------------------------------------
// Octahedral normal decode. Identical to DecodeGTAONormal() in the GLSL source.
inline float3 DecodeGTAONormal(float2 n)
{
    n = n * 2.0 - 1.0;
    float3 ret = float3(n.x, n.y, 1.0 - abs(n.x) - abs(n.y));
    float t = max(-ret.z, 0.0);
    ret.x += ret.x >= 0.0 ? -t : t;
    ret.y += ret.y >= 0.0 ? -t : t;
    return normalize(ret);
}

// ----------------------------------------------------------------------------
// Clamp a pixel to the active viewport rectangle (viewport.xy origin,
// viewport.zw size). Matches clampScreenPixel().
inline int2 clampScreenPixel(constant GTAOConstants& u_pc, int2 px)
{
    int2 lo = int2(u_pc.m_viewport.xy);
    int2 hi = int2(u_pc.m_viewport.xy + u_pc.m_viewport.zw) - int2(1);
    return clamp(px, lo, hi);
}

// ----------------------------------------------------------------------------
// Reconstruct a view-space position from the device-depth texture. Matches
// viewPosFromScreen(). texelFetch(u_depth, px, 0).r -> depth.read(uint2(px)).x.
//
// NOTE: the NDC.z fed to m_inverse_projection is the *raw* device depth `z`
// as stored (Metal device depth is [0,1] and the CPU builds
// m_inverse_projection to match), exactly as the GLSL passes the sampled
// value straight through. The x/y NDC is remapped from viewport-local pixel
// coords to [-1,1] identically.
inline float3 viewPosFromScreen(constant GTAOConstants& u_pc,
                                texture2d<float> u_depth, int2 px)
{
    px = clampScreenPixel(u_pc, px);
    float z = u_depth.read(uint2(px)).x;
    float2 ndc = ((float2(px) + float2(0.5) - u_pc.m_viewport.xy) /
        u_pc.m_viewport.zw) * 2.0 - 1.0;
    float4 view_pos = u_pc.m_inverse_projection * float4(ndc, z, 1.0);
    float inv_w = abs(view_pos.w) > 1e-6 ? 1.0 / view_pos.w : 0.0;
    return view_pos.xyz * inv_w;
}

// ----------------------------------------------------------------------------
// View-space linear depth magnitude. Matches viewDepthFromScreen().
inline float viewDepthFromScreen(constant GTAOConstants& u_pc,
                                 texture2d<float> u_depth, int2 px)
{
    return abs(viewPosFromScreen(u_pc, u_depth, px).z);
}

// ----------------------------------------------------------------------------
// Reconstruct a view-space normal from the encoded world-normal texture.
// Matches viewNormalFromScreen(): decode octahedral world normal, then rotate
// into view space with transpose(mat3(inverse_view)).
//
// GLSL mat3(m) takes the upper-left 3x3 of the 4x4. In MSL float3x3(m4)
// likewise takes columns 0..2 truncated to xyz, so float3x3(m).transpose()
// reproduces transpose(mat3(m)) exactly.
inline float3 viewNormalFromScreen(constant GTAOConstants& u_pc,
                                   texture2d<float> u_normal, int2 px)
{
    float2 n = u_normal.read(uint2(clampScreenPixel(u_pc, px))).xy;
    float3 world_normal = DecodeGTAONormal(n);
    float3x3 inv_view3 = float3x3(u_pc.m_inverse_view[0].xyz,
                                  u_pc.m_inverse_view[1].xyz,
                                  u_pc.m_inverse_view[2].xyz);
    return normalize(transpose(inv_view3) * world_normal);
}

// ----------------------------------------------------------------------------
// Interleaved gradient noise. Bit-identical to the GLSL constants/ops.
inline float interleavedGradientNoise(float2 p)
{
    const float3 m = float3(0.06711056, 0.00583715, 52.9829189);
    return fract(m.z * fract(dot(p, m.xy)));
}

// ----------------------------------------------------------------------------
// R2 low-discrepancy temporal offset of the gradient noise. Matches r2Noise().
inline float r2Noise(float2 p, float frame)
{
    return fract(interleavedGradientNoise(p) + frame * 0.754877666);
}

#endif // HEADER_GE_METAL_GTAO_COMMON_H
