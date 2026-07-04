#ifndef HEADER_GE_METAL_GTAO_UPSAMPLE_IMPL_H
#define HEADER_GE_METAL_GTAO_UPSAMPLE_IMPL_H

// ============================================================================
// MSL port of data/shaders/ge_shaders/utils/gtao_upsample_impl.glsl
//
// Full-res joint-bilateral upsample of the half-res AO plus temporal
// reprojection against the previous frame's AO (u_history). This is the stage
// that carries the temporal-reprojection math, ported byte-for-byte:
//
//   currentAO():   3x3 joint-bilateral tap of the half-res u_input, weighted by
//                  depth (exp -3.0), normal (pow ^10) and a sub-texel Gaussian
//                  (exp -1.35*|frac_delta|^2); also returns the min/max AO of
//                  the neighbourhood for the history clamp.
//   reprojectHistory(): world = inverse_view * view_pos; prev_clip =
//                  previous_projection_view * world; reject if behind
//                  (w <= 1e-4) or off-screen; reproject depth (reject if
//                  |Δdepth| > max(0.18, depth*0.035)) and normal (reject if
//                  dot < 0.78).
//   main(): far-plane pixels write 1.0; otherwise blend history (clamped to
//                  [min-0.04, max+0.04]) toward current by m_params0.z, gated
//                  off when m_params1.z (reset history) >= 0.5.
//
// GLSL fetch semantics preserved:
//   - u_input  : texelFetch/point (.read) for the 3x3 taps
//   - u_history: texture()/BILINEAR (.sample) at history_uv
//   - u_linear_depth : texelFetch/point (.read)
//   - textureSize(u_input,0) -> u_input.get_width()/get_height()
//
// A single [[kernel]] serves the r16f and r32f variants. Dispatch mirrors GLSL
// local_size 8x8x1. Writes both u_out0 and u_out1 (result + next-frame history).
// ============================================================================

#include "gtao_common.h"

// ----------------------------------------------------------------------------
// currentAO(): 3x3 joint-bilateral half-res tap. min_ao/max_ao returned by ref.
inline float gtaoCurrentAO(constant GTAOConstants& u_pc,
                           texture2d<float> u_normal,
                           texture2d<float> u_linear_depth,
                           texture2d<float> u_input,
                           int2 gid, float center_depth, float3 center_normal,
                           thread float& min_ao, thread float& max_ao)
{
    int2 half_size = int2(u_input.get_width(), u_input.get_height());
    float2 half_uv = (float2(gid) + float2(0.5)) /
        max(u_pc.m_viewport.zw, float2(1.0));
    float2 half_px = half_uv * float2(half_size) - float2(0.5);
    int2 base = int2(floor(half_px));

    float sum = 0.0;
    float weight_sum = 0.0;
    min_ao = 1.0;
    max_ao = 0.0;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            int2 sid = clamp(base + int2(x, y), int2(0),
                half_size - int2(1));
            int2 sample_screen = clampScreenPixel(u_pc, int2(u_pc.m_viewport.xy +
                (float2(sid) + float2(0.5)) * 2.0));
            float sample_depth = u_linear_depth.read(uint2(sid)).x;
            float3 sample_normal = viewNormalFromScreen(u_pc, u_normal,
                sample_screen);
            float ao = u_input.read(uint2(sid)).x;
            float depth_w = exp(-abs(sample_depth - center_depth) * 3.0);
            float normal_w = pow(max(dot(sample_normal, center_normal), 0.0),
                10.0);
            float2 frac_delta = float2(sid) + float2(0.5) - half_px;
            float spatial_w = exp(-dot(frac_delta, frac_delta) * 1.35);
            float w = depth_w * normal_w * spatial_w;
            sum += ao * w;
            weight_sum += w;
            min_ao = min(min_ao, ao);
            max_ao = max(max_ao, ao);
        }
    }
    return sum / max(weight_sum, 1e-4);
}

