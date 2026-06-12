// ge_glow.vert
//
// Vertex shader for the per-object glow silhouette pass (port of the
// SP/OpenGL glow pass). Reproduces the spm.vert world position (including
// the relativistic warp) so the silhouettes line up with the depth buffer,
// and forwards only the per-object glow colour.

#include "utils/camera.glsl"
#include "utils/relativity_bridge.glsl"
#include "utils/spm_data.glsl"
#include "../utils/get_world_location.vert"
#include "../utils/relativity_visual.vert"

layout(location = 0) in vec3 v_position;

layout(location = 0) flat out vec4 f_glow_color;

void main()
{
    vec4 raw_world_position = getWorldPosition(
        u_object_buffer.m_objects[gl_InstanceIndex].m_translation,
        u_object_buffer.m_objects[gl_InstanceIndex].m_rotation,
        u_object_buffer.m_objects[gl_InstanceIndex].m_scale, v_position);

    vec3 i_velocity    = u_object_buffer.m_objects[gl_InstanceIndex].m_velocity.xyz;
    float disable_rel  = u_object_buffer.m_objects[gl_InstanceIndex].m_velocity.w;
    float visual_fade  = getRelativisticVisualFade(raw_world_position.xyz,
                             i_velocity, disable_rel);
    vec4 v_world_position = applyRelativisticVisualPosition(raw_world_position,
                                i_velocity, visual_fade);

    gl_Position = u_camera.m_projection_view_matrix * v_world_position;
    // Slight pull toward the camera: the depth buffer may have been written
    // by the adaptively tessellated variants whose interior depths differ
    // marginally from the plain vertex path.
    gl_Position.z -= 1e-4 * gl_Position.w;
    f_glow_color = u_object_buffer.m_objects[gl_InstanceIndex].m_glow_color;
}
