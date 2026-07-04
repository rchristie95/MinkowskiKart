// ibl_prefilter.metal - native Metal port of the IBL cubemap prefilter compute
// shaders:
//   data/shaders/ge_shaders/diffuse_irradiance.comp   -> kernel diffuse_irradiance_main
//   data/shaders/ge_shaders/specular_prefilter.comp   -> kernel specular_prefilter_main
//
// Purpose: prefilter an environment (skybox) cubemap into
//   - a diffuse irradiance cubemap (cosine-weighted hemisphere integral), and
//   - a specular prefiltered radiance cubemap (GGX importance sampling, one
//     mip level per dispatch).
// Both write one cube face per array slice through a texture2d_array with
// access::write, exactly like the GLSL image2DArray outputs.
//
// PORTING NOTES
//  - GLSL logic is reproduced numerically identically (loop order, sample
//    formulas, weighting, mip-level math and all constants copied verbatim).
//  - GLSL push_constant block -> a constant struct (GEEnvMapPushConstants) bound
//    at [[buffer(GE_MTL_BUF_PUSH_CONSTANT)]].
//  - samplerCube uSkybox -> texturecube<float> + an explicit sampler. The GLSL
//    texture()/textureLod() calls become .sample(...) / .sample(..., level(l)).
//  - image2DArray output (rgb10_a2 / rgba8 format qualifier) -> the format
//    qualifier is dropped; the CPU picks the MTLPixelFormat. Written via
//    texture2d_array<float, access::write>.write(color, coord.xy, coord.z).
//  - GLSL local_size 16x16x1, dispatched z = 6 faces. Metal dispatches the same:
//    threads_per_threadgroup = (16,16,1); the caller sizes the grid so the
//    z dimension covers all 6 cube faces (gid.z = face index).
//  - inversesqrt/... not needed here; all math is straight from the .comp files.

#include <metal_stdlib>
using namespace metal;

#include "shared/ge_metal_bindings.h"

// ---------------------------------------------------------------------------
// environment_map.glsl helpers (ported inline so this .metal is self-contained;
// identical to data/shaders/ge_metal/common/environment_map.h).
// ---------------------------------------------------------------------------

// Push constants: face size, sample count, mip level, mip count.
struct GEIBLPrefilterPushConstants
{
    int size;            // width and height for current mipmap level
    int sampleCount;     // number of samples for integration
    int mipmapLevel;     // current mipmap level
    int mipmapCount;     // total mipmap levels
};

constant float GE_PI = 3.14159265359;

// Returns the radical inverse of "bits" with base 2.
static inline float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

// Generate a 2D Hammersley sequence value.
static inline float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

// Converts face index and UV coordinates in [0,1] to a normalized direction.
static inline float3 FaceUVtoDir(int face, float2 uv)
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

// ---------------------------------------------------------------------------
// pbr_utils.glsl helper needed by the specular prefilter (D_GGX). Ported
// numerically identically; matches data/shaders/ge_metal/shared/pbr_utils.h.
// NOTE: like the GLSL, this is called with the perceptual "roughness" value
// (the mip fraction), NOT its square. Kept verbatim to match the reference.
// ---------------------------------------------------------------------------
static inline float D_GGX(float roughness, float NdotH)
{
    float oneMinusNdotHSquared = 1.0 - NdotH * NdotH;
    float a = NdotH * roughness;
    float k = roughness / (oneMinusNdotHSquared + a * a);
    return k * k * (1.0 / 3.14159265359);
}

// A skybox sampler equivalent to the GLSL samplerCube. Linear + trilinear so
// textureLod() mip selection in the specular pass works; clamp_to_edge across
// cube faces matches the default cubemap addressing.
constant sampler ge_skybox_sampler(filter::linear,
                                   mip_filter::linear,
                                   address::clamp_to_edge);

