#ifndef HEADER_GE_METAL_GTAO_MAIN_IMPL_H
#define HEADER_GE_METAL_GTAO_MAIN_IMPL_H

// ============================================================================
// MSL port of data/shaders/ge_shaders/utils/gtao_main_impl.glsl
//
// Half-res GTAO estimate: 3 slices x 2 sides x 6 samples horizon search with
// range/thin-object attenuation, then intensity + gamma shaping. Every literal
// (SLICE_COUNT, SAMPLES_PER_SLICE, 0.65 step bias, 0.61803398875 jitter,
// 0.45/0.1 mix, thickness 0.08..0.55, -0.08 horizon bias, pow 1.15, clamps) is
// reproduced verbatim so the AO is numerically identical to the GLSL.
//
// A single [[kernel]] serves the r16f and r32f variants (format is a CPU-side
// MTLPixelFormat choice; the GLSL AO_IMAGE_FORMAT qualifier is dropped).
// Dispatch mirrors GLSL local_size 8x8x1.
// ============================================================================

#include "gtao_common.h"

// ----------------------------------------------------------------------------
// gid is unused in the estimator (as in the GLSL: it is passed but only the
// screen-space center_px drives sampling); kept in the signature for parity.
inline float gtaoEstimate(constant GTAOConstants& u_pc,
                          texture2d<float> u_depth,
                          int2 gid, float2 center_px, float3 center_pos,
                          float3 center_normal, float noise)
{
    const int SLICE_COUNT = 3;
    const int SAMPLES_PER_SLICE = 6;
    const float PI = 3.14159265359;

    float view_depth = max(abs(center_pos.z), 0.05);
    float radius = u_pc.m_params0.x;
    float projection_scale = 0.5 * u_pc.m_viewport.w;
    float pixel_radius = clamp(radius * projection_scale / view_depth,
        2.0, 64.0);
    float occlusion = 0.0;
    float weight_sum = 0.0;

    for (int slice = 0; slice < SLICE_COUNT; slice++)
    {
        float angle = (float(slice) + noise) * PI / float(SLICE_COUNT);
        float2 dir = float2(cos(angle), sin(angle));
        for (int side = -1; side <= 1; side += 2)
        {
            float2 ray_dir = dir * float(side);
            float horizon = 0.0;
            for (int sample_id = 0; sample_id < SAMPLES_PER_SLICE; sample_id++)
            {
                float step_t = (float(sample_id) + 0.65) /
                    float(SAMPLES_PER_SLICE);
                float jitter = fract(noise + float(sample_id) * 0.61803398875);
                float sample_radius = pixel_radius *
                    mix(step_t, step_t * step_t, 0.45 + 0.1 * jitter);
                int2 sample_px = int2(round(center_px +
                    ray_dir * sample_radius));
                sample_px = clampScreenPixel(u_pc, sample_px);
                float raw_depth = u_depth.read(uint2(sample_px)).x;
                if (raw_depth >= 1.0)
                    continue;

                float3 sample_pos = viewPosFromScreen(u_pc, u_depth, sample_px);
                float3 v = sample_pos - center_pos;
                float dist2 = dot(v, v);
                float inv_len = rsqrt(max(dist2, 1e-5));
                float dist = dist2 * inv_len;
                float range = clamp(1.0 - dist / radius, 0.0, 1.0);
                range *= range;

                // Thin-object compensation: close blockers should survive
                // the half-res path, but broad depth jumps should not haze.
                float thickness = mix(0.08, 0.55, step_t);
                float thin = exp(-max(abs(v.z) - thickness, 0.0) * 2.0);
                float ndot = dot(center_normal, v * inv_len);
                horizon = max(horizon, max(ndot - 0.08, 0.0) * range * thin);
            }
            occlusion += horizon;
            weight_sum += 1.0;
        }
    }

    float ao = 1.0 - (occlusion / max(weight_sum, 1.0)) * u_pc.m_params0.y;
    return clamp(pow(ao, 1.15), 0.0, 1.0);
}

// ----------------------------------------------------------------------------
inline void gtaoMain(uint2 gid_u,
                     constant GTAOConstants& u_pc,
                     texture2d<float> u_depth,
                     texture2d<float> u_normal,
                     texture2d<float, access::write> u_out0)
{
    int2 gid = int2(gid_u);
    int2 out_size = int2(u_out0.get_width(), u_out0.get_height());
    if (gid.x >= out_size.x || gid.y >= out_size.y)
        return;

    float2 center_px = u_pc.m_viewport.xy + (float2(gid) + float2(0.5)) * 2.0;
    int2 screen_px = clampScreenPixel(u_pc, int2(center_px));
    float raw_depth = u_depth.read(uint2(screen_px)).x;
    if (raw_depth >= 1.0)
    {
        u_out0.write(float4(1.0), gid_u);
        return;
    }

    float3 center_pos = viewPosFromScreen(u_pc, u_depth, screen_px);
    float3 center_normal = viewNormalFromScreen(u_pc, u_normal, screen_px);
    float noise = r2Noise(center_px, u_pc.m_params0.w);
    float ao = gtaoEstimate(u_pc, u_depth, gid, center_px, center_pos,
        center_normal, noise);
    u_out0.write(float4(ao), gid_u);
}

// ----------------------------------------------------------------------------
// Entry point. Matches GLSL main() + local_size 8x8x1.
kernel void gtao_main_main(
    uint2 gid                                        [[thread_position_in_grid]],
    constant GTAOConstants& u_pc                     [[buffer(GTAO_BUF_CONSTANTS)]],
    texture2d<float> u_depth                         [[texture(GTAO_TEX_DEPTH)]],
    texture2d<float> u_normal                        [[texture(GTAO_TEX_NORMAL)]],
    texture2d<float, access::write> u_out0           [[texture(GTAO_TEX_OUT0)]])
{
    gtaoMain(gid, u_pc, u_depth, u_normal, u_out0);
}

#endif // HEADER_GE_METAL_GTAO_MAIN_IMPL_H
