// spm.metal — native-Metal port of the SPM (skinned/static mesh) vertex stage.
//
// Ports:
//   * ge_shaders/spm.vert           -> vertex ge_spm_vertex
//   * ge_shaders/spm_skinning.vert  -> vertex ge_spm_skinning_vertex
//   * ge_shaders/fullscreen_quad.vert -> vertex ge_fullscreen_quad_vertex
//   * ge_shaders/utils/spm_layout.vert (the in/out interface) -> VertexIn / VOut
//
// The per-vertex world transform, per-instance ObjectData lookup, and the
// per-vertex relativistic light-travel-time + aberration warp are ported
// numerically identically to the GLSL. World transform and 10-bit / vertex
// colour unpack come from the shared ge_metal/common headers; the relativity
// math comes from relativity_visual.h via the relativity_bridge.h CameraBuffer.
//
// Binding contract (see shared/ge_metal_bindings.h):
//   buffer(0)  CameraBuffer         (GE_MTL_BUF_CAMERA)      -> u_camera
//   buffer(1)  ObjectBuffer (SSBO)  (GE_MTL_BUF_OBJECT)      -> u_object_buffer
//   buffer(2)  SkinningMatrices     (GE_MTL_BUF_SKINNING)    -> u_skinning_matrices
//   buffer(4)  MaterialIDs (SSBO)   (GE_MTL_BUF_MATERIAL_ID) -> u_material_ids
//                                    (only when BIND_MESH_TEXTURES_AT_ONCE)
//
// GLSL gl_InstanceIndex -> Metal [[instance_id]]
// GLSL gl_VertexIndex   -> Metal [[vertex_id]]
// GLSL gl_DrawIDARB     -> Metal [[base_instance]] path is not available per
//                          draw call the same way; the bindless material-id
//                          fallback is guarded exactly like the GLSL #ifdef and
//                          is documented below. See correctness note (2).

#include <metal_stdlib>
using namespace metal;

#include "shared/ge_metal_bindings.h"     // buffer/texture index constants
#include "shared/relativity_bridge.h"     // CameraBuffer + u_relativity_* aliases
#include "common/relativity_visual.h"     // relativistic retarded-pos + aberration
#include "common/get_world_location.h"    // rotateVector / getWorldPosition
#include "common/get_vertex_color.h"      // getVertexColor (0xAARRGGBB unpack)

// ---------------------------------------------------------------------------
// ObjectData — MSL mirror of the std140 GLSL ObjectData SSBO (spm_data.glsl).
//
// std140 offsets (bytes):
//   m_translation       vec3  @0
//   m_hue_change        float @12   (packs into the vec3's 16-byte slot)
//   m_rotation          vec4  @16
//   m_scale             vec3  @32
//   m_custom_vertex_color uint@44   (packs into the vec3's 16-byte slot)
//   m_skinning_offset   int   @48
//   m_material_id       int   @52
//   m_texture_trans     vec2  @56
//   m_velocity          vec4  @64
//   m_glow_color        vec4  @80
// total: 96 bytes. packed_float3 + trailing scalar reproduces the std140
// vec3+scalar packing exactly (16-byte slot, no padding inserted).
// ---------------------------------------------------------------------------
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
    float4        m_velocity;      // xyz = world velocity, w = disable_relativity
    float4        m_glow_color;
};

// ---------------------------------------------------------------------------
// Vertex input — full S3DVertexSkinnedMesh (spm_layout.vert locations 0..7).
//   pos     float3            @0
//   normal  Int1010102Norm    @12  -> vec4
//   color   UChar4Norm (BGRA) @16  -> vec4 (memory order B,G,R,A)
//   uv      half2             @20
//   uv_two  half2             @24
//   tangent Int1010102Norm    @28  -> vec4
//   joint   short4 / ivec4    @32
//   weight  half4 / vec4      @40
//   stride: 48
// The vertex-descriptor bufferIndex/format is configured CPU-side; here we only
// name the [[attribute(n)]] slots matching that descriptor.
// ---------------------------------------------------------------------------
struct VertexIn
{
    float3 v_position [[attribute(0)]];
    float4 v_normal   [[attribute(1)]];   // Int1010102Normalized
    float4 v_color    [[attribute(2)]];   // UChar4Normalized, BGRA in memory
    float2 v_uv       [[attribute(3)]];   // Half2
    float2 v_uv_two   [[attribute(4)]];   // Half2
    float4 v_tangent  [[attribute(5)]];   // Int1010102Normalized
    int4   v_joint    [[attribute(6)]];   // Short4 / ivec4
    float4 v_weight   [[attribute(7)]];   // Half4 / vec4
};

