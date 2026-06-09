// spm_tess.vert
//
// GE Vulkan tessellation vertex shader.  Unlike spm.vert this shader does NOT
// project to clip space; it passes world-space data through to the tessellation
// control shader (ge_tess.tesc) which distributes it, and the tessellation
// evaluation shader (ge_tess.tese) which projects at the final tessellated
// position.
//
// Output variable names are prefixed with "v_" to match ge_tess.tesc inputs.

#include "utils/camera.glsl"
#include "utils/relativity_bridge.glsl"
#include "utils/get_vertex_color.glsl"
#include "utils/spm_data.glsl"
#include "../utils/get_world_location.vert"

// ---- Vertex inputs (GE standard layout) ----
layout(location = 0) in vec3 v_position;
layout(location = 1) in vec4 v_normal;
layout(location = 2) in vec4 v_color;
layout(location = 3) in vec2 v_uv;
layout(location = 4) in vec2 v_uv_two;
layout(location = 5) in vec4 v_tangent;

// ---- TCS-bound outputs (world-space) ----
layout(location = 0) out vec4 tcs_vertex_color;
layout(location = 1) out vec2 tcs_uv;
layout(location = 2) out vec2 tcs_uv_two;
layout(location = 3) flat out int tcs_material_id;
layout(location = 4) out float tcs_hue_change;
layout(location = 5) out vec3 tcs_normal;
layout(location = 6) out vec3 tcs_tangent;
layout(location = 7) out vec3 tcs_bitangent;
layout(location = 8) out vec4 tcs_world_position;
layout(location = 9) out vec3 tcs_velocity;
layout(location = 10) out float tcs_disable_rel;

void main()
{
    vec4 world_pos = getWorldPosition(
        u_object_buffer.m_objects[gl_InstanceIndex].m_translation,
        u_object_buffer.m_objects[gl_InstanceIndex].m_rotation,
        u_object_buffer.m_objects[gl_InstanceIndex].m_scale, v_position);

    vec3 world_normal  = rotateVector(
        u_object_buffer.m_objects[gl_InstanceIndex].m_rotation, v_normal.xyz);
    vec3 world_tangent = rotateVector(
        u_object_buffer.m_objects[gl_InstanceIndex].m_rotation, v_tangent.xyz);

    tcs_world_position = world_pos;
    tcs_normal         = world_normal;
    tcs_tangent        = world_tangent;
    tcs_bitangent      = cross(world_normal, world_tangent) * v_tangent.w;

    tcs_vertex_color = v_color.zyxw * getVertexColor(
        u_object_buffer.m_objects[gl_InstanceIndex].m_custom_vertex_color);
    tcs_uv       = v_uv + u_object_buffer.m_objects[gl_InstanceIndex].m_texture_trans;
    tcs_uv_two   = v_uv_two;
    tcs_material_id  = u_object_buffer.m_objects[gl_InstanceIndex].m_material_id;
#ifdef BIND_MESH_TEXTURES_AT_ONCE
    if (tcs_material_id < 0)
        tcs_material_id = u_material_ids.m_material_id[gl_DrawIDARB];
#endif
    tcs_hue_change   = u_object_buffer.m_objects[gl_InstanceIndex].m_hue_change;
    tcs_velocity     = u_object_buffer.m_objects[gl_InstanceIndex].m_velocity.xyz;
    tcs_disable_rel  = u_object_buffer.m_objects[gl_InstanceIndex].m_velocity.w;

    // Pass world position as gl_Position for Apple TCS compatibility
    // (Apple OpenGL driver culls patches whose VS does not write gl_Position).
    gl_Position = world_pos;
}
