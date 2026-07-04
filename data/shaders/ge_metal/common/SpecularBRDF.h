#ifndef GE_METAL_SPECULAR_BRDF_H
#define GE_METAL_SPECULAR_BRDF_H

// Faithful MSL port of data/shaders/utils/SpecularBRDF.frag.

#include <metal_stdlib>
using namespace metal;

// Blinn Phong with emulated fresnel factor
inline float3 SpecularBRDF(float3 normal, float3 eyedir, float3 lightdir,
                           float3 color, float roughness)
{
    float exponentroughness = exp2(10. * roughness + 1.);
    // Half Light View direction
    float3 H = normalize(eyedir + lightdir);
    float NdotH = clamp(dot(normal, H), 0., 1.);
    float normalisationFactor = (exponentroughness + 2.) / 8.;
    float3 FresnelSchlick = color + (1.0 - color) *
        pow(1.0 - clamp(dot(eyedir, H), 0., 1.), 5.);
    return max(pow(NdotH, exponentroughness) * FresnelSchlick * normalisationFactor,
        float3(0.));
}

#endif // GE_METAL_SPECULAR_BRDF_H