// ===========================================================================
// diffuse_irradiance.comp -> diffuse_irradiance_main
// Cosine-weighted hemisphere integral of the skybox -> irradiance cubemap.
// ===========================================================================
kernel void diffuse_irradiance_main(
    texturecube<float>                     uSkybox        [[texture(0)]],
    texture2d_array<float, access::write>  uIrradianceMap [[texture(1)]],
    constant GEIBLPrefilterPushConstants&  pc             [[buffer(GE_MTL_BUF_PUSH_CONSTANT)]],
    uint3                                  gid            [[thread_position_in_grid]])
{
    int2 pix = int2(gid.xy);
    if (pix.x >= pc.size || pix.y >= pc.size)
        return;

    // Get normalized UV for current pixel.
    float2 uv = (float2(pix) + 0.5) / float2(pc.size, pc.size);
    int face = int(gid.z);
    float3 normal = FaceUVtoDir(face, uv);

    // Establish a tangent space basis.
    float3 up = abs(normal.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 right = normalize(cross(up, normal));
    float3 tangent = cross(normal, right);

    float3 irradiance = float3(0.0);
    float weight = 0.0;
    uint sampleCount = uint(pc.sampleCount);

    for (uint i = 0u; i < sampleCount; i++)
    {
        float2 xi = Hammersley(i, sampleCount);
        // Cosine-weighted hemisphere sampling.
        float phi = 2.0 * GE_PI * xi.x;
        float cosTheta = sqrt(1.0 - xi.y); // weight factor equals cos(theta)
        float sinTheta = sqrt(xi.y);
        float3 sampleDir = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

        // Transform sample direction from tangent space to world space.
        float3 sampleVec = normalize(right * sampleDir.x + tangent * sampleDir.y + normal * sampleDir.z);

        float3 sampleColor = uSkybox.sample(ge_skybox_sampler, sampleVec).rgb;
        irradiance += sampleColor * cosTheta;
        weight += cosTheta;
    }
    irradiance /= weight;

    uIrradianceMap.write(float4(irradiance, 1.0), uint2(pix), uint(face));
}

// ===========================================================================
// specular_prefilter.comp -> specular_prefilter_main
// GGX importance-sampled prefilter of the skybox for one mip level -> radiance.
// ===========================================================================
kernel void specular_prefilter_main(
    texturecube<float>                     uSkybox       [[texture(0)]],
    texture2d_array<float, access::write>  uPrefilterMap [[texture(1)]],
    constant GEIBLPrefilterPushConstants&  pc            [[buffer(GE_MTL_BUF_PUSH_CONSTANT)]],
    uint3                                  gid           [[thread_position_in_grid]])
{
    int2 pix = int2(gid.xy);
    if (pix.x >= pc.size || pix.y >= pc.size)
        return;

    // Get normalized UV for current pixel.
    float2 uv = (float2(pix) + 0.5) / float2(pc.size, pc.size);
    int face = int(gid.z);

    float roughness = float(pc.mipmapLevel) / float(pc.mipmapCount - 1);
    float3 R = FaceUVtoDir(face, uv);
    float3 V = normalize(R);
    // Don't need to sample skybox when roughness is 0.
    // Since it's a perfect reflector.
    if (roughness == 0.0)
    {
        float4 color = uSkybox.sample(ge_skybox_sampler, V, level(0.0));
        uPrefilterMap.write(color, uint2(pix), uint(face));
        return;
    }

    float3 prefilteredColor = float3(0.0);
    float totalWeight = 0.0;
    uint sampleCount = uint(pc.sampleCount);
    for (uint i = 0u; i < sampleCount; ++i)
    {
        float2 xi = Hammersley(i, sampleCount);
        // Importance sampling with a GGX distribution.
        float a = roughness * roughness;
        float phi = 2.0 * GE_PI * xi.x;
        // GGX importance sampling for the cosine of the angle.
        float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
        float3 H = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

        // Construct TBN basis from view direction V.
        float3 up = abs(V.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
        float3 tangent = normalize(cross(up, V));
        float3 bitangent = cross(V, tangent);
        float3x3 TBN = float3x3(tangent, bitangent, V);
        H = normalize(TBN * H);

        // Compute reflection vector L.
        float3 L = normalize(reflect(-V, H));
        float NdotL = max(dot(V, L), 0.0);

        if (NdotL > 0.0)
        {
            // Calculate the mip level based on the PDF.
            // In a skybox/environment map scenario, N = V.
            float NoH = max(dot(V, H), 0.0);
            float VoH = max(dot(V, H), 0.0);

            float D = D_GGX(roughness, NoH);
            float pdf = D * NoH / (4.0 * VoH);
            float omegaS = 1.0 / (float(sampleCount) * pdf);
            float omegaP = 4.0 * GE_PI / (6.0 * float(pc.size * pc.size));
            float mipLevel = 0.5 * log2(omegaS / omegaP);

            float3 sampleColor = uSkybox.sample(ge_skybox_sampler, L, level(mipLevel)).rgb;
            prefilteredColor += sampleColor * NdotL;
            totalWeight += NdotL;
        }
    }
    prefilteredColor = prefilteredColor / totalWeight;

    uPrefilterMap.write(float4(prefilteredColor, 1.0), uint2(pix), uint(face));
}
