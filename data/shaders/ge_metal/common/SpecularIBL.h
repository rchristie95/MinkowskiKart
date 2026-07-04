#ifndef GE_METAL_SPECULAR_IBL_H
#define GE_METAL_SPECULAR_IBL_H

// Faithful MSL port of data/shaders/utils/SpecularIBL.frag.
//
// GLSL globals -> MSL parameters:
//   samplerCube probe          -> texturecube<float> probe + sampler
//   u_inverse_view_matrix      -> float4x4 inverse_view_matrix

#include <metal_stdlib>
using namespace metal;

inline float3 SpecularIBL(float3 normal, float3 V, float roughness,
                          texturecube<float> probe, sampler probe_sampler,
                          float4x4 inverse_view_matrix)
{
    float3 sampleDirection = reflect(-V, normal);
    sampleDirection = (inverse_view_matrix * float4(sampleDirection, 0.)).xyz;

    // Assume 8 level of lod (ie 256x256 texture)
    float lodval = 7. * (1. - roughness);
    return clamp(probe.sample(probe_sampler, sampleDirection, level(lodval)).rgb,
        0., 1.);
}

#endif // GE_METAL_SPECULAR_IBL_H
