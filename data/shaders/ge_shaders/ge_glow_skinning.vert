// ge_glow_skinning.vert
//
// Skinned variant of ge_glow.vert (see there); reproduces the
// spm_skinning.vert world position and forwards the per-object glow colour.

#include "utils/camera.glsl"
#include "utils/relativity_bridge.glsl"
#include "utils/spm_data.glsl"
#include "../utils/get_world_location.vert"
#include "../utils/relativity_visual.vert"

layout(location = 0) in vec3 v_position;
layout(location = 6) in ivec4 v_joint;
layout(location = 7) in vec4 v_weight;

layout(location = 0) flat out vec4 f_glow_color;

void main()
{
    int offset = u_object_buffer.m_objects[gl_InstanceIndex].m_skinning_offset;
    mat4 joint_matrix =
        v_weight[0] * u_skinning_matrices.m_mat[max(v_joint[0] + offset, 0)] +
        v_weight[1] * u_skinning_matrices.m_mat[max(v_joint[1] + offset, 0)] +
        v_weight[2] * u_skinning_matrices.m_mat[max(v_joint[2] + offset, 0)] +
        v_weight[3] * u_skinning_matrices.m_mat[max(v_joint[3] + offset, 0)];
    vec4 v_skinning_position = joint_matrix * vec4(v_position, 1.0);
    vec4 raw_world_position = getWorldPosition(
        u_object_buffer.m_objects[gl_InstanceIndex].m_translation,
        u_object_buffer.m_objects[gl_InstanceIndex].m_rotation,
        u_object_buffer.m_objects[gl_InstanceIndex].m_scale,
        v_skinning_position.xyz);

    vec3 i_velocity    = u_object_buffer.m_objects[gl_InstanceIndex].m_velocity.xyz;
    float disable_rel  = u_object_buffer.m_objects[gl_InstanceIndex].m_velocity.w;
    float visual_fade  = getRelativisticVisualFade(raw_world_position.xyz,
                             i_velocity, disable_rel);
    vec4 v_world_position = applyRelativisticVisualPosition(raw_world_position,
                                i_velocity, visual_fade);

    gl_Position = u_camera.m_projection_view_matrix * v_world_position;
    gl_Position.z -= 1e-4 * gl_Position.w;
    f_glow_color = u_object_buffer.m_objects[gl_InstanceIndex].m_glow_color;
}
