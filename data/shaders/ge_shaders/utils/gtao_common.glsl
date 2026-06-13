layout(binding = 0) uniform sampler2D u_depth;
layout(binding = 1) uniform sampler2D u_normal;
layout(binding = 2) uniform sampler2D u_linear_depth;
layout(binding = 3) uniform sampler2D u_input;
layout(binding = 4) uniform sampler2D u_history;
layout(binding = 5, AO_IMAGE_FORMAT) uniform writeonly image2D u_out0;
layout(binding = 6, AO_IMAGE_FORMAT) uniform writeonly image2D u_out1;

layout(std140, binding = 7) uniform GTAOConstants
{
    mat4 m_inverse_projection;
    mat4 m_inverse_view;
    mat4 m_projection_view;
    mat4 m_previous_projection_view;
    vec4 m_viewport;
    vec4 m_screen;
    vec4 m_params0; // radius, intensity, temporal blend, frame index
    vec4 m_params1; // half width, half height, reset history, unused
} u_pc;

vec3 DecodeGTAONormal(vec2 n)
{
    n = n * 2.0 - 1.0;
    vec3 ret = vec3(n.x, n.y, 1.0 - abs(n.x) - abs(n.y));
    float t = max(-ret.z, 0.0);
    ret.x += ret.x >= 0.0 ? -t : t;
    ret.y += ret.y >= 0.0 ? -t : t;
    return normalize(ret);
}

ivec2 clampScreenPixel(ivec2 px)
{
    ivec2 lo = ivec2(u_pc.m_viewport.xy);
    ivec2 hi = ivec2(u_pc.m_viewport.xy + u_pc.m_viewport.zw) - ivec2(1);
    return clamp(px, lo, hi);
}

vec3 viewPosFromScreen(ivec2 px)
{
    px = clampScreenPixel(px);
    float z = texelFetch(u_depth, px, 0).r;
    vec2 ndc = ((vec2(px) + vec2(0.5) - u_pc.m_viewport.xy) /
        u_pc.m_viewport.zw) * 2.0 - 1.0;
    vec4 view_pos = u_pc.m_inverse_projection * vec4(ndc, z, 1.0);
    float inv_w = abs(view_pos.w) > 1e-6 ? 1.0 / view_pos.w : 0.0;
    return view_pos.xyz * inv_w;
}

float viewDepthFromScreen(ivec2 px)
{
    return abs(viewPosFromScreen(px).z);
}

vec3 viewNormalFromScreen(ivec2 px)
{
    vec3 world_normal = DecodeGTAONormal(texelFetch(u_normal,
        clampScreenPixel(px), 0).xy);
    return normalize(transpose(mat3(u_pc.m_inverse_view)) * world_normal);
}

float interleavedGradientNoise(vec2 p)
{
    const vec3 m = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(m.z * fract(dot(p, m.xy)));
}

float r2Noise(vec2 p, float frame)
{
    return fract(interleavedGradientNoise(p) + frame * 0.754877666);
}