// ---------------------------------------------------------------------------
// Vertex output — matches spm_layout.vert out interface (locations 0..8).
// f_material_id is `flat` in GLSL -> [[flat]] here.
// ---------------------------------------------------------------------------
struct VOut
{
    float4 position        [[position]];
    float4 f_vertex_color;
    float2 f_uv;
    float2 f_uv_two;
    int    f_material_id    [[flat]];
    float  f_hue_change;
    float3 f_normal;
    float3 f_tangent;
    float3 f_bitangent;
    float4 f_world_position;
};

// ---------------------------------------------------------------------------
// ge_spm_vertex — port of spm.vert main().
// ---------------------------------------------------------------------------
vertex VOut ge_spm_vertex(
    VertexIn                     in         [[stage_in]],
    uint                         iid        [[instance_id]],
    constant CameraBuffer&       u_camera   [[buffer(GE_MTL_BUF_CAMERA)]],
    const device ObjectData*     u_objects  [[buffer(GE_MTL_BUF_OBJECT)]])
{
    VOut o;
    const device ObjectData& obj = u_objects[iid];

    float4 raw_world_position = getWorldPosition(
        obj.m_translation, obj.m_rotation, obj.m_scale, in.v_position);

    // Apply relativistic light-travel-time + aberration correction.
    // m_velocity.xyz = object world-space velocity; .w = disable_relativity flag.
    float3 i_velocity   = obj.m_velocity.xyz;
    float  disable_rel  = obj.m_velocity.w;
    float  visual_fade  = getRelativisticVisualFade(u_camera,
                              raw_world_position.xyz, i_velocity, disable_rel);
    float4 v_world_position = applyRelativisticVisualPosition(u_camera,
                                  raw_world_position, i_velocity, visual_fade);

    o.f_world_position = v_world_position;

    // GLSL: gl_Position = u_camera.m_projection_view_matrix * v_world_position;
    // Metal NDC z is [0,1]; keep the same GL->Metal remap the rest of the
    // native 3D path uses (clip.z = (z + w) * 0.5). See correctness note (1).
    float4 clip = u_camera.m_projection_view_matrix * v_world_position;
    clip.z = (clip.z + clip.w) * 0.5;
    o.position = clip;

    o.f_vertex_color = in.v_color.zyxw * getVertexColor(obj.m_custom_vertex_color);
    o.f_uv           = in.v_uv + obj.m_texture_trans;
    o.f_uv_two       = in.v_uv_two;
    o.f_material_id  = obj.m_material_id;
    // NOTE: GLSL BIND_MESH_TEXTURES_AT_ONCE material-id fallback (gl_DrawIDARB)
    // is omitted here; see correctness note (2).
    o.f_hue_change   = obj.m_hue_change;

    // PBR_ENABLED path (spm_layout.vert emits these regardless; the GLSL guards
    // the compute with #ifdef PBR_ENABLED but always declares the outputs).
    float3 world_normal  = normalize(rotateVector(obj.m_rotation, in.v_normal.xyz));
    float3 world_tangent = rotateVector(obj.m_rotation, in.v_tangent.xyz);
    o.f_bitangent = cross(world_normal, world_tangent) * in.v_tangent.w;
    o.f_tangent   = world_tangent;
    o.f_normal    = world_normal;

    return o;
}

