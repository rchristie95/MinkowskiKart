// sun_shadow.h - MSL port of data/shaders/ge_shaders/utils/sun_shadow.glsl
//
// Sun shadow atlas sampling for the deferred lighting pass. Two cascades (near +
// far halves of a 2x1 atlas). Ported numerically identically from the GLSL
// (PCF + PCSS), preserving every constant, bias and kernel.
//
// GLSL -> MSL mapping of the resources the GLSL declared as globals:
//   sampler2DShadow u_sun_shadow_pcf  ->  depth2d<float> + sampler (compare)
//                                          .sample_compare(s, uv, ref)
//   sampler2D       u_sun_shadow_raw  ->  texture2d<float> + sampler
//                                          .sample(s, uv).x
//   u_camera        (camera.h)        ->  constant GECameraBuffer&
//   u_global_light  (global_light.h)  ->  constant GEGlobalLightBuffer&
//   gl_FragCoord.xy                   ->  float2 frag_coord (pass [[position]].xy)
//
// The atlas is authored with GL/Vulkan depth conventions; coord.z from the
// sun_shadow_matrix is expected in [0,1] already (the CPU bakes the remap into
// m_sun_shadow_matrix), matching the GLSL which used coord.z directly.
#ifndef GE_METAL_SUN_SHADOW_H
#define GE_METAL_SUN_SHADOW_H

#include <metal_stdlib>
using namespace metal;

#include "camera.h"
#include "global_light_data.h"

// PCF / blocker search offsets (Vogel disk, from sunlightshadowpcss.frag)
constant float2 SUN_SHADOW_VOGEL16[16] =
{
    float2(0.18993645671348536, 0.027087114076591513),
    float2(-0.21261242652069953, 0.23391293246949066),
    float2(0.04771781344140756, -0.3666840644525993),
    float2(0.297730981239584, 0.398259878229082),
    float2(-0.509063425827436, -0.06528681462854097),
    float2(0.507855152944665, -0.2875976005206389),
    float2(-0.15230616564632418, 0.6426121151781916),
    float2(-0.30240170651828074, -0.5805072900736001),
    float2(0.6978019230005561, 0.2771173334141519),
    float2(-0.6990963248129052, 0.3210960724922725),
    float2(0.3565142601623699, -0.7066415061851589),
    float2(0.266890002328106, 0.8360191043249159),
    float2(-0.7515861305520581, -0.41609876195815027),
    float2(0.9102937449894895, -0.17014527555321657),
    float2(-0.5343471434373126, 0.8058593459499529),
    float2(-0.1133270115046468, -0.9490025827627441)
};

constant float2 SUN_SHADOW_SEARCH8[8] =
{
    float2( 0.125, -0.375),
    float2(-0.125,  0.375),
    float2( 0.625,  0.125),
    float2(-0.375, -0.625),
    float2(-0.625,  0.625),
    float2(-0.875, -0.125),
    float2( 0.375,  0.875),
    float2( 0.875, -0.875)
};

inline float sunShadowNoise(float2 w)
{
    const float3 m = float3(0.06711056, 0.00583715, 52.9829189);
    return fract(m.z * fract(dot(w, m.xy)));
}

// Samples one cascade of the shadow atlas. u_base = 0.0 (near) or 0.5 (far);
// the u axis covers half the atlas, so horizontal offsets are scaled by 0.5.
inline float sampleSunShadowCascade(float3 world_pos, float3 geo_normal,
    float slope, bool pcss, float4x4 sample_matrix, float4 cparams,
    float u_base, float2 frag_coord,
    depth2d<float> u_sun_shadow_pcf, sampler pcf_sampler,
    texture2d<float> u_sun_shadow_raw, sampler raw_sampler)
{
    float depth_range = cparams.x;
    float texel = cparams.z;
    float penumbra_per_metre = cparams.w;
    // Normal-offset bias: push the receiver out along the geometric normal
    // by 1..11 shadow texels (world units) depending on slope.
    float texel_world = 0.02 * texel / penumbra_per_metre;
    float4 sp = sample_matrix * float4(world_pos +
        geo_normal * (texel_world * (1.0 + slope) * 1.5), 1.0);
    float3 coord = sp.xyz / sp.w;
    float u_min = u_base + texel;
    float u_max = u_base + 0.5 - texel;
    if (coord.x <= u_min || coord.x >= u_max ||
        coord.y <= 0.002 || coord.y >= 0.998 ||
        coord.z <= 0.0 || coord.z >= 1.0)
        return 1.0;

    // Receiver depth bias along the sun axis, slope-scaled.
    float ref_z = coord.z -
        max((0.06 + texel_world * slope * 2.0) / depth_range, 0.0006);
    const float2 AXIS = float2(0.5, 1.0); // u axis covers half the atlas

    if (pcss)
    {
        // ---- PCSS (contact hardening) ----
        float angle = sunShadowNoise(frag_coord) * 6.2831853;
        float2 base = float2(cos(angle), sin(angle));
        float2x2 R = float2x2(base.x, base.y, -base.y, base.x);

        float search_radius = 5.0 * texel;
        float z_sum = 0.0;
        float blockers = 0.0;
        for (int i = 0; i < 8; i++)
        {
            float2 duv = R * (SUN_SHADOW_SEARCH8[i] * search_radius) * AXIS;
            float2 tc = float2(clamp(coord.x + duv.x, u_min, u_max),
                coord.y + duv.y);
            float z_occ = u_sun_shadow_raw.sample(raw_sampler, tc).x;
            if (z_occ < ref_z)
            {
                z_sum += z_occ;
                blockers += 1.0;
            }
        }
        if (blockers < 0.5)
            return 1.0;
        float separation =
            max(ref_z - z_sum / blockers, 0.0) * depth_range;
        float radius = clamp(separation * penumbra_per_metre,
            0.5 * texel, 8.0 * texel);
        float sum = 0.0;
        for (int i = 0; i < 16; i++)
        {
            float2 duv = R * (SUN_SHADOW_VOGEL16[i] * radius) * AXIS;
            float2 tc = float2(clamp(coord.x + duv.x, u_min, u_max),
                coord.y + duv.y);
            sum += u_sun_shadow_pcf.sample_compare(pcf_sampler, tc, ref_z);
        }
        return sum * (1.0 / 16.0);
    }
    else
    {
        // ---- Fixed kernel PCF (3x3 with hardware 2x2 per tap) ----
        float sum = 0.0;
        float r = 1.5 * texel;
        for (int x = -1; x <= 1; x++)
        {
            for (int y = -1; y <= 1; y++)
            {
                float2 tc = coord.xy +
                    float2(float(x) * 0.5, float(y)) * r;
                tc.x = clamp(tc.x, u_min, u_max);
                sum += u_sun_shadow_pcf.sample_compare(pcf_sampler, tc, ref_z);
            }
        }
        return sum * (1.0 / 9.0);
    }
}

