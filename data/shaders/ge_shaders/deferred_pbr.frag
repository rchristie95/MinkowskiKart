layout(binding = 0) uniform sampler2D u_color;
layout(binding = 1) uniform sampler2D u_normal;
layout(binding = 2) uniform sampler2D u_depth;
layout(set = 1, binding = 7) uniform sampler2D u_ao;

layout(location = 0) out vec4 o_color;

layout(push_constant) uniform Constants
{
    int m_fullscreen_light_count;
} u_push_constants;

#include "utils/unproject_position.glsl"
#include "utils/handle_pbr.glsl"
#include "utils/sun_shadow.glsl"
#include "../utils/decodeNormal.frag"

void main()
{
    ivec2 px = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(u_depth, px, 0).x;
    if (!u_has_skybox && depth == 1.0)
        discard;
    vec4 color_data = texelFetch(u_color, px, 0);
    vec4 normal_data = texelFetch(u_normal, px, 0);
    vec3 diffuse_color = color_data.xyz;
    vec3 pbr = vec3(normal_data.zw, color_data.w);
    vec3 world_normal = DecodeNormal(normal_data.xy);
    vec3 xpos = getPosFromUVDepth(vec3(gl_FragCoord.xy, depth),
        u_camera.m_viewport, u_camera.m_inverse_projection_matrix);
    vec3 eyedir = -normalize(xpos);
    vec3 normal = (u_camera.m_view_matrix * vec4(world_normal, 0.0)).xyz;
    vec3 world_pos = (u_camera.m_inverse_view_matrix *
        vec4(xpos, 1.0)).xyz;
    // Geometric normal from screen-space derivatives for the shadow bias
    // (the G-buffer normal includes normal mapping, which would underbias
    // grazing surfaces, like the SP/OpenGL sunlightshadow geo_norm).
    vec3 pos_dx = dFdx(world_pos);
    vec3 pos_dy = dFdy(world_pos);
    vec3 geo_normal = cross(pos_dy, pos_dx);
    float geo_len = length(geo_normal);
    geo_normal = geo_len > 1e-8 ? geo_normal / geo_len : world_normal;
    if (dot(geo_normal, world_normal) < 0.0)
        geo_normal = -geo_normal;
    float sun_shadow = getSunShadowFactor(world_pos, world_normal,
        geo_normal, xpos.z);
    vec2 ao_uv = (gl_FragCoord.xy - u_camera.m_viewport.xy) /
        u_camera.m_viewport.zw;
    float ao = clamp(texture(u_ao, ao_uv).r, 0.0, 1.0);
    vec3 hdr = handlePBRDeferred(diffuse_color, pbr, world_normal, eyedir,
        normal, 1.0 - pbr.x, sun_shadow, ao);
    hdr += accumulateLights(u_push_constants.m_fullscreen_light_count,
        diffuse_color, normal, xpos, eyedir, 1.0 - pbr.x, pbr.y);
    o_color = vec4(hdr, 1.0);
}
