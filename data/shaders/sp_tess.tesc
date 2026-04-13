layout(vertices = 3) out;

in vec3 v_tangent[];
in vec3 v_bitangent[];
in vec3 v_normal[];
in vec2 v_uv[];
in vec2 v_uv_two[];
in vec4 v_color[];
in vec4 v_world_position[];
in vec3 v_world_normal[];
in float v_hue_change[];
in vec3 v_velocity[];

out vec3 tc_tangent[];
out vec3 tc_bitangent[];
out vec3 tc_normal[];
out vec2 tc_uv[];
out vec2 tc_uv_two[];
out vec4 tc_color[];
out vec4 tc_world_position[];
out vec3 tc_world_normal[];
out float tc_hue_change[];
out vec3 tc_velocity[];

#stk_include "utils/relativity_visual.vert"

float getTessLevel(vec3 pA, vec3 pB) {
    float edge_l = length(pB - pA);
    vec3 mid = (pA + pB) * 0.5;
    float dist = length(mid - u_relativity_observer_pos.xyz);
    float beta = length(u_relativity_beta.xyz);
    
    if (edge_l < 2.0) return 1.0;
    
    float level = clamp(edge_l / 5.0, 1.0, 8.0);
    if (dist < 50.0) {
        level *= mix(2.0, 1.0, clamp(dist / 50.0, 0.0, 1.0));
    }
    if (beta > 0.5) {
        level *= (1.0 + beta);
    }
    return clamp(level, 1.0, 16.0);
}

void main()
{
    tc_tangent[gl_InvocationID] = v_tangent[gl_InvocationID];
    tc_bitangent[gl_InvocationID] = v_bitangent[gl_InvocationID];
    tc_normal[gl_InvocationID] = v_normal[gl_InvocationID];
    tc_uv[gl_InvocationID] = v_uv[gl_InvocationID];
    tc_uv_two[gl_InvocationID] = v_uv_two[gl_InvocationID];
    tc_color[gl_InvocationID] = v_color[gl_InvocationID];
    tc_world_position[gl_InvocationID] = v_world_position[gl_InvocationID];
    tc_world_normal[gl_InvocationID] = v_world_normal[gl_InvocationID];
    tc_hue_change[gl_InvocationID] = v_hue_change[gl_InvocationID];
    tc_velocity[gl_InvocationID] = v_velocity[gl_InvocationID];

    if (gl_InvocationID == 0)
    {
        vec3 p0 = v_world_position[0].xyz;
        vec3 p1 = v_world_position[1].xyz;
        vec3 p2 = v_world_position[2].xyz;

        gl_TessLevelOuter[0] = getTessLevel(p1, p2);
        gl_TessLevelOuter[1] = getTessLevel(p2, p0);
        gl_TessLevelOuter[2] = getTessLevel(p0, p1);
        gl_TessLevelInner[0] = (gl_TessLevelOuter[0] + gl_TessLevelOuter[1] + gl_TessLevelOuter[2]) / 3.0;
    }
}
