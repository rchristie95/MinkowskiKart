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
    // Reconstruct the view ray exactly like the SP/OpenGL sky shader
    // (sky.frag): unproject to a *view-space* ray, then rotate it to world
    // space with w = 0 so the camera's world position drops out completely.
    // The skybox only depends on camera orientation, so this is correct and,
    // crucially, stable. The previous approach unprojected two clip points to
    // *world* space with the full inverse projection-view matrix and subtracted
    // them; both points carry the camera's world position, so at large world
    // coordinates (e.g. Shifting Sands) the subtraction is catastrophic
    // cancellation. Tiny per-frame camera jitter (suspension settle, FP
    // rounding) then made the sampled direction vibrate -> a shuddering skybox
    // at rest, on Vulkan only (OpenGL's sky.frag never had it).
    vec4 view_pos = u_camera.m_inverse_projection_matrix * vec4(uv, 1.0, 1.0);
    vec3 dir = normalize((u_camera.m_inverse_view_matrix *
        vec4(view_pos.xyz / view_pos.w, 0.0)).xyz);
    // Aberrate the view ray exactly like sky.frag does. Without this the sky
    // stays pinned to the camera while all scene geometry swings with the
    // observer's beta, which reads as the skybox sliding around.
    dir = transformObserverRayToWorldDirection(dir);
    if (u_deferred)
        o_color = texture(f_skybox_texture_srgb, dir);
    else
        o_color = texture(f_skybox_texture, dir);
}
