#ifdef BIND_MESH_TEXTURES_AT_ONCE
#extension GL_ARB_shader_draw_parameters : enable
#endif
struct ObjectData
{
    vec3 m_translation;
    float m_hue_change;
    vec4 m_rotation;
    vec3 m_scale;
    uint m_custom_vertex_color;
    int m_skinning_offset;
    int m_material_id;
    vec2 m_texture_trans;
    // Per-instance velocity for relativistic aberration.
    // w = disable_relativity_visual (1.0 = disabled).
    vec4 m_velocity;
    // Per-object glow colour (linear rgb, w = 1.0 if the node glows)
    vec4 m_glow_color;
};

layout(std140, set = 1, binding = 1) readonly buffer ObjectBuffer
{
    ObjectData m_objects[];
} u_object_buffer;

layout(std140, set = 1, binding = 2) readonly buffer SkinningMatrices
{
    mat4 m_mat[];
} u_skinning_matrices;

#ifdef BIND_MESH_TEXTURES_AT_ONCE
layout(std430, set = 1, binding = 4) readonly buffer MaterialIDs
{
    int m_material_id[];
} u_material_ids;
#endif
