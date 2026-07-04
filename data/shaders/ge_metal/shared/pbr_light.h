#ifndef GE_METAL_PBR_LIGHT_H
#define GE_METAL_PBR_LIGHT_H

// Faithful MSL port of data/shaders/ge_shaders/utils/pbr_light.glsl.
//
// GLSL reads u_ibl (spec constant), u_global_light and u_camera as module-level
// globals. MSL has no such globals, so:
//   * u_ibl stays a [[function_constant]] (global, from ge_metal_bindings.h).
//   * u_global_light / u_camera are threaded in as `constant ...&` parameters.
//   * TILED_GPU is defined by the shader manager predefines (native Metal is
//     tile-based), so the calculateLight()-style path is used everywhere; the
//     accumulateLights() early-out under !TILED_GPU is preserved for parity.

#include <metal_stdlib>
using namespace metal;

#include "ge_metal_bindings.h"
#include "pbr_utils.h"

inline float3 PBRLight(
    float3 normal,
    float3 eyedir,
    float3 lightdir,
    float3 color,
    float perceptual_roughness,
    float metallic)
{
    float NdotV = max(dot(normal, eyedir), 0.0001);
    float NdotL = clamp(dot(normal, lightdir), 0.0, 1.0);

    float2 F_ab = F_AB(perceptual_roughness, NdotV);

    float3 H = normalize(eyedir + lightdir);
    float NdotH = clamp(dot(normal, H), 0.0, 1.0);
    float LdotH = clamp(dot(lightdir, H), 0.0, 1.0);

    float3 diffuse_color = color * (1.0 - metallic);
    float3 F0 = mix(float3(0.04), color, metallic);
    // No real world material has specular values under 0.02, so we use this range as a
    // "pre-baked specular occlusion" that extinguishes the fresnel term, for artistic control.
    // See: https://google.github.io/filament/Filament.html#specularocclusion
    float F90 = clamp(dot(F0, float3(50.0 * 0.33)), 0.0, 1.0);

    float roughness = perceptualRoughnessToRoughness(perceptual_roughness);

    float3 diffuse = diffuse_color * Fd_Burley(roughness, NdotV, NdotL, NdotH);

    float D = D_GGX(roughness, NdotH);
    float V = V_Smith_GGX_Correlated(roughness, NdotV, NdotL);
    float3 F = fresnel(F0, F90, LdotH);
    float3 specular = D * V * F * (1.0 + F0 * (1.0 / F_ab.x - 1.0));

    return NdotL * (diffuse + specular);
}

inline float3 PBRSunAmbientEmitLight(
    float3 normal,
    float3 eyedir,
    float3 sundir,
    float3 color,
    float3 irradiance,
    float3 radiance,
    float3 sun_color,
    float3 ambient_color,
    float perceptual_roughness,
    float metallic,
    float emissive,
    float ambient_occlusion,
    constant GEGlobalLightBuffer& u_global_light)
{
    // Copied from PBRLight to use F_ab and F90 again
    float NdotV = max(dot(normal, eyedir), 0.0001);
    float NdotL = clamp(dot(normal, sundir), 0.0, 1.0);

    float2 F_ab = F_AB(perceptual_roughness, NdotV);

    float3 H = normalize(eyedir + sundir);
    float NdotH = clamp(dot(normal, H), 0.0, 1.0);
    float LdotH = clamp(dot(sundir, H), 0.0, 1.0);

    float3 diffuse_color = color * (1.0 - metallic);
    float3 F0 = mix(float3(0.04), color, metallic);
    // No real world material has specular values under 0.02, so we use this range as a
    // "pre-baked specular occlusion" that extinguishes the fresnel term, for artistic control.
    // See: https://google.github.io/filament/Filament.html#specularocclusion
    float F90 = clamp(dot(F0, float3(50.0 * 0.33)), 0.0, 1.0);

    float roughness = perceptualRoughnessToRoughness(perceptual_roughness);

    float3 diffuse = diffuse_color * Fd_Burley(roughness, NdotV, NdotL, NdotH);

    float D = D_GGX(roughness, NdotH);
    float V = V_Smith_GGX_Correlated(roughness, NdotV, NdotL);
    float3 F = fresnel(F0, F90, LdotH);
    float3 specular = D * V * F * (1.0 + F0 * (1.0 / F_ab.x - 1.0));

    float3 sunlight = NdotL * (diffuse + specular);

    float3 diffuse_ambient = envBRDFApprox(diffuse_color, F_AB(1.0, NdotV));

    float3 specular_ambient = F90 * envBRDFApprox(F0, F_ab);

    // Other 0.6 comes from skybox
    ambient_color *= 0.4;
    float3 environment;
    if (u_ibl)
    {
        environment = environmentLight(irradiance, radiance, roughness,
            diffuse_color, F_ab, F0, F90, NdotV);
    }
    else
    {
        environment = float3(u_global_light.m_skytop_color) * ambient_color *
            diffuse_color;
    }

    float3 emit = emissive * color * 4.0;

    float diffuse_ao = clamp(ambient_occlusion, 0.0, 1.0);
    float specular_ao = clamp(pow(diffuse_ao,
        mix(0.45, 1.0, perceptual_roughness)), 0.0, 1.0);

    return sun_color * sunlight
          + environment * diffuse_ao + emit
          + (diffuse_ambient * diffuse_ao + specular_ambient * specular_ao) *
            ambient_color;
}

