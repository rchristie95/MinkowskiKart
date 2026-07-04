// get_rgb_from_cie_xxy.h - MSL port of data/shaders/utils/getRGBfromCIEXxy.frag
// CIE Yxy -> RGB. Numerical values from
// http://content.gpwiki.org/index.php/D3DBook:High-Dynamic_Range_Rendering
// Ported numerically identically.
//
// GLSL transpose(mat3(col0, col1, col2)) and MSL transpose(float3x3(...)) share
// the same column-major convention, so the matrix and the matrix*vector product
// map element-for-element.
#ifndef GE_METAL_GET_RGB_FROM_CIE_XXY_H
#define GE_METAL_GET_RGB_FROM_CIE_XXY_H

#include <metal_stdlib>
using namespace metal;

inline float3 getRGBFromCIEXxy(float3 YxyColor)
{
    float Yovery = YxyColor.x / max(YxyColor.z, 0.1);
    float3 XYZ = float3(YxyColor.y * Yovery, YxyColor.x,
        (1. - YxyColor.y - YxyColor.z) * Yovery);

    float3x3 XYZ2RGB = transpose(float3x3(
        float3(3.2405, -.9693, .0556),
        float3(-1.5371, 1.8760, -.2040),
        float3(-.4985, .0416, 1.0572)));

    float3 RGBColor = XYZ2RGB * XYZ;
    return max(RGBColor, float3(0.));
}

#endif // GE_METAL_GET_RGB_FROM_CIE_XXY_H
