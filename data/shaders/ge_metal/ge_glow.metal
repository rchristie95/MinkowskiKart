// ge_glow.metal
//
// Native-Metal port of the per-object glow-silhouette pass:
//   * ge_shaders/ge_glow.vert          -> vertex ge_glow_vertex
//   * ge_shaders/ge_glow_skinning.vert -> vertex ge_glow_skinning_vertex
//   * ge_shaders/ge_glow.frag          -> fragment ge_glow_fragment
//
// Purpose: write flat per-object glow silhouettes into the glow attachment of
// the displace-mask pass; displace_color blurs them and composites the outline
// (mirrors the SP/OpenGL renderGlow + glow.frag chain).
//
// The vertex stage reproduces spm.vert / spm_skinning.vert world position
// (including the relativistic retarded-position + aberration warp) so the
// silhouettes line up with the depth buffer, then forwards only the per-object
// glow colour (flat).
//
// This is a FAITHFUL, numerically-identical port of the GLSL. Every operation,
// clamp, index guard and branch order is preserved; only the plumbing changes:
//
//   * gl_InstanceIndex        -> [[instance_id]]
//   * SSBO blocks (set=1)      -> `const device ...*` buffers at the shared
//                                 ge_metal_bindings.h slots
//   * global u_camera block    -> `constant CameraBuffer&` at GE_MTL_BUF_CAMERA
//                                 (the relativity helpers take it explicitly)
//   * per-vertex attributes    -> [[stage_in]]; only v_position (loc 0) and, for
//                                 the skinned variant, v_joint (loc 6) and
//                                 v_weight (loc 7) are consumed, matching the
//                                 GLSL `in` declarations.
//   * inversesqrt()            -> rsqrt() (inside the included relativity header)
//   * discard                  -> discard_fragment()
//
// Binding contract (see shared/ge_metal_bindings.h):
//   buffer(0)  CameraBuffer         (GE_MTL_BUF_CAMERA)   -> u_camera
//   buffer(1)  ObjectBuffer (SSBO)  (GE_MTL_BUF_OBJECT)   -> u_object_buffer
//   buffer(2)  SkinningMatrices     (GE_MTL_BUF_SKINNING) -> u_skinning_matrices
//                                    (skinned variant only)
//
// Metal NDC z is [0,1] whereas GL clip space is [-1,1]; the final clip position
// is remapped with clip.z = (clip.z + clip.w) * 0.5, matching the existing
// native 3D path in ge_metal_driver.mm. The GLSL depth pull
// `gl_Position.z -= 1e-4 * gl_Position.w` is applied to the GL clip-space z
// BEFORE that remap so the result is identical to GL-then-remap. See note (1).

#include <metal_stdlib>
using namespace metal;

#include "shared/ge_metal_bindings.h"     // GE_MTL_BUF_* index constants
#include "shared/relativity_bridge.h"     // CameraBuffer + u_relativity_* aliases
#include "common/get_world_location.h"    // rotateVector / getWorldPosition
#include "common/relativity_visual.h"     // getRelativisticVisualFade / apply...

// ===========================================================================
// SSBO payload (GLSL: ge_shaders/utils/spm_data.glsl -> ObjectData)
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
    // Per-object glow colour (linear rgb, w = 1.0 if the node glows).
    float4        m_glow_color;
};

// ===========================================================================
// Vertex input
// ===========================================================================
// The glow vertex GLSL declares only v_position (location 0). The skinned
// variant additionally declares v_joint (location 6) and v_weight (location 7).
// We keep the same [[attribute(n)]] slots as the full S3DVertexSkinnedMesh
// vertex descriptor so this shader can share it; unused attributes are simply
// not read.
struct VIn
{
    float3 v_position [[attribute(0)]];
};

struct VInSkinning
{
    float3 v_position [[attribute(0)]];
    int4   v_joint    [[attribute(6)]];   // ivec4 joint indices
    float4 v_weight   [[attribute(7)]];   // vec4 bone weights
};

// ===========================================================================
// Vertex output (GLSL: `flat out vec4 f_glow_color` at location 0)
// ===========================================================================
struct VOut
{
    float4 gl_Position  [[position]];
    float4 f_glow_color [[flat]];
};

