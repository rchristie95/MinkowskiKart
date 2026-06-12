layout (input_attachment_index = 0, binding = 0) uniform subpassInput u_color;
layout (input_attachment_index = 1, binding = 1) uniform subpassInput u_normal;
layout (input_attachment_index = 2, binding = 2) uniform subpassInput u_depth;

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
    float depth = subpassLoad(u_depth).x;
    if (!u_has_skybox && depth == 1.0)
        discard;
    vec3 diffuse_color = subpassLoad(u_color).xyz;
    vec3 pbr = vec3(subpassLoad(u_normal).zw, subpassLoad(u_color).w);
    vec3 world_normal = DecodeNormal(subpassLoad(u_normal).xy);
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
    vec3 hdr = handlePBRDeferred(diffuse_color, pbr, world_normal, eyedir,
        normal, 1.0 - pbr.x, sun_shadow);
    hdr += accumulateLights(u_push_constants.m_fullscreen_light_count,
        diffuse_color, normal, xpos, eyedir, 1.0 - pbr.x, pbr.y);
    o_color = vec4(hdr, 1.0);
}
