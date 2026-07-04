// get_cie_xyz.h - MSL port of data/shaders/utils/getCIEXYZ.frag
// RGB -> CIE Yxy. Numerical values from
// http://content.gpwiki.org/index.php/D3DBook:High-Dynamic_Range_Rendering
// Ported numerically identically.
//
// GLSL transpose(mat3(col0, col1, col2)) and MSL transpose(float3x3(...)) share
// the same column-major convention, so the matrix and the matrix*vector product
// map element-for-element.
#ifndef GE_METAL_GET_CIE_XYZ_H
#define GE_METAL_GET_CIE_XYZ_H

#include <metal_stdlib>
using namespace metal;

inline float3 getCIEYxy(float3 rgbColor)
{
    float3x3 RGB2XYZ = transpose(float3x3(
        float3(.4125, .2126, .0193),
        float3(.3576, .7152, .1192),
        float3(.1805, .0722, .9505)));

    float3 xYz = RGB2XYZ * rgbColor;
    float tmp = max(xYz.x + xYz.y + xYz.z, 0.1);
    return float3(xYz.y, xYz.xy / tmp);
}

#endif // GE_METAL_GET_CIE_XYZ_H
