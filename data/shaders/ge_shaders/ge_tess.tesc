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

// Desktop GPUs retain the original high-quality limits.  Tile-based mobile
// GPUs need tessellation for correct relativistic warping of large triangles,
// but factor 64 can amplify one triangle into thousands.  Use a coarser world
// target and a strict factor cap there; the projected-edge term below still
// drives large visible or strongly aberrated edges to that cap.
#ifdef TILED_GPU
const float TARGET_EDGE_LENGTH_NEAR = 0.35;
const float TARGET_EDGE_LENGTH_SCREEN = 64.0;
const float MAX_TESS_LEVEL = 8.0;
#else
const float TARGET_EDGE_LENGTH_NEAR = 0.1;
const float MAX_TESS_LEVEL = 64.0;
#endif
const float FULL_TESSELLATION_RADIUS = 10.0;

float getDistanceToEdge(vec3 point, vec3 pA, vec3 pB)
{
    vec3 edge = pB - pA;
    float edge_l2 = dot(edge, edge);
    if (edge_l2 <= 1e-8)
        return length(point - pA);
    float t = clamp(dot(point - pA, edge) / edge_l2, 0.0, 1.0);
    return length(point - (pA + edge * t));
}

// Aberration magnifies image space by ~1/(1 + beta.d) (largest behind the
// observer, where the denominator shrinks), so geometry there needs
// proportionally denser tessellation to keep the nonlinear warp smooth.
// Depends only on the edge endpoints, so shared edges still agree (no cracks).
float getWarpAmplification(vec3 pA, vec3 pB)
{
    vec3 beta = getRelativityBetaVector();
    if (dot(beta, beta) < 1e-6)
        return 1.0;
    vec3 mid = 0.5 * (pA + pB) - u_relativity_observer_pos.xyz;
    float len2 = dot(mid, mid);
    if (len2 < 1e-6)
        return 8.0;
    vec3 dir = mid * inversesqrt(len2);
    return clamp(1.0 / max(1.0 + dot(beta, dir), 0.125), 1.0, 8.0);
}

#ifdef TILED_GPU
// Approximate the unwarped projected edge length in pixels.  This depends only
// on the shared edge endpoints and camera, so adjacent patches select the same
// outer level.  Edges crossing the camera plane fall back to the conservative
// world-space metric instead of dividing by an unstable clip-space w.
float getProjectedEdgeLength(vec3 pA, vec3 pB)
{
    vec4 clip_a = u_camera.m_projection_view_matrix * vec4(pA, 1.0);
    vec4 clip_b = u_camera.m_projection_view_matrix * vec4(pB, 1.0);
    if (clip_a.w <= 1e-4 || clip_b.w <= 1e-4)
        return 0.0;

    vec2 ndc_a = clip_a.xy / clip_a.w;
    vec2 ndc_b = clip_b.xy / clip_b.w;
    return length((ndc_b - ndc_a) * 0.5 * u_camera.m_screensize);
}
#endif

float getTessLevel(vec3 pA, vec3 pB)
{
    float edge_l = length(pB - pA);
    float dist = getDistanceToEdge(u_relativity_bubble.xyz, pA, pB);
    // Unbounded adaptive falloff: rather than cutting tessellation off at a
    // fixed radius (which left huge far triangles rigidly warped, e.g. an
    // ocean plane), the target edge length grows linearly with distance, so
    // geometry keeps subdividing everywhere, just coarser further away.
    // The level depends only on the edge endpoints and the bubble centre, so
    // patches sharing an edge agree on its level and no cracks appear.
    float warp_amplification = getWarpAmplification(pA, pB);
    float target_edge = TARGET_EDGE_LENGTH_NEAR *
        max(1.0, dist / FULL_TESSELLATION_RADIUS) /
        warp_amplification;
    float level = edge_l / target_edge;
#ifdef TILED_GPU
    // Retain enough subdivisions for long on-screen edges even when they are
    // far from the bubble.  Aberration raises the requirement in the direction
    // where the visual transform magnifies nonlinear curvature.
    float screen_level = getProjectedEdgeLength(pA, pB) *
        warp_amplification / TARGET_EDGE_LENGTH_SCREEN;
    level = max(level, screen_level);
#endif
    return clamp(level, 1.0, MAX_TESS_LEVEL);
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
