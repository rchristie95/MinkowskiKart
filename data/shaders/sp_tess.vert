layout(location = 0) in vec3 i_position;

#if defined(Converts_10bit_Vector)
layout(location = 1) in vec4 i_normal_orig;
#else
layout(location = 1) in vec4 i_normal;
#endif

layout(location = 2) in vec4 i_color;
layout(location = 3) in vec2 i_uv;
layout(location = 4) in vec2 i_uv_two;

#if defined(Converts_10bit_Vector)
layout(location = 5) in vec4 i_tangent_orig;
#else
layout(location = 5) in vec4 i_tangent;
#endif

layout(location = 8) in vec3 i_origin;
layout(location = 9) in vec4 i_rotation;
layout(location = 10) in vec4 i_scale;
layout(location = 11) in vec2 i_texture_trans;
layout(location = 12) in ivec2 i_misc_data;
layout(location = 13) in vec3 i_velocity;

#stk_include "utils/get_world_location.vert"

out vec3 v_tangent;
out vec3 v_bitangent;
out vec3 v_normal;
out vec2 v_uv;
out vec2 v_uv_two;
out vec4 v_color;
out vec4 v_world_position;
out vec3 v_world_normal;
out float v_hue_change;
out vec3 v_velocity;

void main()
{
#if defined(Converts_10bit_Vector)
    vec4 i_normal = convert10BitVector(i_normal_orig);
    vec4 i_tangent = convert10BitVector(i_tangent_orig);
#endif

    vec3 raw_world_offset = rotateVector(i_rotation, i_position * i_scale.xyz);
    v_world_position = vec4(i_origin + raw_world_offset, 1.0);
    v_world_normal = rotateVector(i_rotation, i_normal.xyz);
    v_tangent = rotateVector(i_rotation, i_tangent.xyz);
    v_bitangent = cross(v_world_normal, v_tangent) * i_tangent.w;

    v_uv = vec2(i_uv.x + (i_texture_trans.x * i_normal.w),
        i_uv.y + (i_texture_trans.y * i_normal.w));
    v_uv_two = i_uv_two;

    v_color = i_color.zyxw;
    v_hue_change = float(i_misc_data.y) * 0.01;
    v_velocity = i_velocity;
    
    // We don't set gl_Position here as it will be set by the Tessellation Evaluation shader
    gl_Position = v_world_position;
}
