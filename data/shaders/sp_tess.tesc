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
in float v_disable_relativity_visual[];

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
out float tc_disable_relativity_visual[];

#stk_include "utils/relativity_visual.vert"

const float TARGET_EDGE_LENGTH_NEAR = 0.25;
const float FULL_TESSELLATION_RADIUS = 10.0;
const float FALLOFF_END_RADIUS = 50.0;
const float MAX_TESS_LEVEL = 64.0;

float getDistanceToEdge(vec3 point, vec3 pA, vec3 pB)
{
    vec3 edge = pB - pA;
    float edge_l2 = dot(edge, edge);
    if (edge_l2 <= 1e-8)
    {
        return length(point - pA);
    }

    float t = clamp(dot(point - pA, edge) / edge_l2, 0.0, 1.0);
    return length(point - (pA + edge * t));
}

float getTessLevel(vec3 pA, vec3 pB)
{
    float edge_l = length(pB - pA);
    float near_level = ceil(edge_l / TARGET_EDGE_LENGTH_NEAR);
    float dist = getDistanceToEdge(u_relativity_bubble.xyz, pA, pB);

    if (dist <= FULL_TESSELLATION_RADIUS)
    {
        return clamp(near_level, 1.0, MAX_TESS_LEVEL);
    }
    if (dist >= FALLOFF_END_RADIUS)
    {
        return 1.0;
    }

    float falloff = smoothstep(
        FULL_TESSELLATION_RADIUS, FALLOFF_END_RADIUS, dist);
    float level = mix(near_level, 1.0, falloff);
    return clamp(level, 1.0, MAX_TESS_LEVEL);
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
    tc_disable_relativity_visual[gl_InvocationID] =
        v_disable_relativity_visual[gl_InvocationID];

    if (gl_InvocationID == 0)
    {
        vec3 p0 = v_world_position[0].xyz;
        vec3 p1 = v_world_position[1].xyz;
        vec3 p2 = v_world_position[2].xyz;

        float level0 = getTessLevel(p1, p2);
        float level1 = getTessLevel(p2, p0);
        float level2 = getTessLevel(p0, p1);

        gl_TessLevelOuter[0] = level0;
        gl_TessLevelOuter[1] = level1;
        gl_TessLevelOuter[2] = level2;
        gl_TessLevelInner[0] = max(max(level0, level1), level2);
    }
}
