#include "utils/camera.glsl"
#include "utils/relativity_bridge.glsl"
#include "utils/spm_data.glsl"
#include "utils/spm_layout.vert"
#include "../utils/get_world_location.vert"
#include "../utils/relativity_visual.vert"

layout(push_constant) uniform Constants
{
    vec3 m_wind_direction;
} u_push_constants;

void main()
{
    vec3 offset = sin(u_push_constants.m_wind_direction * (v_position.y * 0.1));
    offset += vec3(cos(u_push_constants.m_wind_direction) * 0.7);

    vec4 raw_world_position = getWorldPosition(
        u_object_buffer.m_objects[gl_InstanceIndex].m_translation + offset *
        v_color.r,
        u_object_buffer.m_objects[gl_InstanceIndex].m_rotation,
        u_object_buffer.m_objects[gl_InstanceIndex].m_scale, v_position);

    // Apply relativistic Lorentz contraction and light-travel-time correction.
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
    f_vertex_color = vec4(1.0);
    f_uv = v_uv;
    f_uv_two = v_uv_two;
    f_material_id = u_object_buffer.m_objects[gl_InstanceIndex].m_material_id;
#ifdef BIND_MESH_TEXTURES_AT_ONCE
    if (f_material_id < 0)
        f_material_id = u_material_ids.m_material_id[gl_DrawIDARB];
#endif
    f_hue_change = u_object_buffer.m_objects[gl_InstanceIndex].m_hue_change;
#ifdef PBR_ENABLED
    f_normal = applyRelativisticNormalTransform(
        rotateVector(u_object_buffer.m_objects[gl_InstanceIndex].m_rotation, v_normal.xyz),
        visual_fade);
#endif
}
