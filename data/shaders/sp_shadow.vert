uniform int layer;

layout(location = 0) in vec3 i_position;
layout(location = 3) in vec2 i_uv;
layout(location = 8) in vec3 i_origin;
layout(location = 9) in vec4 i_rotation;
layout(location = 10) in vec4 i_scale;
layout(location = 13) in vec3 i_velocity;

#stk_include "utils/get_world_location.vert"
#stk_include "utils/relativity_visual.vert"

out vec2 uv;

void main()
{
    vec3 raw_world_offset = rotateVector(i_rotation, i_position * i_scale.xyz);
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
