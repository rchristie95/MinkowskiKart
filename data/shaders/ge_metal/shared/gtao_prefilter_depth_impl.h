#ifndef HEADER_GE_METAL_GTAO_PREFILTER_DEPTH_IMPL_H
#define HEADER_GE_METAL_GTAO_PREFILTER_DEPTH_IMPL_H

// ============================================================================
// MSL port of data/shaders/ge_shaders/utils/gtao_prefilter_depth_impl.glsl
//
// Half-res linear-depth prefilter: for each half-res output texel, take the
// nearest (min view-depth) of the 2x2 full-res block, skipping the far plane
// (raw depth >= 1.0). Empty blocks write 0.0. Math is identical to the GLSL.
//
// The kernel wrapper (declared here as [[kernel]]) is the shader entry point;
// it forwards its bindings to the shared helpers in gtao_common.h. A single
// [[kernel]] serves both the r16f and r32f variants because the MTLPixelFormat
// is chosen CPU-side (the GLSL AO_IMAGE_FORMAT qualifier is dropped per the
// Metal porting rules).
//
// Dispatch: GLSL local_size 8x8x1 -> threadgroup size (8,8,1); the CPU dispatches
// ceil(out_size/8) threadgroups. The bounds check is preserved regardless.
// ============================================================================

#include "gtao_common.h"

// out0 is written; the other write target (out1) is unused by this pass but is
// listed in the shared texture table, so it is accepted and ignored here.
inline void gtaoPrefilterDepth(uint2 gid_u,
                               constant GTAOConstants& u_pc,
                               texture2d<float> u_depth,
                               texture2d<float, access::write> u_out0)
{
    int2 gid = int2(gid_u);
    int2 out_size = int2(u_out0.get_width(), u_out0.get_height());
    if (gid.x >= out_size.x || gid.y >= out_size.y)
        return;

    int2 base_px = int2(u_pc.m_viewport.xy) + gid * 2;
    float best_depth = 1e20;
    for (int y = 0; y < 2; y++)
    {
        for (int x = 0; x < 2; x++)
        {
            int2 px = clampScreenPixel(u_pc, base_px + int2(x, y));
            float raw_depth = u_depth.read(uint2(px)).x;
            if (raw_depth >= 1.0)
                continue;
            best_depth = min(best_depth, viewDepthFromScreen(u_pc, u_depth, px));
        }
    }

    if (best_depth == 1e20)
        best_depth = 0.0;
    u_out0.write(float4(best_depth), gid_u);
}

// ----------------------------------------------------------------------------
// Entry point. Matches GLSL main() + local_size 8x8x1.
kernel void gtao_prefilter_depth_main(
    uint2 gid                                        [[thread_position_in_grid]],
    constant GTAOConstants& u_pc                     [[buffer(GTAO_BUF_CONSTANTS)]],
    texture2d<float> u_depth                         [[texture(GTAO_TEX_DEPTH)]],
    texture2d<float, access::write> u_out0           [[texture(GTAO_TEX_OUT0)]])
{
    gtaoPrefilterDepth(gid, u_pc, u_depth, u_out0);
}

#endif // HEADER_GE_METAL_GTAO_PREFILTER_DEPTH_IMPL_H
