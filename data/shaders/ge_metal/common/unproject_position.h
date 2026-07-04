// unproject_position.h - MSL port of
// data/shaders/ge_shaders/utils/unproject_position.glsl
// Reconstructs view-space positions from a fragment coordinate (or UV+depth)
// and a viewport, via the full inverse projection matrix. Ported numerically
// identically.
//
// frag_coord is the equivalent of gl_FragCoord: xy = window-space pixel centre,
// w = 1/clip.w (as supplied by the caller); this matches the GLSL source which
// divides by frag_coord.w in getPosFromFragCoord.
#ifndef GE_METAL_UNPROJECT_POSITION_H
#define GE_METAL_UNPROJECT_POSITION_H

#include <metal_stdlib>
using namespace metal;

inline float3 getPosFromFragCoord(float4 frag_coord, float4 viewport,
                                  float4x4 inverse_projection_matrix)
{
    float2 ndc = float2((frag_coord.x - viewport.x) / viewport.z * 2.0 - 1.0,
        (frag_coord.y - viewport.y) / viewport.w * 2.0 - 1.0);
    float4 clip = float4(ndc, 1.0, 1.0);
    float4 view_space = inverse_projection_matrix * clip;
    return view_space.xyz / frag_coord.w;
}

inline float3 getPosFromUVDepth(float3 uv_depth, float4 viewport,
                                float4x4 inverse_projection_matrix)
{
    float2 ndc = float2((uv_depth.x - viewport.x) / viewport.z * 2.0 - 1.0,
        (uv_depth.y - viewport.y) / viewport.w * 2.0 - 1.0);
    float4 clip = float4(ndc, uv_depth.z, 1.0);
    float4 view_space = inverse_projection_matrix * clip;
    return view_space.xyz / view_space.w;
}

#endif // GE_METAL_UNPROJECT_POSITION_H
