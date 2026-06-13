layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include "gtao_common.glsl"

void main()
{
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    ivec2 out_size = imageSize(u_out0);
    if (gid.x >= out_size.x || gid.y >= out_size.y)
        return;

    ivec2 base_px = ivec2(u_pc.m_viewport.xy) + gid * 2;
    float best_depth = 1e20;
    for (int y = 0; y < 2; y++)
    {
        for (int x = 0; x < 2; x++)
        {
            ivec2 px = clampScreenPixel(base_px + ivec2(x, y));
            float raw_depth = texelFetch(u_depth, px, 0).r;
            if (raw_depth >= 1.0)
                continue;
            best_depth = min(best_depth, viewDepthFromScreen(px));
        }
    }

    if (best_depth == 1e20)
        best_depth = 0.0;
    imageStore(u_out0, gid, vec4(best_depth));
}
