// environment_map.h - MSL port of
// data/shaders/ge_shaders/utils/environment_map.glsl
// Cubemap prefiltering helpers (Hammersley/GGX importance sampling support).
// Ported numerically identically.
//
// The GLSL layout(push_constant) block becomes a plain constant struct the
// compute kernel binds at [[buffer(GE_MTL_BUF_PUSH)]]; the image-format
// qualifiers on the compute output image are dropped (the CPU sets the
// MTLPixelFormat) and cubemap faces are written through a texture2d_array with
// access::write.
#ifndef GE_METAL_ENVIRONMENT_MAP_H
#define GE_METAL_ENVIRONMENT_MAP_H

#include <metal_stdlib>
using namespace metal;

#include "ge_metal_bindings.h"

// Push constants: face index, dimensions, and sample count.
struct GEEnvMapPushConstants
{
    int size;            // width and height for current mipmap level
    int sampleCount;     // number of samples for integration
    int mipmapLevel;     // current mipmap level
    int mipmapCount;     // total mipmap levels
};

// Returns the radical inverse of "bits" with base 2.
inline float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

// Generate a 2D Hammersley sequence value.
inline float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

// Converts face index and UV coordinates in [0,1] to a normalized direction.
inline float3 FaceUVtoDir(int face, float2 uv)
{
    // Map UV from [0, 1] to [-1, 1]
    uv = uv * 2.0 - 1.0;
    float3 dir = float3(0.0);
    if (face == 0)        // +X
        dir = float3(1.0, -uv.y, -uv.x);
    else if (face == 1)   // -X
        dir = float3(-1.0, -uv.y, uv.x);
    else if (face == 2)   // +Y
        dir = float3(uv.x, 1.0, uv.y);
    else if (face == 3)   // -Y
        dir = float3(uv.x, -1.0, -uv.y);
    else if (face == 4)   // +Z
        dir = float3(uv.x, -uv.y, 1.0);
    else if (face == 5)   // -Z
        dir = float3(-uv.x, -uv.y, -1.0);
    return normalize(dir);
}

constant float PI = 3.14159265359;

#endif // GE_METAL_ENVIRONMENT_MAP_H
