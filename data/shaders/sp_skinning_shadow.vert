uniform int layer;

#ifdef SKINNING_TBO_DISABLED
uniform sampler2D skinning_tex;
#else
uniform samplerBuffer skinning_tex;
#endif

layout(location = 0) in vec3 i_position;
layout(location = 3) in vec2 i_uv;
layout(location = 6) in ivec4 i_joint;
layout(location = 7) in vec4 i_weight;
layout(location = 8) in vec3 i_origin;
layout(location = 9) in vec4 i_rotation;
layout(location = 10) in vec4 i_scale;
layout(location = 12) in ivec2 i_misc_data;
layout(location = 13) in vec3 i_velocity;

#stk_include "utils/get_world_location.vert"
#stk_include "utils/relativity_visual.vert"

out vec2 uv;

void main()
{
    vec4 idle_position = vec4(i_position, 1.0);
    vec4 skinned_position = vec4(0.0);
    int skinning_offset = i_misc_data.x;

#ifdef SKINNING_TBO_DISABLED
    mat4 joint_matrix =
        i_weight[0] * mat4(
        texelFetch(skinning_tex, ivec2(0, clamp(i_joint[0] + skinning_offset, 0, MAX_BONES)), 0),
        texelFetch(skinning_tex, ivec2(1, clamp(i_joint[0] + skinning_offset, 0, MAX_BONES)), 0),
        texelFetch(skinning_tex, ivec2(2, clamp(i_joint[0] + skinning_offset, 0, MAX_BONES)), 0),
        texelFetch(skinning_tex, ivec2(3, clamp(i_joint[0] + skinning_offset, 0, MAX_BONES)), 0)) +
        i_weight[1] * mat4(
        texelFetch(skinning_tex, ivec2(0, clamp(i_joint[1] + skinning_offset, 0, MAX_BONES)), 0),
        texelFetch(skinning_tex, ivec2(1, clamp(i_joint[1] + skinning_offset, 0, MAX_BONES)), 0),
        texelFetch(skinning_tex, ivec2(2, clamp(i_joint[1] + skinning_offset, 0, MAX_BONES)), 0),
        texelFetch(skinning_tex, ivec2(3, clamp(i_joint[1] + skinning_offset, 0, MAX_BONES)), 0)) +
        i_weight[2] * mat4(
        texelFetch(skinning_tex, ivec2(0, clamp(i_joint[2] + skinning_offset, 0, MAX_BONES)), 0),
        texelFetch(skinning_tex, ivec2(1, clamp(i_joint[2] + skinning_offset, 0, MAX_BONES)), 0),
        texelFetch(skinning_tex, ivec2(2, clamp(i_joint[2] + skinning_offset, 0, MAX_BONES)), 0),
        texelFetch(skinning_tex, ivec2(3, clamp(i_joint[2] + skinning_offset, 0, MAX_BONES)), 0)) +
        i_weight[3] * mat4(
        texelFetch(skinning_tex, ivec2(0, clamp(i_joint[3] + skinning_offset, 0, MAX_BONES)), 0),
        texelFetch(skinning_tex, ivec2(1, clamp(i_joint[3] + skinning_offset, 0, MAX_BONES)), 0),
        texelFetch(skinning_tex, ivec2(2, clamp(i_joint[3] + skinning_offset, 0, MAX_BONES)), 0),
        texelFetch(skinning_tex, ivec2(3, clamp(i_joint[3] + skinning_offset, 0, MAX_BONES)), 0));
#else
    mat4 joint_matrix =
        i_weight[0] * mat4(
        texelFetch(skinning_tex, clamp(i_joint[0] + skinning_offset, 0, MAX_BONES) * 4),
        texelFetch(skinning_tex, clamp(i_joint[0] + skinning_offset, 0, MAX_BONES) * 4 + 1),
        texelFetch(skinning_tex, clamp(i_joint[0] + skinning_offset, 0, MAX_BONES) * 4 + 2),
        texelFetch(skinning_tex, clamp(i_joint[0] + skinning_offset, 0, MAX_BONES) * 4 + 3)) +
        i_weight[1] * mat4(
        texelFetch(skinning_tex, clamp(i_joint[1] + skinning_offset, 0, MAX_BONES) * 4),
        texelFetch(skinning_tex, clamp(i_joint[1] + skinning_offset, 0, MAX_BONES) * 4 + 1),
        texelFetch(skinning_tex, clamp(i_joint[1] + skinning_offset, 0, MAX_BONES) * 4 + 2),
        texelFetch(skinning_tex, clamp(i_joint[1] + skinning_offset, 0, MAX_BONES) * 4 + 3)) +
        i_weight[2] * mat4(
        texelFetch(skinning_tex, clamp(i_joint[2] + skinning_offset, 0, MAX_BONES) * 4),
        texelFetch(skinning_tex, clamp(i_joint[2] + skinning_offset, 0, MAX_BONES) * 4 + 1),
        texelFetch(skinning_tex, clamp(i_joint[2] + skinning_offset, 0, MAX_BONES) * 4 + 2),
        texelFetch(skinning_tex, clamp(i_joint[2] + skinning_offset, 0, MAX_BONES) * 4 + 3)) +
        i_weight[3] * mat4(
        texelFetch(skinning_tex, clamp(i_joint[3] + skinning_offset, 0, MAX_BONES) * 4),
        texelFetch(skinning_tex, clamp(i_joint[3] + skinning_offset, 0, MAX_BONES) * 4 + 1),
        texelFetch(skinning_tex, clamp(i_joint[3] + skinning_offset, 0, MAX_BONES) * 4 + 2),
        texelFetch(skinning_tex, clamp(i_joint[3] + skinning_offset, 0, MAX_BONES) * 4 + 3));
#endif

    skinned_position = joint_matrix * idle_position;
    vec3 raw_world_offset = rotateVector(i_rotation,
        skinned_position.xyz * i_scale.xyz);
    vec4 raw_world_position = vec4(i_origin + raw_world_offset, 1.0);
    float relativity_fade = getRelativisticVisualFade(raw_world_position.xyz,
        i_velocity, i_scale.w);
    vec4 world_position = applyRelativisticContraction(raw_world_position,
        relativity_fade);
    world_position = applyRelativisticVisualPosition(world_position,
        i_velocity, relativity_fade);
    uv = i_uv;
    gl_Position = u_shadow_projection_view_matrices[layer] * world_position;
}
