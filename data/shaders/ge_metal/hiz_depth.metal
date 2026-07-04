#include <metal_stdlib>
using namespace metal;

#include "shared/ge_metal_bindings.h"

// ============================================================================
// HiZ min-reduction compute (r32f).
//
// Faithful MSL port of ge_shaders/hiz_depth.comp.
//
// GLSL -> MSL mapping:
//   layout(local_size_x=16, local_size_y=16, local_size_z=1)
//       -> dispatched with a 16x16x1 threadgroup; gl_GlobalInvocationID.xy
//          -> [[thread_position_in_grid]].xy
//   layout(binding=0) sampler2D u_depth
//       -> texture2d<float> u_depth [[texture(0)]] (only texelFetch is used, so
//          integer .read(coord, lod) — no sampler required)
//   layout(binding=1, r32f) writeonly image2D u_hiz_depth
//       -> texture2d<float, access::write> u_hiz_depth [[texture(1)]]
//          (r32f image-format qualifier dropped; CPU sets MTLPixelFormat)
//   layout(push_constant) ivec3 u_offset_miplevel
//       -> constant PushConstants& at buffer(GE_MTL_BUF_PUSH_CONSTANT)
//   imageSize(u_hiz_depth)          -> int2(get_width(), get_height())
//   textureSize(u_depth, prev_level)-> int2(get_width(l), get_height(l))
//   texelFetch(u_depth, c, l).r     -> u_depth.read(uint2(c), uint(l)).r
//   imageStore(u_hiz_depth, dst, v) -> u_hiz_depth.write(v, uint2(dst))
//
// The math is reproduced verbatim; only builtin names change.
// ============================================================================

struct PushConstants
{
    int3 u_offset_miplevel;
};

kernel void hiz_depth_main(
    texture2d<float>              u_depth      [[texture(0)]],
    texture2d<float, access::write> u_hiz_depth [[texture(1)]],
    constant PushConstants&       pc           [[buffer(GE_MTL_BUF_PUSH_CONSTANT)]],
    uint2                         gid          [[thread_position_in_grid]])
{
    int2 dst = int2(gid);
    int2 current_size = int2(u_hiz_depth.get_width(), u_hiz_depth.get_height());

    if (dst.x >= current_size.x || dst.y >= current_size.y)
        return;

    if (pc.u_offset_miplevel.z == 0)
    {
        // at level 0, do a 1:1 copy
        int2 c = dst + pc.u_offset_miplevel.xy;
        float d = u_depth.read(uint2(c), 0).r;
        u_hiz_depth.write(float4(d), uint2(dst));
    }
    else
    {
        // at higher mip levels, read a 2x2 block from previous level
        int2 src = dst * 2;
        int prev_level = pc.u_offset_miplevel.z - 1;
        int2 prev_size = int2(u_depth.get_width(uint(prev_level)),
                              u_depth.get_height(uint(prev_level)));
        float d0 = u_depth.read(uint2(src + int2(0, 0)), uint(prev_level)).r;
        float d1 = u_depth.read(uint2(src + int2(1, 0)), uint(prev_level)).r;
        float d2 = u_depth.read(uint2(src + int2(0, 1)), uint(prev_level)).r;
        float d3 = u_depth.read(uint2(src + int2(1, 1)), uint(prev_level)).r;
        float min_depth = min(min(d0, d1), min(d2, d3));
        //float max_depth = max(max(d0, d1), max(d2, d3));
        bool extra_sample_x = (current_size.x * 2) < prev_size.x;
        bool extra_sample_y = (current_size.y * 2) < prev_size.y;
        if (extra_sample_x)
        {
            float d4 = u_depth.read(uint2(src + int2(2, 0)), uint(prev_level)).r;
            float d5 = u_depth.read(uint2(src + int2(2, 1)), uint(prev_level)).r;
            min_depth = min(min_depth, min(d4, d5));
            //max_depth = max(max_depth, max(d4, d5));
        }
        if (extra_sample_y)
        {
            float d6 = u_depth.read(uint2(src + int2(0, 2)), uint(prev_level)).r;
            float d7 = u_depth.read(uint2(src + int2(1, 2)), uint(prev_level)).r;
            min_depth = min(min_depth, min(d6, d7));
            //max_depth = max(max_depth, max(d6, d7));
        }
        if (extra_sample_x && extra_sample_y)
        {
            float d8 = u_depth.read(uint2(src + int2(2, 2)), uint(prev_level)).r;
            min_depth = min(min_depth, d8);
            //max_depth = max(max_depth, d8);
        }
        u_hiz_depth.write(float4(min_depth), uint2(dst));
        //u_hiz_depth.write(float4(min_depth, max_depth, 0.0, 0.0), uint2(dst));
    }
}
