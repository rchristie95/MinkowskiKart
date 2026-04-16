uniform vec3 wind_direction;

layout(location = 0) in vec3 i_position;

#if defined(Converts_10bit_Vector)
layout(location = 1) in vec4 i_normal_orig;
#else
layout(location = 1) in vec4 i_normal;
#endif

layout(location = 2) in vec4 i_color;
layout(location = 3) in vec2 i_uv;
layout(location = 8) in vec3 i_origin;
layout(location = 9) in vec4 i_rotation;
layout(location = 10) in vec4 i_scale;
layout(location = 12) in ivec2 i_misc_data;
layout(location = 13) in vec3 i_velocity;

#stk_include "utils/get_world_location.vert"
#stk_include "utils/relativity_visual.vert"

out vec3 normal;
out vec2 uv;
out float hue_change;

void main()
{

#if defined(Converts_10bit_Vector)
    vec4 i_normal = convert10BitVector(i_normal_orig);
#endif

    vec3 test = sin(wind_direction * (i_position.y * 0.1));
    test += cos(wind_direction) * 0.7;

    vec3 raw_world_offset = rotateVector(i_rotation, i_position * i_scale.xyz) + test * i_color.r;
    vec4 raw_world_position = vec4(i_origin + raw_world_offset, 1.0);
    float relativity_fade = getRelativisticVisualFade(raw_world_position.xyz,
        i_velocity, i_scale.w);
    vec4 world_position = applyRelativisticContraction(raw_world_position,
        relativity_fade);
    vec3 world_normal = applyRelativisticNormalTransform(
        rotateVector(i_rotation, i_normal.xyz), relativity_fade);
    world_position = applyRelativisticVisualPosition(world_position,
        i_velocity, relativity_fade);

    normal = world_normal;
    uv = i_uv;
    hue_change = float(i_misc_data.y) * 0.01;
    gl_Position = u_projection_view_matrix * world_position;
}