// ----------------------------------------------------------------------------
// reprojectHistory(): temporal reprojection + depth/normal disocclusion reject.
// history_uv returned by ref; returns false if the history sample is invalid.
inline bool gtaoReprojectHistory(constant GTAOConstants& u_pc,
                                 texture2d<float> u_depth,
                                 texture2d<float> u_normal,
                                 int2 gid, float3 view_pos, float3 normal,
                                 thread float2& history_uv)
{
    float4 world = u_pc.m_inverse_view * float4(view_pos, 1.0);
    float4 prev_clip = u_pc.m_previous_projection_view * world;
    if (prev_clip.w <= 1e-4)
        return false;
    float2 prev_ndc = prev_clip.xy / prev_clip.w;
    history_uv = prev_ndc * 0.5 + 0.5;
    if (any(history_uv < float2(0.0)) ||
        any(history_uv > float2(1.0)))
        return false;

    int2 prev_screen = clampScreenPixel(u_pc, int2(u_pc.m_viewport.xy +
        history_uv * u_pc.m_viewport.zw));
    float prev_depth_now = viewDepthFromScreen(u_pc, u_depth, prev_screen);
    float cur_depth = abs(view_pos.z);
    if (abs(prev_depth_now - cur_depth) > max(0.18, cur_depth * 0.035))
        return false;

    float3 prev_normal_now = viewNormalFromScreen(u_pc, u_normal, prev_screen);
    if (dot(prev_normal_now, normal) < 0.78)
        return false;
    return true;
}

// ----------------------------------------------------------------------------
inline void gtaoUpsample(uint2 gid_u,
                         constant GTAOConstants& u_pc,
                         texture2d<float> u_depth,
                         texture2d<float> u_normal,
                         texture2d<float> u_linear_depth,
                         texture2d<float> u_input,
                         texture2d<float> u_history,
                         sampler u_history_sampler,
                         texture2d<float, access::write> u_out0,
                         texture2d<float, access::write> u_out1)
{
    int2 gid = int2(gid_u);
    int2 out_size = int2(u_out0.get_width(), u_out0.get_height());
    if (gid.x >= out_size.x || gid.y >= out_size.y)
        return;

    int2 screen_px = clampScreenPixel(u_pc, int2(u_pc.m_viewport.xy) + gid);
    float raw_depth = u_depth.read(uint2(screen_px)).x;
    if (raw_depth >= 1.0)
    {
        u_out0.write(float4(1.0), gid_u);
        u_out1.write(float4(1.0), gid_u);
        return;
    }

    float3 view_pos = viewPosFromScreen(u_pc, u_depth, screen_px);
    float center_depth = abs(view_pos.z);
    float3 normal = viewNormalFromScreen(u_pc, u_normal, screen_px);
    float min_ao;
    float max_ao;
    float ao = gtaoCurrentAO(u_pc, u_normal, u_linear_depth, u_input,
        gid, center_depth, normal, min_ao, max_ao);

    if (u_pc.m_params1.z < 0.5)
    {
        float2 history_uv;
        if (gtaoReprojectHistory(u_pc, u_depth, u_normal, gid, view_pos,
                normal, history_uv))
        {
            float history_ao = u_history.sample(u_history_sampler,
                history_uv).x;
            history_ao = clamp(history_ao, min_ao - 0.04, max_ao + 0.04);
            ao = mix(ao, history_ao, u_pc.m_params0.z);
        }
    }

    ao = clamp(ao, 0.0, 1.0);
    u_out0.write(float4(ao), gid_u);
    u_out1.write(float4(ao), gid_u);
}

// ----------------------------------------------------------------------------
// Entry point. Matches GLSL main() + local_size 8x8x1.
kernel void gtao_upsample_main(
    uint2 gid                                        [[thread_position_in_grid]],
    constant GTAOConstants& u_pc                     [[buffer(GTAO_BUF_CONSTANTS)]],
    texture2d<float> u_depth                         [[texture(GTAO_TEX_DEPTH)]],
    texture2d<float> u_normal                        [[texture(GTAO_TEX_NORMAL)]],
    texture2d<float> u_linear_depth                  [[texture(GTAO_TEX_LINEAR_DEPTH)]],
    texture2d<float> u_input                         [[texture(GTAO_TEX_INPUT)]],
    texture2d<float> u_history                       [[texture(GTAO_TEX_HISTORY)]],
    texture2d<float, access::write> u_out0           [[texture(GTAO_TEX_OUT0)]],
    texture2d<float, access::write> u_out1           [[texture(GTAO_TEX_OUT1)]])
{
    // Bilinear/clamp sampler for texture(u_history, history_uv) in the GLSL.
    constexpr sampler u_history_sampler(coord::normalized,
                                        address::clamp_to_edge,
                                        filter::linear);
    gtaoUpsample(gid, u_pc, u_depth, u_normal, u_linear_depth, u_input,
        u_history, u_history_sampler, u_out0, u_out1);
}

#endif // HEADER_GE_METAL_GTAO_UPSAMPLE_IMPL_H