// world_pos / shading normal from the G-buffer; geo_normal is the geometric
// (derivative-based) normal used for slope-scaled bias; view_z selects the
// cascade. frag_coord is gl_FragCoord.xy.
inline float getSunShadowFactor(float3 world_pos, float3 world_normal,
    float3 geo_normal, float view_z, float2 frag_coord,
    constant GECameraBuffer& u_camera,
    constant GEGlobalLightBuffer& u_global_light,
    depth2d<float> u_sun_shadow_pcf, sampler pcf_sampler,
    texture2d<float> u_sun_shadow_raw, sampler raw_sampler)
{
    if (u_camera.m_shadow_params.x <= 0.0)
        return 1.0;
    // Relativity fade: shadows are correct when slow and vanish before the warp
    // makes them visibly misplaced (see GLSL comment).
    float rel_fade = 0.0;
    if (u_camera.m_relativity_params.x > 0.5)
    {
        float beta = length(u_camera.m_relativity_beta.xyz);
        rel_fade = clamp((beta - 0.25) * 4.0, 0.0, 1.0);
        if (rel_fade >= 1.0)
            return 1.0;
    }

    float3 sun_dir = u_global_light.m_sun_direction;
    // Surfaces facing away from the sun receive no direct light.
    float ndl = dot(world_normal, sun_dir);
    if (ndl <= 0.02)
        return 1.0;
    // Slope of the geometric surface relative to the sun: tan(acos(N.L)).
    float geo_ndl = clamp(dot(geo_normal, sun_dir), 0.05, 1.0);
    float slope = clamp(sqrt(max(1.0 - geo_ndl * geo_ndl, 0.0)) / geo_ndl,
        0.0, 10.0);

    bool pcss = u_camera.m_shadow_params.y > 0.5;
    float split = u_camera.m_shadow_params_far.y;
    float blend_start = split * 0.8;

    float factor;
    if (view_z < blend_start)
    {
        factor = sampleSunShadowCascade(world_pos, geo_normal, slope, pcss,
            u_camera.m_sun_shadow_matrix, u_camera.m_shadow_params, 0.0,
            frag_coord, u_sun_shadow_pcf, pcf_sampler, u_sun_shadow_raw,
            raw_sampler);
    }
    else if (view_z < split)
    {
        float near_f = sampleSunShadowCascade(world_pos, geo_normal, slope,
            pcss, u_camera.m_sun_shadow_matrix, u_camera.m_shadow_params, 0.0,
            frag_coord, u_sun_shadow_pcf, pcf_sampler, u_sun_shadow_raw,
            raw_sampler);
        float far_f = sampleSunShadowCascade(world_pos, geo_normal, slope,
            pcss, u_camera.m_sun_shadow_matrix_far,
            u_camera.m_shadow_params_far, 0.5, frag_coord, u_sun_shadow_pcf,
            pcf_sampler, u_sun_shadow_raw, raw_sampler);
        factor = mix(near_f, far_f,
            (view_z - blend_start) / max(split - blend_start, 0.001));
    }
    else
    {
        factor = sampleSunShadowCascade(world_pos, geo_normal, slope, pcss,
            u_camera.m_sun_shadow_matrix_far, u_camera.m_shadow_params_far,
            0.5, frag_coord, u_sun_shadow_pcf, pcf_sampler, u_sun_shadow_raw,
            raw_sampler);
    }
    return mix(factor, 1.0, rel_fade);
}

#endif // GE_METAL_SUN_SHADOW_H
