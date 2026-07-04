#ifndef GE_METAL_HANDLE_PBR_H
#define GE_METAL_HANDLE_PBR_H

// Faithful MSL port of data/shaders/ge_shaders/utils/handle_pbr.glsl.
//
// GLSL globals -> MSL parameters:
//   set=2 binding=0 u_diffuse  (samplerCube irradiance) -> texturecube + sampler
//   set=2 binding=1 u_specular (samplerCube radiance)   -> texturecube + sampler
//   set=1 binding=0 u_camera        -> constant GECameraBuffer&
//   set=1 binding=3 u_global_light  -> constant GEGlobalLightBuffer&
//   u_ibl / u_specular_levels_minus_one -> [[function_constant]] (bindings hdr)
//
// convertColor() (constants_utils.glsl) and sunDirection() (sun_direction.glsl)
// are small enough that they are inlined here to keep this header self-contained
// (the shader manager inlines each unique include once, so pulling in the whole
// GLSL util tree is unnecessary and would drag in unrelated bindings).

#include <metal_stdlib>
using namespace metal;

#include "ge_metal_bindings.h"
#include "pbr_utils.h"
#include "pbr_light.h"

// ---- from constants_utils.glsl --------------------------------------------
inline float3 ge_convertColor(float3 input_color)
{
    if (u_ibl)
    {
        return (input_color * (6.5 * input_color + 0.45)) /
            (input_color * (5.0 * input_color + 1.75) + 0.05);
    }
    else
    {
        return (input_color * (7.0 * input_color + 0.75)) /
            (input_color * (5.0 * input_color + 1.75) + 0.05);
    }
}

// ---- from sun_direction.glsl ----------------------------------------------
// Sun Most Representative Point (used for MRP area lighting method)
// From "Frostbite going PBR" paper
inline float3 ge_sunDirection(float3 R, float3 sun_direction,
                              float sun_angle_tan_half,
                              float4x4 inverse_view_matrix)
{
    sun_direction = normalize((transpose(inverse_view_matrix) *
        float4(sun_direction, 0.)).xyz);
    float DdotR = dot(sun_direction, R);
    float3 S = normalize(R - DdotR * sun_direction);
    float sun_angle_tan_half2 = 1 + sun_angle_tan_half * sun_angle_tan_half;
    float2 sun_angle_sin_cos =
        float2(2 * sun_angle_tan_half, 2 - sun_angle_tan_half2) / sun_angle_tan_half2;
    // Equivalent to DdotR < cos(sun_angle)
    float factor = step(DdotR, sun_angle_sin_cos.y);
    return mix(R, normalize(sun_direction * sun_angle_sin_cos.y +
        S * sun_angle_sin_cos.x), factor);
}

// ---------------------------------------------------------------------------
inline float3 handlePBRDeferred(float3 diffuse_color, float3 pbr, float3 world_normal,
                                float3 eyedir, float3 normal, float perceptual_roughness,
                                float sun_shadow, float ambient_occlusion,
                                constant GECameraBuffer& u_camera,
                                constant GEGlobalLightBuffer& u_global_light,
                                texturecube<float> u_diffuse,
                                sampler u_diffuse_sampler,
                                texturecube<float> u_specular,
                                sampler u_specular_sampler)
{
    float radiance_level = perceptual_roughness * u_specular_levels_minus_one;
    float3 reflection = reflect(-eyedir, normal);

    float3 irradiance = float3(0.0);
    float3 radiance = float3(0.0);
    if (u_ibl)
    {
        float3 world_reflection = (u_camera.m_inverse_view_matrix *
            float4(reflection, 0.0)).xyz;
        irradiance = u_diffuse.sample(u_diffuse_sampler, world_normal).rgb;
        radiance = u_specular.sample(u_specular_sampler, world_reflection,
            level(radiance_level)).rgb;
    }

    float3 lightdir = ge_sunDirection(reflection,
        float3(u_global_light.m_sun_direction), u_global_light.m_sun_angle_tan_half,
        u_camera.m_inverse_view_matrix);

    float3 mixed_color = PBRSunAmbientEmitLight(
        normal, eyedir, lightdir, diffuse_color,
        irradiance, radiance,
        float3(u_global_light.m_sun_color) * sun_shadow,
        float3(u_global_light.m_ambient_color),
        perceptual_roughness, pbr.y, pbr.z, ambient_occlusion,
        u_global_light);

    return mixed_color;
}

inline float3 handlePBRDeferred(float3 diffuse_color, float3 pbr, float3 world_normal,
                                float3 eyedir, float3 normal, float perceptual_roughness,
                                float sun_shadow,
                                constant GECameraBuffer& u_camera,
                                constant GEGlobalLightBuffer& u_global_light,
                                texturecube<float> u_diffuse,
                                sampler u_diffuse_sampler,
                                texturecube<float> u_specular,
                                sampler u_specular_sampler)
{
    return handlePBRDeferred(diffuse_color, pbr, world_normal, eyedir,
        normal, perceptual_roughness, sun_shadow, 1.0,
        u_camera, u_global_light,
        u_diffuse, u_diffuse_sampler, u_specular, u_specular_sampler);
}

inline float3 handlePBRDeferred(float3 diffuse_color, float3 pbr, float3 world_normal,
                                float3 eyedir, float3 normal, float perceptual_roughness,
                                constant GECameraBuffer& u_camera,
                                constant GEGlobalLightBuffer& u_global_light,
                                texturecube<float> u_diffuse,
                                sampler u_diffuse_sampler,
                                texturecube<float> u_specular,
                                sampler u_specular_sampler)
{
    return handlePBRDeferred(diffuse_color, pbr, world_normal, eyedir,
        normal, perceptual_roughness, 1.0,
        u_camera, u_global_light,
        u_diffuse, u_diffuse_sampler, u_specular, u_specular_sampler);
}

inline float3 handlePBR(float3 diffuse_color, float3 pbr, float4 world_position,
                        float3 world_normal,
                        constant GECameraBuffer& u_camera,
                        constant GEGlobalLightBuffer& u_global_light,
                        texturecube<float> u_diffuse,
                        sampler u_diffuse_sampler,
                        texturecube<float> u_specular,
                        sampler u_specular_sampler)
{
    float3 xpos = (u_camera.m_view_matrix * world_position).xyz;
    float3 eyedir = -normalize(xpos);
    float3 normal = (u_camera.m_view_matrix * float4(world_normal, 0.0)).xyz;
    float perceptual_roughness = 1.0 - pbr.x;

    float3 mixed_color = handlePBRDeferred(diffuse_color, pbr, world_normal,
        eyedir, normal, perceptual_roughness,
        u_camera, u_global_light,
        u_diffuse, u_diffuse_sampler, u_specular, u_specular_sampler);
    mixed_color += accumulateLights(u_global_light.m_light_count,
        diffuse_color, normal, xpos, eyedir, perceptual_roughness, pbr.y,
        u_camera, u_global_light);

    //Disable for deferred shading
    //float factor = (1.0 - exp(length(xpos) * -0.0001));
    //mixed_color = mixed_color + float3(0.5) * factor;
    return ge_convertColor(mixed_color);
}

#endif // GE_METAL_HANDLE_PBR_H
