// get_pos_from_uv_depth.h - MSL port of data/shaders/utils/getPosFromUVDepth.frag
// Reconstructs a (view-space) position from a UV+depth triple using the inverse
// projection matrix's diagonal/tail terms directly. Ported numerically
// identically.
//
// GLSL float4x4 indexing m[col][row] is column-major; MSL float4x4 indexing is
// also column-major, so m[0][0], m[1][1], m[2][2], m[2][3] map element-for-
// element with no transpose.
#ifndef GE_METAL_GET_POS_FROM_UV_DEPTH_H
#define GE_METAL_GET_POS_FROM_UV_DEPTH_H

#include <metal_stdlib>
using namespace metal;

inline float4 getPosFromUVDepth(float3 uvDepth,
                                float4x4 u_inverse_projection_matrix)
{
    float4 pos = 2.0 * float4(uvDepth, 1.0) - 1.0;
    pos.xy *= float2(u_inverse_projection_matrix[0][0],
                     u_inverse_projection_matrix[1][1]);
    pos.zw = float2(pos.z * u_inverse_projection_matrix[2][2] + pos.w,
                    pos.z * u_inverse_projection_matrix[2][3] + pos.w);
    pos /= pos.w;
    return pos;
}

#endif // GE_METAL_GET_POS_FROM_UV_DEPTH_H
