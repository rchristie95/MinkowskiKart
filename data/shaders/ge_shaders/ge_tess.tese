// ge_tess.tese
//
// GE Vulkan tessellation evaluation shader.
// Interpolates per-vertex data across the tessellated patch, applies
// relativistic deformation, then projects to clip space via the camera UBO.
// Outputs the standard GE fragment varyings matching spm_layout.vert.

// Vulkan GE pipelines use VK_FRONT_FACE_CLOCKWISE. Keep tessellated patches in
// the same winding as the non-tessellated triangle-list path.
layout(triangles, fractional_odd_spacing, cw) in;

// ---- Inputs from ge_tess.tesc ----
layout(location = 0) in vec4  tc_vertex_color[];
layout(location = 1) in vec2  tc_uv[];
layout(location = 2) in vec2  tc_uv_two[];
layout(location = 3) flat in  int   tc_material_id[];
layout(location = 4) in float tc_hue_change[];
layout(location = 5) in vec3  tc_normal[];
layout(location = 6) in vec3  tc_tangent[];
layout(location = 7) in vec3  tc_bitangent[];
layout(location = 8) in vec4  tc_world_position[];
layout(location = 9) in vec3  tc_velocity[];
layout(location = 10) in float tc_disable_rel[];

// ---- Outputs (GE standard layout, matching spm_layout.vert) ----
layout(location = 0) out vec4  f_vertex_color;
layout(location = 1) out vec2  f_uv;
layout(location = 2) out vec2  f_uv_two;
layout(location = 3) flat out int   f_material_id;
layout(location = 4) out float f_hue_change;
layout(location = 5) out vec3  f_normal;
layout(location = 6) out vec3  f_tangent;
layout(location = 7) out vec3  f_bitangent;
layout(location = 8) out vec4  f_world_position;

#include "utils/camera.glsl"
#include "utils/relativity_bridge.glsl"
#include "../utils/relativity_visual.vert"

void main()
{
    // Barycentric interpolation
    vec3 bc = gl_TessCoord;

    vec4 world_pos =
        bc.x * tc_world_position[0] +
        bc.y * tc_world_position[1] +
        bc.z * tc_world_position[2];

    vec3 raw_normal =
        bc.x * tc_normal[0] +
        bc.y * tc_normal[1] +
        bc.z * tc_normal[2];

    vec3 raw_tangent =
        bc.x * tc_tangent[0] +
        bc.y * tc_tangent[1] +
        bc.z * tc_tangent[2];

    vec3 raw_bitangent =
        bc.x * tc_bitangent[0] +
        bc.y * tc_bitangent[1] +
        bc.z * tc_bitangent[2];

    f_vertex_color =
        bc.x * tc_vertex_color[0] +
        bc.y * tc_vertex_color[1] +
        bc.z * tc_vertex_color[2];

    f_uv =
        bc.x * tc_uv[0] +
        bc.y * tc_uv[1] +
        bc.z * tc_uv[2];

    f_uv_two =
        bc.x * tc_uv_two[0] +
        bc.y * tc_uv_two[1] +
        bc.z * tc_uv_two[2];

    f_hue_change =
        bc.x * tc_hue_change[0] +
        bc.y * tc_hue_change[1] +
        bc.z * tc_hue_change[2];

    // Velocity and disable flag are per-instance, same for all patch vertices
    vec3 i_velocity   = tc_velocity[0];
    float disable_rel = tc_disable_rel[0];

    // Apply relativistic deformation at the interpolated world position
    float visual_fade = getRelativisticVisualFade(world_pos.xyz, i_velocity,
                            disable_rel);
    vec4 deformed_pos = applyRelativisticVisualPosition(world_pos, i_velocity,
                            visual_fade);

    vec3 deformed_normal  = normalize(raw_normal);
    vec3 deformed_tangent = normalize(raw_tangent);

    f_world_position = deformed_pos;
    f_normal    = deformed_normal;
    f_tangent   = deformed_tangent;
    f_bitangent = normalize(raw_bitangent);
    f_material_id = tc_material_id[0];

    gl_Position = u_camera.m_projection_view_matrix * deformed_pos;
}
