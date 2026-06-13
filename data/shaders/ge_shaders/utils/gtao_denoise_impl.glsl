layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include "gtao_common.glsl"

void main()
{
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    ivec2 out_size = imageSize(u_out0);
    if (gid.x >= out_size.x || gid.y >= out_size.y)
        return;

    vec2 inv_size = 1.0 / vec2(out_size);
    vec2 uv = (vec2(gid) + vec2(0.5)) * inv_size;
    ivec2 screen_px = clampScreenPixel(ivec2(u_pc.m_viewport.xy +
        (vec2(gid) + vec2(0.5)) * 2.0));
    float center_depth = texelFetch(u_linear_depth, gid, 0).r;
    vec3 center_normal = viewNormalFromScreen(screen_px);

    float sum = 0.0;
    float weight_sum = 0.0;
    for (int y = -2; y <= 2; y++)
    {
        for (int x = -2; x <= 2; x++)
        {
            ivec2 sample_id = clamp(gid + ivec2(x, y), ivec2(0),
                out_size - ivec2(1));
            ivec2 sample_screen = clampScreenPixel(ivec2(u_pc.m_viewport.xy +
                (vec2(sample_id) + vec2(0.5)) * 2.0));
            float sample_depth = texelFetch(u_linear_depth, sample_id, 0).r;
            vec3 sample_normal = viewNormalFromScreen(sample_screen);
            float depth_w = exp(-abs(sample_depth - center_depth) * 2.5);
            float normal_w = pow(max(dot(sample_normal, center_normal), 0.0),
                8.0);
            vec2 o = vec2(x, y);
            float spatial_w = exp(-dot(o, o) * 0.22);
            float w = spatial_w * depth_w * normal_w;
            sum += texture(u_input, (vec2(sample_id) + vec2(0.5)) *
                inv_size).r * w;
            weight_sum += w;
        }
    }

    imageStore(u_out0, gid, vec4(sum / max(weight_sum, 1e-4)));
}
