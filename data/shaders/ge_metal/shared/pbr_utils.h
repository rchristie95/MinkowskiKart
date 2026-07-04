#ifndef GE_METAL_PBR_UTILS_H
#define GE_METAL_PBR_UTILS_H

// Faithful MSL port of data/shaders/ge_shaders/utils/pbr_utils.glsl.
// Pure math helpers, no resource bindings. Numerically identical to the GLSL.

#include <metal_stdlib>
using namespace metal;

inline float2 F_AB(float perceptual_roughness, float NdotV)
{
    float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 r = perceptual_roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}

// Lambert model
inline float F_Schlick(float f0, float f90, float VdotH)
{
    return mix(f0, f90, pow(1.0 - VdotH, 5.0));
}

inline float Fd_Burley(float roughness, float NdotV, float NdotL, float LdotH)
{
    // Don't divide by Pi to avoid light being too dim.
    float f90 = 0.5 + 2.0 * roughness * LdotH * LdotH;
    float lightScatter = F_Schlick(1.0, f90, NdotL);
    float viewScatter = F_Schlick(1.0, f90, NdotV);
    return lightScatter * viewScatter;
}

// Calculate distribution.
// Based on https://google.github.io/filament/Filament.html#citation-walter07
// D_GGX(h,a) = a^2 / { pi ((n.h)^2 (a2-1) + 1)^2 }
// Simple implementation, has precision problems when using fp16 instead of fp32
// see https://google.github.io/filament/Filament.html#listing_speculardfp16
inline float D_GGX(float roughness, float NdotH)
{
    float oneMinusNdotHSquared = 1.0 - NdotH * NdotH;
    float a = NdotH * roughness;
    float k = roughness / (oneMinusNdotHSquared + a * a);
    return k * k * (1.0 / 3.14159265359);
}

// Calculate visibility.
// Hammon 2017, "PBR Diffuse Lighting for GGX+Smith Microsurfaces"
// see https://google.github.io/filament/Filament.html#listing_approximatedspecularv
inline float V_Smith_GGX_Correlated(float roughness, float NdotV, float NdotL)
{
    return 0.5 / mix(2.0 * NdotL * NdotV, NdotL + NdotV, roughness);
}

// Fresnel function
// see https://google.github.io/filament/Filament.html#citation-schlick94
// F_Schlick(v,h,f_0,f_90) = f_0 + (f_90 - f_0) (1 - v.h)^5
inline float3 fresnel(float3 f0, float f90, float VdotH)
{
    return f0 + (f90 - f0) * pow(1.0 - VdotH, 5.0);
}

inline float3 envBRDFApprox(float3 F0, float2 F_ab)
{
    return F0 * F_ab.x + F_ab.y;
}

inline float perceptualRoughnessToRoughness(float perceptual_roughness)
{
    float roughness = clamp(perceptual_roughness, 0.089, 1.0);
    return roughness * roughness;
}

inline float3 environmentLight(
    float3 irradiance,
    float3 radiance,
    float roughness,
    float3 diffuse_color,
    float2 F_ab,
    float3 F0,
    float F90,
    float NdotV)
{
    // Multiscattering approximation: https://www.jcgt.org/published/0008/01/03/paper.pdf
    // Useful reference: https://bruop.github.io/ibl
    float3 Fr = max(float3(1.0 - roughness), F0) - F0;
    float3 kS = F0 + Fr * pow(1.0 - NdotV, 5.0);
    float Ess = F_ab.x + F_ab.y;
    float3 FssEss = kS * Ess * F90;
    float Ems = 1.0 - Ess;
    float3 Favg = F0 + (1.0 - F0) / 21.0;
    float3 Fms = FssEss * Favg / (1.0 - Ems * Favg);
    float3 FmsEms = Fms * Ems;
    float3 Edss = 1.0 - (FssEss + FmsEms);
    float3 kD = diffuse_color * Edss;

    float3 diffuse = (FmsEms + kD) * irradiance;
    float3 specular = FssEss * radiance;
    return diffuse + specular;
}

#endif // GE_METAL_PBR_UTILS_H