// ===========================================================================
// ge_glow_vertex (GLSL: ge_glow.vert main())
// ===========================================================================
vertex VOut ge_glow_vertex(
    VIn                      in              [[stage_in]],
    uint                     gl_InstanceIndex [[instance_id]],
    constant CameraBuffer&   u_camera        [[buffer(GE_MTL_BUF_CAMERA)]],
    const device ObjectData* u_object_buffer [[buffer(GE_MTL_BUF_OBJECT)]])
{
    VOut o;

    const device ObjectData& obj = u_object_buffer[gl_InstanceIndex];

    float4 raw_world_position = getWorldPosition(
        float3(obj.m_translation),
        obj.m_rotation,
        float3(obj.m_scale),
        in.v_position);

    float3 i_velocity  = obj.m_velocity.xyz;
    float  disable_rel = obj.m_velocity.w;
    float  visual_fade = getRelativisticVisualFade(u_camera, raw_world_position.xyz,
                             i_velocity, disable_rel);
    float4 v_world_position = applyRelativisticVisualPosition(u_camera,
                                 raw_world_position, i_velocity, visual_fade);

    float4 clip = u_camera.m_projection_view_matrix * v_world_position;
    // GLSL: gl_Position.z -= 1e-4 * gl_Position.w;  (slight pull toward camera)
    // Applied in GL clip space, then remapped GL [-1,1] -> Metal [0,1]. See (1).
    clip.z -= 1e-4 * clip.w;
    clip.z = (clip.z + clip.w) * 0.5;
    o.gl_Position = clip;

    o.f_glow_color = obj.m_glow_color;
    return o;
}

// ===========================================================================
// ge_glow_skinning_vertex (GLSL: ge_glow_skinning.vert main())
// ===========================================================================
vertex VOut ge_glow_skinning_vertex(
    VInSkinning              in                  [[stage_in]],
    uint                     gl_InstanceIndex    [[instance_id]],
    constant CameraBuffer&   u_camera            [[buffer(GE_MTL_BUF_CAMERA)]],
    const device ObjectData* u_object_buffer     [[buffer(GE_MTL_BUF_OBJECT)]],
    const device float4x4*   u_skinning_matrices [[buffer(GE_MTL_BUF_SKINNING)]])
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

    float3 i_velocity  = obj.m_velocity.xyz;
    float  disable_rel = obj.m_velocity.w;
    float  visual_fade = getRelativisticVisualFade(u_camera, raw_world_position.xyz,
                             i_velocity, disable_rel);
    float4 v_world_position = applyRelativisticVisualPosition(u_camera,
                                 raw_world_position, i_velocity, visual_fade);

    float4 clip = u_camera.m_projection_view_matrix * v_world_position;
    clip.z -= 1e-4 * clip.w;             // GL clip-space depth pull
    clip.z = (clip.z + clip.w) * 0.5;    // GL [-1,1] -> Metal [0,1]
    o.gl_Position = clip;

    o.f_glow_color = obj.m_glow_color;
    return o;
}

// ===========================================================================
// Fragment output (GLSL: ge_glow.frag -> o_color0/1/2 at locations 0,1,2)
// ===========================================================================
// The glow value is written to every colour attachment; the pipeline's
// per-attachment write masks make sure only the glow attachment is stored (see
// ge_glow.frag header comment). The glow attachment index depends on whether
// the SSR attachment exists, so all three are emitted here.
struct FOut
{
    float4 o_color0 [[color(0)]];
    float4 o_color1 [[color(1)]];
    float4 o_color2 [[color(2)]];
};

// ===========================================================================
// ge_glow_fragment (GLSL: ge_glow.frag main())
// ===========================================================================
fragment FOut ge_glow_fragment(VOut in [[stage_in]])
{
    if (in.f_glow_color.w < 0.5)
        discard_fragment();

    FOut o;
    float4 glow = float4(in.f_glow_color.rgb, 1.0);
    o.o_color0 = glow;
    o.o_color1 = glow;
    o.o_color2 = glow;
    return o;
}
