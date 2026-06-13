layout(binding = 0) uniform sampler2D u_color;
layout(binding = 1) uniform sampler2D u_normal;
layout(binding = 2) uniform sampler2D u_depth;

layout(location = 0) flat in int light_idx;

layout(location = 0) out vec4 o_color;

#include "utils/unproject_position.glsl"
#include "utils/handle_pbr.glsl"
#include "../utils/decodeNormal.frag"

void main()
{
    ivec2 px = ivec2(gl_FragCoord.xy);
    float depth = texelFetch(u_depth, px, 0).x;
    if (depth == 1.0)
    {
        o_color = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec4 color_data = texelFetch(u_color, px, 0);
    vec4 normal_data = texelFetch(u_normal, px, 0);
    vec3 diffuse_color = color_data.xyz;
    vec3 pbr = vec3(normal_data.zw, color_data.w);
    vec3 world_normal = DecodeNormal(normal_data.xy);
    vec3 xpos = getPosFromUVDepth(vec3(gl_FragCoord.xy, depth),
        u_camera.m_viewport, u_camera.m_inverse_projection_matrix);
    vec3 eyedir = -normalize(xpos);
    vec3 normal = (u_camera.m_view_matrix * vec4(world_normal, 0.0)).xyz;
    vec3 light = calculateLight(light_idx, diffuse_color, normal, xpos,
        eyedir, 1.0 - pbr.x, pbr.y);
    o_color = vec4(light, 1.0);
}