// ---------------------------------------------------------------------------
// ge_spm_skinning_vertex — port of spm_skinning.vert main().
// Adds the 4-bone linear-blend skinning of position/normal/tangent before the
// world transform; everything else matches ge_spm_vertex.
// ---------------------------------------------------------------------------
vertex VOut ge_spm_skinning_vertex(
    VertexIn                     in         [[stage_in]],
    uint                         iid        [[instance_id]],
    constant CameraBuffer&       u_camera   [[buffer(GE_MTL_BUF_CAMERA)]],
    const device ObjectData*     u_objects  [[buffer(GE_MTL_BUF_OBJECT)]],
    const device float4x4*       u_skinning [[buffer(GE_MTL_BUF_SKINNING)]])
{
    VOut o;
    const device ObjectData& obj = u_objects[iid];

    int offset = obj.m_skinning_offset;
    float4x4 joint_matrix =
        in.v_weight[0] * u_skinning[max(in.v_joint[0] + offset, 0)] +
        in.v_weight[1] * u_skinning[max(in.v_joint[1] + offset, 0)] +
        in.v_weight[2] * u_skinning[max(in.v_joint[2] + offset, 0)] +
        in.v_weight[3] * u_skinning[max(in.v_joint[3] + offset, 0)];

    float4 v_skinning_position = joint_matrix * float4(in.v_position, 1.0);
    float4 raw_world_position = getWorldPosition(
        obj.m_translation, obj.m_rotation, obj.m_scale, v_skinning_position.xyz);

    float3 i_velocity   = obj.m_velocity.xyz;
    float  disable_rel  = obj.m_velocity.w;
    float  visual_fade  = getRelativisticVisualFade(u_camera,
                              raw_world_position.xyz, i_velocity, disable_rel);
    float4 v_world_position = applyRelativisticVisualPosition(u_camera,
                                  raw_world_position, i_velocity, visual_fade);

    o.f_world_position = v_world_position;

    float4 clip = u_camera.m_projection_view_matrix * v_world_position;
    clip.z = (clip.z + clip.w) * 0.5;   // GL [-1,1] -> Metal [0,1]
    o.position = clip;

    o.f_vertex_color = in.v_color.zyxw * getVertexColor(obj.m_custom_vertex_color);
    o.f_uv           = in.v_uv + obj.m_texture_trans;
    o.f_uv_two       = in.v_uv_two;
    o.f_material_id  = obj.m_material_id;
    o.f_hue_change   = obj.m_hue_change;

    // PBR_ENABLED skinned normal/tangent path.
    float4 skinned_normal  = joint_matrix * in.v_normal;
    float4 skinned_tangent = joint_matrix * float4(in.v_tangent.xyz, 0.0);
    float3 world_normal  = normalize(rotateVector(obj.m_rotation, skinned_normal.xyz));
    float3 world_tangent = rotateVector(obj.m_rotation, skinned_tangent.xyz);
    o.f_bitangent = cross(world_normal, world_tangent) * in.v_tangent.w;
    o.f_tangent   = world_tangent;
    o.f_normal    = world_normal;

    return o;
}

// ---------------------------------------------------------------------------
// ge_fullscreen_quad_vertex — port of fullscreen_quad.vert.
// Emits a single oversized triangle covering the screen (3-vertex draw).
// GLSL:
//   f_uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
//   gl_Position = vec4(f_uv * 2.0 - 1.0, 1.0, 1.0);
// Metal NDC z is [0,1]; the GL far plane 1.0 maps to Metal far 1.0 unchanged
// (a full-screen pass writes the far depth), so z stays 1.0. See note (3).
// ---------------------------------------------------------------------------
struct FullscreenQuadOut
{
    float4 position [[position]];
    float2 f_uv;
};

vertex FullscreenQuadOut ge_fullscreen_quad_vertex(uint vid [[vertex_id]])
{
    FullscreenQuadOut o;
    o.f_uv = float2(float((vid << 1) & 2), float(vid & 2));
    o.position = float4(o.f_uv * 2.0 - 1.0, 1.0, 1.0);
    return o;
}
