// grass.metal — native-Metal port of ge_shaders/grass.vert (grass wind vertex).
//
// Ports:
//   * ge_shaders/grass.vert                 -> vertex ge_grass_vertex
//   * ge_shaders/utils/spm_layout.vert (in/out interface) -> VertexIn / VOut
//
// grass.vert is the SPM (static-mesh) vertex path plus a per-vertex sinusoidal
// wind sway: a wind offset derived from the push-constant wind direction is
// added to the per-instance translation, scaled by the vertex's red colour
// channel (per-blade stiffness mask). Everything else (per-instance ObjectData
// lookup, quaternion world transform, and the relativistic light-travel-time +
// aberration warp) matches ge_spm_vertex and is ported numerically identically.
//
// The world transform / 10-bit / vertex-colour unpack come from the shared
// ge_metal/common headers; the relativity math comes from relativity_visual.h
// via the relativity_bridge.h CameraBuffer.
//
// Binding contract (see shared/ge_metal_bindings.h):
//   buffer(0)  CameraBuffer         (GE_MTL_BUF_CAMERA)      -> u_camera
//   buffer(1)  ObjectBuffer (SSBO)  (GE_MTL_BUF_OBJECT)      -> u_object_buffer
//   buffer(4)  MaterialIDs (SSBO)   (GE_MTL_BUF_MATERIAL_ID) -> u_material_ids
//                                    (only when BIND_MESH_TEXTURES_AT_ONCE)
//   buffer(15) Constants (push)     (GE_MTL_BUF_PUSH_CONSTANT) -> u_push_constants
//
// GLSL gl_InstanceIndex -> Metal [[instance_id]]
// GLSL push_constant    -> constant struct at GE_MTL_BUF_PUSH_CONSTANT
// GLSL gl_DrawIDARB material-id fallback: see correctness note (2) below.

#include <metal_stdlib>
using namespace metal;

#include "shared/ge_metal_bindings.h"     // buffer/texture index constants
#include "shared/relativity_bridge.h"     // CameraBuffer + u_relativity_* aliases
#include "common/relativity_visual.h"     // relativistic retarded-pos + aberration
#include "common/get_world_location.h"    // rotateVector / getWorldPosition

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
// Push constants — mirror of the GLSL `layout(push_constant) Constants` block.
//   vec3 m_wind_direction;   (std430 push block; 12 bytes, +4 pad)
// ---------------------------------------------------------------------------
struct GrassConstants
{
    packed_float3 m_wind_direction;
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
// ge_grass_vertex — port of grass.vert main().
// ---------------------------------------------------------------------------
vertex VOut ge_grass_vertex(
    VertexIn                     in              [[stage_in]],
    uint                         iid             [[instance_id]],
    constant CameraBuffer&       u_camera        [[buffer(GE_MTL_BUF_CAMERA)]],
    const device ObjectData*     u_objects       [[buffer(GE_MTL_BUF_OBJECT)]],
    constant GrassConstants&     u_push_constants [[buffer(GE_MTL_BUF_PUSH_CONSTANT)]])
{
    VOut o;
    const device ObjectData& obj = u_objects[iid];

    // Per-vertex wind sway. GLSL:
    //   vec3 offset = sin(m_wind_direction * (v_position.y * 0.1));
    //   offset += vec3(cos(m_wind_direction) * 0.7);
    // sin()/cos() are componentwise; float3(cos(v) * 0.7) broadcasts nothing
    // extra here since cos() already returns a float3 (GLSL vec3(vec3) is a
    // no-op copy).
    float3 wind_direction = float3(u_push_constants.m_wind_direction);
    float3 offset = sin(wind_direction * (in.v_position.y * 0.1));
    offset += cos(wind_direction) * 0.7;

    // GLSL v_color.r reads the RED channel. The vertex colour is UChar4Normalized
    // BGRA in memory, so red lives in in.v_color.z (same swizzle basis the SPM
    // path uses: in.v_color.zyxw == RGBA). See correctness note (3).
    float vcolor_r = in.v_color.z;

    float4 raw_world_position = getWorldPosition(
        float3(obj.m_translation) + offset * vcolor_r,
        obj.m_rotation, obj.m_scale, in.v_position);

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

    // grass.vert hard-codes the vertex colour to opaque white.
    o.f_vertex_color = float4(1.0);
    o.f_uv           = in.v_uv;
    o.f_uv_two       = in.v_uv_two;
    o.f_material_id  = obj.m_material_id;
    // NOTE: GLSL BIND_MESH_TEXTURES_AT_ONCE material-id fallback (gl_DrawIDARB)
    // is omitted here; see correctness note (2).
    o.f_hue_change   = obj.m_hue_change;

    // grass.vert computes f_normal only under #ifdef PBR_ENABLED, but the
    // spm_layout.vert out interface always declares f_normal/f_tangent/
    // f_bitangent. Emit the PBR world normal to match ge_spm_vertex so the
    // fragment interface stays consistent; tangent/bitangent are left zero
    // because grass.vert never writes them (GLSL leaves them undefined).
    o.f_normal    = normalize(rotateVector(obj.m_rotation, in.v_normal.xyz));
    o.f_tangent   = float3(0.0);
    o.f_bitangent = float3(0.0);

    return o;
}