inline float3 accumulateLights(int light_count, float3 diffuse_color, float3 normal,
                               float3 xpos, float3 eyedir, float perceptual_roughness,
                               float metallic,
                               constant GECameraBuffer& u_camera,
                               constant GEGlobalLightBuffer& u_global_light)
{
    float3 accumulated_color = float3(0.0);
    for (int i = 0; i < light_count; i++)
    {
        float3 light_to_frag = (u_camera.m_view_matrix *
            float4(float3(u_global_light.m_lights[i].m_position_radius.xyz),
            1.0)).xyz - xpos;
        float invrange = u_global_light.m_lights[i].m_color_inverse_square_range.w;
        float distance_sq = dot(light_to_frag, light_to_frag);
        if (distance_sq * invrange > 1.)
            continue;
        // SpotLight
        float sattenuation = 1.;
        float sscale = u_global_light.m_lights[i].m_direction_scale_offset.z;
        float dist = sqrt(distance_sq);
        float distance_inverse = 1. / dist;
        float3 L = light_to_frag * distance_inverse;
        if (sscale != 0.)
        {
            float3 sdir =
                float3(u_global_light.m_lights[i].m_direction_scale_offset.xy, 0.);
            sdir.z = sqrt(1. - dot(sdir, sdir)) * sign(sscale);
            sdir = (u_camera.m_view_matrix * float4(sdir, 0.0)).xyz;
            sattenuation = clamp(dot(-sdir, L) *
                abs(sscale) +
                u_global_light.m_lights[i].m_direction_scale_offset.w, 0.0, 1.0);
#ifndef TILED_GPU
            // Reduce branching in tiled GPU
            if (sattenuation == 0.)
                continue;
#endif
        }
        float3 diffuse_specular = PBRLight(normal, eyedir, L, diffuse_color,
            perceptual_roughness, metallic);
        float attenuation = 20. / (1. + distance_sq);
        float radius = u_global_light.m_lights[i].m_position_radius.w;
        attenuation *= (radius - dist) / radius;
        attenuation *= sattenuation * sattenuation;
        float3 light_color =
            u_global_light.m_lights[i].m_color_inverse_square_range.xyz;
        accumulated_color += light_color * attenuation * diffuse_specular;
    }
    return accumulated_color;
}

// Copied because reusing in a loop will be slower
inline float3 calculateLight(int i, float3 diffuse_color, float3 normal, float3 xpos,
                             float3 eyedir, float perceptual_roughness, float metallic,
                             constant GECameraBuffer& u_camera,
                             constant GEGlobalLightBuffer& u_global_light)
{
    float3 light_to_frag = (u_camera.m_view_matrix *
        float4(float3(u_global_light.m_lights[i].m_position_radius.xyz),
        1.0)).xyz - xpos;
    float invrange = u_global_light.m_lights[i].m_color_inverse_square_range.w;
    float distance_sq = dot(light_to_frag, light_to_frag);
    if (distance_sq * invrange > 1.)
        return float3(0.0);
    // SpotLight
    float sattenuation = 1.;
    float sscale = u_global_light.m_lights[i].m_direction_scale_offset.z;
    float dist = sqrt(distance_sq);
    float distance_inverse = 1. / dist;
    float3 L = light_to_frag * distance_inverse;
    if (sscale != 0.)
    {
        float3 sdir =
            float3(u_global_light.m_lights[i].m_direction_scale_offset.xy, 0.);
        sdir.z = sqrt(1. - dot(sdir, sdir)) * sign(sscale);
        sdir = (u_camera.m_view_matrix * float4(sdir, 0.0)).xyz;
        sattenuation = clamp(dot(-sdir, L) *
            abs(sscale) +
            u_global_light.m_lights[i].m_direction_scale_offset.w, 0.0, 1.0);
        if (sattenuation == 0.)
            return float3(0.0);
    }
    float3 diffuse_specular = PBRLight(normal, eyedir, L, diffuse_color,
        perceptual_roughness, metallic);
    float attenuation = 20. / (1. + distance_sq);
    float radius = u_global_light.m_lights[i].m_position_radius.w;
    attenuation *= (radius - dist) / radius;
    attenuation *= sattenuation * sattenuation;
    float3 light_color =
        u_global_light.m_lights[i].m_color_inverse_square_range.xyz;
    return light_color * attenuation * diffuse_specular;
}

#endif // GE_METAL_PBR_LIGHT_H
