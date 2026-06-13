layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include "gtao_common.glsl"

float currentAO(ivec2 gid, float center_depth, vec3 center_normal,
                out float min_ao, out float max_ao)
{
    ivec2 half_size = textureSize(u_input, 0);
    vec2 half_uv = (vec2(gid) + vec2(0.5)) /
        max(u_pc.m_viewport.zw, vec2(1.0));
    vec2 half_px = half_uv * vec2(half_size) - vec2(0.5);
    ivec2 base = ivec2(floor(half_px));

    float sum = 0.0;
    float weight_sum = 0.0;
    min_ao = 1.0;
    max_ao = 0.0;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            ivec2 sid = clamp(base + ivec2(x, y), ivec2(0),
                half_size - ivec2(1));
            ivec2 sample_screen = clampScreenPixel(ivec2(u_pc.m_viewport.xy +
                (vec2(sid) + vec2(0.5)) * 2.0));
            float sample_depth = texelFetch(u_linear_depth, sid, 0).r;
            vec3 sample_normal = viewNormalFromScreen(sample_screen);
            float ao = texelFetch(u_input, sid, 0).r;
            float depth_w = exp(-abs(sample_depth - center_depth) * 3.0);
            float normal_w = pow(max(dot(sample_normal, center_normal), 0.0),
                10.0);
            vec2 frac_delta = vec2(sid) + vec2(0.5) - half_px;
            float spatial_w = exp(-dot(frac_delta, frac_delta) * 1.35);
            float w = depth_w * normal_w * spatial_w;
            sum += ao * w;
            weight_sum += w;
            min_ao = min(min_ao, ao);
            max_ao = max(max_ao, ao);
        }
    }
    return sum / max(weight_sum, 1e-4);
}

bool reprojectHistory(ivec2 gid, vec3 view_pos, vec3 normal,
                      out vec2 history_uv)
{
    vec4 world = u_pc.m_inverse_view * vec4(view_pos, 1.0);
    vec4 prev_clip = u_pc.m_previous_projection_view * world;
    if (prev_clip.w <= 1e-4)
        return false;
    vec2 prev_ndc = prev_clip.xy / prev_clip.w;
    history_uv = prev_ndc * 0.5 + 0.5;
    if (any(lessThan(history_uv, vec2(0.0))) ||
        any(greaterThan(history_uv, vec2(1.0))))
        return false;

    ivec2 prev_screen = clampScreenPixel(ivec2(u_pc.m_viewport.xy +
        history_uv * u_pc.m_viewport.zw));
    float prev_depth_now = viewDepthFromScreen(prev_screen);
    float cur_depth = abs(view_pos.z);
    if (abs(prev_depth_now - cur_depth) > max(0.18, cur_depth * 0.035))
        return false;

    vec3 prev_normal_now = viewNormalFromScreen(prev_screen);
    if (dot(prev_normal_now, normal) < 0.78)
        return false;
    return true;
}

void main()
{
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    ivec2 out_size = imageSize(u_out0);
    if (gid.x >= out_size.x || gid.y >= out_size.y)
        return;

    ivec2 screen_px = clampScreenPixel(ivec2(u_pc.m_viewport.xy) + gid);
    float raw_depth = texelFetch(u_depth, screen_px, 0).r;
    if (raw_depth >= 1.0)
    {
        imageStore(u_out0, gid, vec4(1.0));
        imageStore(u_out1, gid, vec4(1.0));
        return;
    }

    vec3 view_pos = viewPosFromScreen(screen_px);
    float center_depth = abs(view_pos.z);
    vec3 normal = viewNormalFromScreen(screen_px);
    float min_ao;
    float max_ao;
    float ao = currentAO(gid, center_depth, normal, min_ao, max_ao);

    if (u_pc.m_params1.z < 0.5)
    {
        vec2 history_uv;
        if (reprojectHistory(gid, view_pos, normal, history_uv))
        {
            float history_ao = texture(u_history, history_uv).r;
            history_ao = clamp(history_ao, min_ao - 0.04, max_ao + 0.04);
            ao = mix(ao, history_ao, u_pc.m_params0.z);
        }
    }

    ao = clamp(ao, 0.0, 1.0);
    imageStore(u_out0, gid, vec4(ao));
    imageStore(u_out1, gid, vec4(ao));
}
