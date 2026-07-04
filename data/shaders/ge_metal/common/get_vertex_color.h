// get_vertex_color.h - MSL port of
// data/shaders/ge_shaders/utils/get_vertex_color.glsl
// Unpacks a 0xAARRGGBB uint into an RGBA float4. Ported numerically
// identically. Note: this reads the packed integer channels directly (A=bits
// 24..31, R=16..23, G=8..15, B=0..7), independent of the S3DVertex BGRA
// in-memory .zyxw swizzle used elsewhere.
#ifndef GE_METAL_GET_VERTEX_COLOR_H
#define GE_METAL_GET_VERTEX_COLOR_H

#include <metal_stdlib>
using namespace metal;

inline float4 getVertexColor(uint packed)
{
    float4 vertex_color;
    vertex_color.a = float(packed >> 24) / 255.0;
    vertex_color.r = float((packed >> 16) & 0xff) / 255.0;
    vertex_color.g = float((packed >> 8) & 0xff) / 255.0;
    vertex_color.b = float(packed & 0xff) / 255.0;
    return vertex_color;
}

#endif // GE_METAL_GET_VERTEX_COLOR_H
