// spm_skinning.metal
//
// Native-Metal port of data/shaders/ge_shaders/spm_skinning.vert:
// the 4-bone GPU skinning vertex function for the SPM (skinned mesh) path.
//
// This is a FAITHFUL, numerically-identical port of the GLSL. Every operation,
// clamp, index guard and branch order is preserved; only the plumbing changes:
//
//   * gl_InstanceIndex        -> [[instance_id]]
//   * gl_DrawIDARB            -> [[draw_id]]  (bindless path only)
//   * SSBO blocks (set=1)      -> `const device ...*` buffers at the shared
//                                 ge_metal_bindings.h slots
//   * global u_camera block    -> `constant CameraBuffer&` at GE_MTL_BUF_CAMERA
//                                 (the relativity helpers take it explicitly)
//   * per-vertex attributes    -> [[stage_in]] (spm_layout.vert locations 0..7)
//   * inversesqrt()            -> rsqrt() (inside the included relativity header)
//
// The vertex attribute formats match the S3DVertexSkinnedMesh layout the Metal
// driver's vertex descriptor already declares (pos float3 @0, normal
// Int1010102Normalized @12, color UChar4Normalized BGRA @16, uv Half2 @20;
// this shader additionally consumes uv_two @24, tangent @28, joints @32,
// weights @40; stride 48). Color is B,G,R,A in memory, so v_color.zyxw yields
// RGBA exactly like the GLSL.
//
// Metal NDC z is [0,1] whereas GL clip space is [-1,1]; the final clip position
// is remapped with clip.z = (clip.z + clip.w) * 0.5, matching the existing
// native 3D path in ge_metal_driver.mm.

#include <metal_stdlib>
using namespace metal;

#include "shared/ge_metal_bindings.h"     // GE_MTL_BUF_* / GECameraBuffer
#include "shared/relativity_bridge.h"     // CameraBuffer + u_relativity_* aliases
#include "common/get_vertex_color.h"      // getVertexColor(uint)
#include "common/get_world_location.h"    // rotateVector / getWorldPosition
#include "common/relativity_visual.h"     // getRelativisticVisualFade / apply...

// ===========================================================================
// SSBO payloads (GLSL: spm_data.glsl)
// ===========================================================================
// std140 ObjectBuffer::m_objects[] element. Field order + byte offsets match
// the GLSL ObjectData block and the C++ GE::ObjectData struct exactly (the same
// SSBO upload feeds both backends). packed_float3 is used for the vec3 members
// so the trailing scalar packs into the same 16-byte std140 slot:
//
//   m_translation (vec3 @0) + m_hue_change (float @12)          -> slot [0,16)
//   m_rotation    (vec4 @16)
//   m_scale       (vec3 @32) + m_custom_vertex_color (uint @44) -> slot [32,48)
//   m_skinning_offset (int @48) + m_material_id (int @52)
//                 + m_texture_trans (vec2 @56)                  -> slot [48,64)
//   m_velocity    (vec4 @64)
//   m_glow_color  (vec4 @80)   -> total stride 96
struct ObjectData
{
    packed_float3 m_translation;
    float         m_hue_change;
    float4        m_rotation;
    packed_float3 m_scale;
    uint          m_custom_vertex_color;
    int           m_skinning_offset;
    int           m_material_id;
    float2        m_texture_trans;
    // Per-instance velocity for relativistic aberration.
    // w = disable_relativity_visual (1.0 = disabled).
    float4        m_velocity;
    // Per-object glow colour (linear rgb, w = 1.0 if the node glows)
    float4        m_glow_color;
};

// ===========================================================================
// Vertex input (GLSL: spm_layout.vert, locations 0..7)
// ===========================================================================
struct VIn
{
    float3 v_position [[attribute(0)]];
    float4 v_normal   [[attribute(1)]];   // Int1010102Normalized -> xyzw
    float4 v_color    [[attribute(2)]];   // UChar4Normalized (B,G,R,A in memory)
    float2 v_uv       [[attribute(3)]];   // Half2
    float2 v_uv_two   [[attribute(4)]];   // Half2
    float4 v_tangent  [[attribute(5)]];   // Int1010102Normalized -> xyz + w sign
    int4   v_joint    [[attribute(6)]];   // ivec4 joint indices
    float4 v_weight   [[attribute(7)]];   // vec4 bone weights
};

