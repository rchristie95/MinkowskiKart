layout(location = 0) in vec2 f_uv;
layout(binding = 2) uniform samplerCube f_skybox_texture;
layout(binding = 3) uniform samplerCube f_skybox_texture_srgb;

layout(location = 0) out vec4 o_color;

#include "utils/camera.glsl"
#include "utils/constants_utils.glsl"
#include "utils/relativity_bridge.glsl"
#include "../utils/relativity_visual.vert"

void main()
{
    vec2 uv = 2.0f * f_uv - 1.0f;
    vec4 front = u_camera.m_inverse_projection_view_matrix * vec4(uv, -1.0, 1.0);
    vec4 back = u_camera.m_inverse_projection_view_matrix * vec4(uv, 1.0, 1.0);
    vec3 dir = back.xyz / back.w - front.xyz / front.w;
    // Aberrate the view ray exactly like the SP/OpenGL sky shader (sky.frag)
    // does. Without this the sky stays pinned to the camera while all scene
    // geometry swings with the observer's beta, which reads as the skybox
    // sliding around (and as constant shake from per-tick beta noise).
    dir = transformObserverRayToWorldDirection(dir);
    if (u_deferred)
        o_color = texture(f_skybox_texture_srgb, dir);
    else
        o_color = texture(f_skybox_texture, dir);
}
