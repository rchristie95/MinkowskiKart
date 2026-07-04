#ifndef GE_METAL_DIFFUSE_BRDF_H
#define GE_METAL_DIFFUSE_BRDF_H

// Faithful MSL port of data/shaders/utils/DiffuseBRDF.frag.

#include <metal_stdlib>
using namespace metal;

// Lambert model
inline float3 DiffuseBRDF(float3 normal, float3 eyedir, float3 lightdir,
                          float3 color, float roughness)
{
    return color;
}

#endif // GE_METAL_DIFFUSE_BRDF_H