// ===========================================================================
// Vertex output (GLSL: spm_layout.vert out locations 0..8)
// ===========================================================================
struct VOut
{
    float4       gl_Position [[position]];
    float4       f_vertex_color;
    float2       f_uv;
    float2       f_uv_two;
    int          f_material_id [[flat]];
    float        f_hue_change;
#ifdef PBR_ENABLED
    float3       f_normal;
    float3       f_tangent;
    float3       f_bitangent;
#endif
    float4       f_world_position;
};

// ===========================================================================
// spm_skinning vertex function (GLSL: spm_skinning.vert main())
// ===========================================================================
vertex VOut spm_skinning_vertex(
    VIn in [[stage_in]],
    uint gl_InstanceIndex [[instance_id]],
#ifdef BIND_MESH_TEXTURES_AT_ONCE
    uint gl_DrawIDARB [[draw_id]],
#endif
    constant CameraBuffer&        u_camera            [[buffer(GE_MTL_BUF_CAMERA)]],
    const device ObjectData*      u_object_buffer     [[buffer(GE_MTL_BUF_OBJECT)]],
    const device float4x4*        u_skinning_matrices [[buffer(GE_MTL_BUF_SKINNING)]]
#ifdef BIND_MESH_TEXTURES_AT_ONCE
    , const device int*           u_material_ids      [[buffer(GE_MTL_BUF_MATERIAL_ID)]]
#endif
)
{
    VOut o;

    const device ObjectData& obj = u_object_buffer[gl_InstanceIndex];

    int offset = obj.m_skinning_offset;
    float4x4 joint_matrix =
        in.v_weight[0] * u_skinning_matrices[max(in.v_joint[0] + offset, 0)] +
        in.v_weight[1] * u_skinning_matrices[max(in.v_joint[1] + offset, 0)] +
        in.v_weight[2] * u_skinning_matrices[max(in.v_joint[2] + offset, 0)] +
        in.v_weight[3] * u_skinning_matrices[max(in.v_joint[3] + offset, 0)];
    float4 v_skinning_position = joint_matrix * float4(in.v_position, 1.0);
    float4 raw_world_position = getWorldPosition(
        float3(obj.m_translation),
        obj.m_rotation,
        float3(obj.m_scale),
        v_skinning_position.xyz);

    // Apply relativistic light-travel-time + aberration correction.
    // m_velocity.xyz = object world-space velocity; .w = disable_relativity flag.
    float3 i_velocity   = obj.m_velocity.xyz;
    float disable_rel   = obj.m_velocity.w;
    float visual_fade   = getRelativisticVisualFade(u_camera, raw_world_position.xyz,
                              i_velocity, disable_rel);
    float4 v_world_position = applyRelativisticVisualPosition(u_camera, raw_world_position,
                                 i_velocity, visual_fade);

    o.f_world_position = v_world_position;
    float4 clip = u_camera.m_projection_view_matrix * v_world_position;
    clip.z = (clip.z + clip.w) * 0.5;   // GL [-1,1] -> Metal [0,1]
    o.gl_Position = clip;
    o.f_vertex_color = in.v_color.zyxw * getVertexColor(obj.m_custom_vertex_color);
    o.f_uv = in.v_uv + obj.m_texture_trans;
    o.f_uv_two = in.v_uv_two;
    o.f_material_id = obj.m_material_id;
#ifdef BIND_MESH_TEXTURES_AT_ONCE
    if (o.f_material_id < 0)
        o.f_material_id = u_material_ids[gl_DrawIDARB];
#endif
    o.f_hue_change = obj.m_hue_change;
#ifdef PBR_ENABLED
    float4 skinned_normal = joint_matrix * in.v_normal;
    float4 skinned_tangent = joint_matrix * float4(in.v_tangent.xyz, 0.0);
    float3 world_normal = normalize(rotateVector(
        obj.m_rotation,
        skinned_normal.xyz));
    float3 world_tangent = rotateVector(
        obj.m_rotation,
        skinned_tangent.xyz);
    o.f_bitangent = cross(world_normal, world_tangent) * in.v_tangent.w;
    o.f_tangent = world_tangent;
    o.f_normal = world_normal;
#endif
    return o;
}
