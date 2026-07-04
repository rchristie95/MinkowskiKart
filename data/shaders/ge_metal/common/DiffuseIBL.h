#ifndef GE_METAL_DIFFUSE_IBL_H
#define GE_METAL_DIFFUSE_IBL_H

// Faithful MSL port of data/shaders/utils/DiffuseIBL.frag.
//
// From "An Efficient Representation for Irradiance Environment Maps" article
// See http://graphics.stanford.edu/papers/envmap/
// Coefficients are calculated in IBL.cpp
//
// GLSL reads the 9 spherical-harmonics coefficients per channel as module-level
// uniforms (rL00.. / redLmn[]..). MSL has no globals, so they are supplied via a
// small constant struct. The two GLSL variants (UBO_DISABLED array form vs. the
// named-scalar form) are numerically identical; a single struct covers both.

#include <metal_stdlib>
using namespace metal;

struct GESHCoefficients
{
    // 9 coefficients per channel, order:
    // [0]=L00 [1]=L1m1 [2]=L10 [3]=L11 [4]=L2m2 [5]=L2m1 [6]=L20 [7]=L21 [8]=L22
    float red[9];
    float green[9];
    float blue[9];
};

inline float4x4 GE_DiffuseIBL_getMatrix(
    float L00, float L1m1, float L10, float L11,
    float L2m2, float L2m1, float L20, float L21, float L22)
{
    float c1 = 0.429043, c2 = 0.511664, c3 = 0.743125, c4 = 0.886227, c5 = 0.247708;

    // GLSL mat4(a,b,c,d, ...) fills column-major: each group of 4 is one column.
    // Reproduce column-by-column to match the GLSL numerically.
    return float4x4(
        float4(c1 * L22,  c1 * L2m2,  c1 * L21,  c2 * L11),
        float4(c1 * L2m2, -c1 * L22,  c1 * L2m1, c2 * L1m1),
        float4(c1 * L21,  c1 * L2m1,  c3 * L20,  c2 * L10),
        float4(c2 * L11,  c2 * L1m1,  c2 * L10,  c4 * L00 - c5 * L20)
    );
}

inline float3 DiffuseIBL(float3 normal, constant GESHCoefficients& sh)
{
    // Convert normal in world space (where SH coordinates were computed)
    float4 extendednormal = float4(normal, 0.);
    extendednormal.w = 1.;

    float4x4 rmat = GE_DiffuseIBL_getMatrix(
        sh.red[0], sh.red[1], sh.red[2], sh.red[3], sh.red[4],
        sh.red[5], sh.red[6], sh.red[7], sh.red[8]);
    float4x4 gmat = GE_DiffuseIBL_getMatrix(
        sh.green[0], sh.green[1], sh.green[2], sh.green[3], sh.green[4],
        sh.green[5], sh.green[6], sh.green[7], sh.green[8]);
    float4x4 bmat = GE_DiffuseIBL_getMatrix(
        sh.blue[0], sh.blue[1], sh.blue[2], sh.blue[3], sh.blue[4],
        sh.blue[5], sh.blue[6], sh.blue[7], sh.blue[8]);

    float r = dot(extendednormal, rmat * extendednormal);
    float g = dot(extendednormal, gmat * extendednormal);
    float b = dot(extendednormal, bmat * extendednormal);

    return max(float3(r, g, b), float3(0.));
}

#endif // GE_METAL_DIFFUSE_IBL_H
