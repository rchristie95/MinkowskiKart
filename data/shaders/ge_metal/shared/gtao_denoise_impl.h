#ifndef HEADER_GE_METAL_GTAO_DENOISE_IMPL_H
#define HEADER_GE_METAL_GTAO_DENOISE_IMPL_H

// ============================================================================
// MSL port of data/shaders/ge_shaders/utils/gtao_denoise_impl.glsl
//
// Half-res edge-aware 5x5 blur of the raw AO, weighted by depth (exp -2.5),
// normal (pow ^8) and a Gaussian spatial term (exp -0.22*|o|^2). All constants
// are reproduced verbatim.
//
// GLSL detail preserved exactly:
//   - center_depth = texelFetch(u_linear_depth, gid).r  (linear view depth)
//   - center_normal reconstructed at the FULL-RES screen pixel (viewport.xy +
//     (gid+0.5)*2), not at gid.
//   - the AO term uses texture(u_input, uv) i.e. BILINEAR sampling at the
//     half-res UV (sample_id+0.5)*inv_size, whereas depth/normal use
//     texelFetch/point at the same tap. This asymmetry is kept: u_input is
//     sampled through a linear/clamp sampler, u_linear_depth via .read().
//
// A single [[kernel]] serves the r16f and r32f variants (format is a CPU-side
// MTLPixelFormat choice). Dispatch mirrors GLSL local_size 8x8x1.
// ============================================================================

#include "gtao_common.h"

inline void gtaoDenoise(uint2 gid_u,
                        constant GTAOConstants& u_pc,
                        texture2d<float> u_normal,
                        texture2d<float> u_linear_depth,
                        texture2d<float> u_input,
                        sampler u_input_sampler,
                        texture2d<float, access::write> u_out0)
{
    int2 gid = int2(gid_u);
    int2 out_size = int2(u_out0.get_width(), u_out0.get_height());
    if (gid.x >= out_size.x || gid.y >= out_size.y)
        return;

    float2 inv_size = 1.0 / float2(out_size);
    // uv is computed in the GLSL but only sample UVs are used below; kept for
    // parity / clarity.
    float2 uv = (float2(gid) + float2(0.5)) * inv_size;
    (void)uv;
    int2 screen_px = clampScreenPixel(u_pc, int2(u_pc.m_viewport.xy +
        (float2(gid) + float2(0.5)) * 2.0));
    float center_depth = u_linear_depth.read(uint2(gid)).x;
    float3 center_normal = viewNormalFromScreen(u_pc, u_normal, screen_px);

    float sum = 0.0;
    float weight_sum = 0.0;
    for (int y = -2; y <= 2; y++)
    {
        for (int x = -2; x <= 2; x++)
        {
            int2 sample_id = clamp(gid + int2(x, y), int2(0),
                out_size - int2(1));
            int2 sample_screen = clampScreenPixel(u_pc, int2(u_pc.m_viewport.xy +
                (float2(sample_id) + float2(0.5)) * 2.0));
            float sample_depth = u_linear_depth.read(uint2(sample_id)).x;
            float3 sample_normal = viewNormalFromScreen(u_pc, u_normal,
                sample_screen);
            float depth_w = exp(-abs(sample_depth - center_depth) * 2.5);
            float normal_w = pow(max(dot(sample_normal, center_normal), 0.0),
                8.0);
            float2 o = float2(x, y);
            float spatial_w = exp(-dot(o, o) * 0.22);
            float w = spatial_w * depth_w * normal_w;
            sum += u_input.sample(u_input_sampler,
                (float2(sample_id) + float2(0.5)) * inv_size).x * w;
            weight_sum += w;
        }
    }

    u_out0.write(float4(sum / max(weight_sum, 1e-4)), gid_u);
}

// ----------------------------------------------------------------------------
// Entry point. Matches GLSL main() + local_size 8x8x1.
kernel void gtao_denoise_main(
    uint2 gid                                        [[thread_position_in_grid]],
    constant GTAOConstants& u_pc                     [[buffer(GTAO_BUF_CONSTANTS)]],
    texture2d<float> u_normal                        [[texture(GTAO_TEX_NORMAL)]],
    texture2d<float> u_linear_depth                  [[texture(GTAO_TEX_LINEAR_DEPTH)]],
    texture2d<float> u_input                         [[texture(GTAO_TEX_INPUT)]],
    texture2d<float, access::write> u_out0           [[texture(GTAO_TEX_OUT0)]])
{
    // Linear-filter, clamp-to-edge sampler for the bilinear texture(u_input,uv)
    // fetch in the GLSL. Depth/normal taps use point .read() and need no sampler.
    constexpr sampler u_input_sampler(coord::normalized,
                                      address::clamp_to_edge,
                                      filter::linear);
    gtaoDenoise(gid, u_pc, u_normal, u_linear_depth, u_input, u_input_sampler,
        u_out0);
}

#endif // HEADER_GE_METAL_GTAO_DENOISE_IMPL_H
