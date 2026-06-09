#include "utils/camera.glsl"
#include "utils/relativity_bridge.glsl"
#include "utils/get_vertex_color.glsl"
#include "utils/spm_data.glsl"
#include "utils/spm_layout.vert"
#include "../utils/get_world_location.vert"
#include "../utils/relativity_visual.vert"

void main()
{
    vec4 raw_world_position = getWorldPosition(
        u_object_buffer.m_objects[gl_InstanceIndex].m_translation,
        u_object_buffer.m_objects[gl_InstanceIndex].m_rotation,
        u_object_buffer.m_objects[gl_InstanceIndex].m_scale, v_position);

    // Apply relativistic Lorentz contraction and light-travel-time correction.
    // m_velocity.xyz = object world-space velocity; .w = disable_relativity flag.
    vec3 i_velocity    = u_object_buffer.m_objects[gl_InstanceIndex].m_velocity.xyz;
    float disable_rel  = u_object_buffer.m_objects[gl_InstanceIndex].m_velocity.w;
    float visual_fade  = getRelativisticVisualFade(raw_world_position.xyz,
                             i_velocity, disable_rel);
    vec4 v_world_position = applyRelativisticContraction(raw_world_position,
                                visual_fade);
    v_world_position = applyRelativisticVisualPosition(v_world_position,
                           i_velocity, visual_fade);

    f_world_position = v_world_position;
    gl_Position = u_camera.m_projection_view_matrix * v_world_position;
    f_vertex_color = v_color.zyxw * getVertexColor(
        u_object_buffer.m_objects[gl_InstanceIndex].m_custom_vertex_color);
    f_uv = v_uv + u_object_buffer.m_objects[gl_InstanceIndex].m_texture_trans;
    f_uv_two = v_uv_two;
    f_material_id = u_object_buffer.m_objects[gl_InstanceIndex].m_material_id;
#ifdef BIND_MESH_TEXTURES_AT_ONCE
    if (f_material_id < 0)
        f_material_id = u_material_ids.m_material_id[gl_DrawIDARB];
#endif
    f_hue_change = u_object_buffer.m_objects[gl_InstanceIndex].m_hue_change;
#ifdef PBR_ENABLED
    vec3 world_normal = applyRelativisticNormalTransform(
        rotateVector(u_object_buffer.m_objects[gl_InstanceIndex].m_rotation, v_normal.xyz),
        visual_fade);
    vec3 world_tangent = applyRelativisticDisplacement(
        rotateVector(u_object_buffer.m_objects[gl_InstanceIndex].m_rotation, v_tangent.xyz),
        visual_fade);
    f_bitangent = cross(world_normal, world_tangent) * v_tangent.w;
    f_tangent = world_tangent;
    f_normal = world_normal;
#endif
}
