// ge_tess.tesc
//
// GE Vulkan tessellation control shader.
// Computes per-edge tessellation levels based on proximity to the warp-bubble
// centre (u_relativity_bubble.xyz), matching the SP/OpenGL sp_tess.tesc logic.

layout(vertices = 3) out;

// ---- Inputs from spm_tess.vert (per-vertex, locations match VS outputs) ----
layout(location = 0)  in vec4  tcs_vertex_color[];
layout(location = 1)  in vec2  tcs_uv[];
layout(location = 2)  in vec2  tcs_uv_two[];
layout(location = 3)  flat in  int  tcs_material_id[];
layout(location = 4)  in float tcs_hue_change[];
layout(location = 5)  in vec3  tcs_normal[];
layout(location = 6)  in vec3  tcs_tangent[];
layout(location = 7)  in vec3  tcs_bitangent[];
layout(location = 8)  in vec4  tcs_world_position[];
layout(location = 9)  in vec3  tcs_velocity[];
layout(location = 10) in float tcs_disable_rel[];

// ---- Outputs to ge_tess.tese (per-vertex, per-patch) ----
layout(location = 0) out vec4  tc_vertex_color[];
layout(location = 1) out vec2  tc_uv[];
layout(location = 2) out vec2  tc_uv_two[];
layout(location = 3) flat out int   tc_material_id[];
layout(location = 4) out float tc_hue_change[];
layout(location = 5) out vec3  tc_normal[];
layout(location = 6) out vec3  tc_tangent[];
layout(location = 7) out vec3  tc_bitangent[];
layout(location = 8) out vec4  tc_world_position[];
layout(location = 9) out vec3  tc_velocity[];
layout(location = 10) out float tc_disable_rel[];

#include "utils/camera.glsl"
#include "utils/relativity_bridge.glsl"
#include "../utils/relativity_visual.vert"

// Tessellation quality constants (match sp_tess.tesc)
const float TARGET_EDGE_LENGTH_NEAR = 0.25;
const float FULL_TESSELLATION_RADIUS = 10.0;
const float FALLOFF_END_RADIUS = 50.0;
const float MAX_TESS_LEVEL = 64.0;

float getDistanceToEdge(vec3 point, vec3 pA, vec3 pB)
{
    vec3 edge = pB - pA;
    float edge_l2 = dot(edge, edge);
    if (edge_l2 <= 1e-8)
        return length(point - pA);
    float t = clamp(dot(point - pA, edge) / edge_l2, 0.0, 1.0);
    return length(point - (pA + edge * t));
}

float getTessLevel(vec3 pA, vec3 pB)
{
    float edge_l = length(pB - pA);
    float near_level = ceil(edge_l / TARGET_EDGE_LENGTH_NEAR);
    float dist = getDistanceToEdge(u_relativity_bubble.xyz, pA, pB);

    if (dist <= FULL_TESSELLATION_RADIUS)
        return clamp(near_level, 1.0, MAX_TESS_LEVEL);
    if (dist >= FALLOFF_END_RADIUS)
        return 1.0;

    float falloff = smoothstep(FULL_TESSELLATION_RADIUS, FALLOFF_END_RADIUS, dist);
    return clamp(mix(near_level, 1.0, falloff), 1.0, MAX_TESS_LEVEL);
}

void main()
{
    // Pass all per-vertex data straight through
    tc_vertex_color[gl_InvocationID]  = tcs_vertex_color[gl_InvocationID];
    tc_uv[gl_InvocationID]            = tcs_uv[gl_InvocationID];
    tc_uv_two[gl_InvocationID]        = tcs_uv_two[gl_InvocationID];
    tc_material_id[gl_InvocationID]   = tcs_material_id[gl_InvocationID];
    tc_hue_change[gl_InvocationID]    = tcs_hue_change[gl_InvocationID];
    tc_normal[gl_InvocationID]        = tcs_normal[gl_InvocationID];
    tc_tangent[gl_InvocationID]       = tcs_tangent[gl_InvocationID];
    tc_bitangent[gl_InvocationID]     = tcs_bitangent[gl_InvocationID];
    tc_world_position[gl_InvocationID] = tcs_world_position[gl_InvocationID];
    tc_velocity[gl_InvocationID]      = tcs_velocity[gl_InvocationID];
    tc_disable_rel[gl_InvocationID]   = tcs_disable_rel[gl_InvocationID];

    // Apple/MoltenVK: TCS must write gl_out[].gl_Position or driver culls patches.
    gl_out[gl_InvocationID].gl_Position = tcs_world_position[gl_InvocationID];

    if (gl_InvocationID == 0)
    {
        vec3 p0 = tcs_world_position[0].xyz;
        vec3 p1 = tcs_world_position[1].xyz;
        vec3 p2 = tcs_world_position[2].xyz;

        float level0 = getTessLevel(p1, p2);
        float level1 = getTessLevel(p2, p0);
        float level2 = getTessLevel(p0, p1);

        gl_TessLevelOuter[0] = level0;
        gl_TessLevelOuter[1] = level1;
        gl_TessLevelOuter[2] = level2;
        gl_TessLevelInner[0] = max(max(level0, level1), level2);
    }
}
