// ge_tess.metal — native-Metal port of the GE relativistic tessellation path.
//
// Ports the three-stage Vulkan tessellation pipeline:
//   * ge_shaders/spm_tess.vert  -> the control-point vertex stage
//                                  (ge_tess_control_point_vertex): world-space
//                                  transform + per-instance data, no projection.
//   * ge_shaders/ge_tess.tesc   -> the per-patch tessellation-factor compute
//                                  kernel (ge_tess_factors_kernel): ports
//                                  getDistanceToEdge / getWarpAmplification /
//                                  getTessLevel and writes MTLTessellationFactorsHalf.
//   * ge_shaders/ge_tess.tese   -> the post-tessellation vertex function
//                                  ([[patch(triangle,3)]] ge_tess_post_vertex):
//                                  barycentric interpolation + relativistic warp
//                                  (applyRelativisticVisualPosition) + projection.
//
// A sketch of an object+mesh-shader variant is at the bottom of this file
// (ge_tess_object_shader / ge_tess_mesh_shader), gated behind
// GE_TESS_MESH_SHADER so it never affects the default classic-tessellation build.
//
// Metal tessellation contract used here (matches the Vulkan pipeline state):
//   * fractional_odd spacing        -> MTLTessellationPartitionModeFractionalOdd
//   * cw winding (VK_FRONT_FACE_CW) -> MTLWindingClockwise
//   * triangle patch, 3 control pts -> patchType = triangle, controlPointCount 3
// These are set CPU-side on the MTLRenderPipelineDescriptor; this file only
// provides the shader functions and must be paired with that state.
//
// The relativistic math (getWarpAmplification/getTessLevel numerics and the
// applyRelativisticVisualPosition warp) is a byte-for-byte port of the GLSL:
// same epsilons, same branch order, same constants. GLSL inversesqrt() -> MSL
// rsqrt() (identical semantics).
//
// Binding contract (see shared/ge_metal_bindings.h):
//   buffer(0)  CameraBuffer          (GE_MTL_BUF_CAMERA)      -> u_camera
//   buffer(1)  ObjectBuffer  (SSBO)  (GE_MTL_BUF_OBJECT)      -> u_object_buffer
//   buffer(4)  MaterialIDs   (SSBO)  (GE_MTL_BUF_MATERIAL_ID) -> u_material_ids
//                                     (only when BIND_MESH_TEXTURES_AT_ONCE)

#include <metal_stdlib>
#include <metal_tessellation>
using namespace metal;

#include "shared/ge_metal_bindings.h"     // buffer/texture index constants
#include "shared/relativity_bridge.h"     // CameraBuffer + u_relativity_* aliases
#include "common/relativity_visual.h"     // retarded-pos + aberration warp
#include "common/get_world_location.h"    // rotateVector / getWorldPosition
#include "common/get_vertex_color.h"      // getVertexColor (0xAARRGGBB unpack)

// ---------------------------------------------------------------------------
// ObjectData — MSL mirror of the std140 GLSL ObjectData SSBO (spm_data.glsl).
// Identical layout to spm.metal (96 bytes; packed_float3 + trailing scalar
// reproduces the std140 vec3+scalar 16-byte packing).
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
// Vertex input — full S3DVertexSkinnedMesh (matches spm.metal VertexIn).
//   pos     float3            @0
//   normal  Int1010102Norm    @12  -> vec4
//   color   UChar4Norm (BGRA) @16  -> vec4 (memory order B,G,R,A)
//   uv      half2             @20
//   uv_two  half2             @24
//   tangent Int1010102Norm    @28  -> vec4
//   joint   short4 / ivec4    @32
//   weight  half4 / vec4      @40
//   stride: 48
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
// ControlPoint — the per-vertex, world-space payload handed off from the
// control-point vertex stage to both the tessellation-factor kernel and the
// post-tessellation vertex function. This is the union of spm_tess.vert's
// "tcs_*" outputs (which ge_tess.tesc passes straight through as "tc_*").
//
// Laid out as a plain struct so the CPU allocates a control-point buffer of
// [patch_count * 3] of these; [[stage_in]] with a MTLVertexDescriptor on the
// post-tessellation function reads them back per patch via [[patch]] control
// points, and the factor kernel reads them directly from the device buffer.
//
// Kept to 16-byte-friendly members; velocity/disable are per-instance and
// identical across the three control points (GLSL reads tc_velocity[0]).
// ---------------------------------------------------------------------------
struct ControlPoint
{
    float4 world_position;   // tc_world_position  (xyz world, w = 1)
    float4 vertex_color;     // tc_vertex_color
    float2 uv;               // tc_uv
    float2 uv_two;           // tc_uv_two
    float3 normal;           // tc_normal   (world-space, un-normalized)
    float3 tangent;          // tc_tangent  (world-space, un-normalized)
    float3 bitangent;        // tc_bitangent
    float3 velocity;         // tc_velocity (per-instance)
    float  hue_change;       // tc_hue_change
    float  disable_rel;      // tc_disable_rel (per-instance)
    int    material_id;      // tc_material_id (flat)
};

// ---------------------------------------------------------------------------
// STAGE 1 — control-point vertex stage (port of spm_tess.vert main()).
//
// Runs as an ordinary Metal vertex function whose output is captured into the
// control-point buffer (post-tessellation pipeline: set
// MTLRenderPipelineDescriptor.vertexFunction = this and consume its output as
// control points). Unlike spm.vert this does NOT project — it emits world-space
// data for the tessellator, exactly like the Vulkan VS.
//
// gl_InstanceIndex -> [[instance_id]]. The BIND_MESH_TEXTURES_AT_ONCE draw-id
// material-id fallback (gl_DrawIDARB) is preserved as a guarded branch; Metal's
// per-draw base_instance/draw-id plumbing differs, see correctness note (2).
// ---------------------------------------------------------------------------
vertex ControlPoint ge_tess_control_point_vertex(
    VertexIn                     in         [[stage_in]],
    uint                         iid        [[instance_id]],
    const device ObjectData*     u_objects  [[buffer(GE_MTL_BUF_OBJECT)]]
#ifdef BIND_MESH_TEXTURES_AT_ONCE
    ,
    const device int*            u_material_ids [[buffer(GE_MTL_BUF_MATERIAL_ID)]],
    uint                         draw_id    [[base_instance]]
#endif
)
{
    ControlPoint cp;
    const device ObjectData& obj = u_objects[iid];

    float4 world_pos = getWorldPosition(
        obj.m_translation, obj.m_rotation, obj.m_scale, in.v_position);

    float3 world_normal  = rotateVector(obj.m_rotation, in.v_normal.xyz);
    float3 world_tangent = rotateVector(obj.m_rotation, in.v_tangent.xyz);

    cp.world_position = world_pos;
    cp.normal         = world_normal;
    cp.tangent        = world_tangent;
    cp.bitangent      = cross(world_normal, world_tangent) * in.v_tangent.w;

    // v_color is BGRA in memory -> .zyxw for RGBA (matches spm.vert v_color.zyxw).
    cp.vertex_color = in.v_color.zyxw * getVertexColor(obj.m_custom_vertex_color);
    cp.uv           = in.v_uv + obj.m_texture_trans;
    cp.uv_two       = in.v_uv_two;

    int material_id = obj.m_material_id;
#ifdef BIND_MESH_TEXTURES_AT_ONCE
    if (material_id < 0)
        material_id = u_material_ids[draw_id];
#endif
    cp.material_id  = material_id;

    cp.hue_change   = obj.m_hue_change;
    cp.velocity     = obj.m_velocity.xyz;
    cp.disable_rel  = obj.m_velocity.w;

    return cp;
}

// ===========================================================================
// STAGE 2 — tessellation-factor compute kernel (port of ge_tess.tesc).
//
// One thread per patch. Reads the three control points' world positions from
// the control-point buffer and writes an MTLTessellationFactorsHalf per patch.
// The level math is a numerically-identical port of getDistanceToEdge /
// getWarpAmplification / getTessLevel.
// ===========================================================================

// Match sp_tess.tesc constants (identical to ge_tess.tesc).
constant float GE_TESS_TARGET_EDGE_LENGTH_NEAR  = 0.1;
constant float GE_TESS_FULL_TESSELLATION_RADIUS = 10.0;
constant float GE_TESS_MAX_TESS_LEVEL           = 64.0;

// Port of getDistanceToEdge(point, pA, pB).
inline float ge_tess_getDistanceToEdge(float3 point, float3 pA, float3 pB)
{
    float3 edge = pB - pA;
    float edge_l2 = dot(edge, edge);
    if (edge_l2 <= 1e-8)
        return length(point - pA);
    float t = clamp(dot(point - pA, edge) / edge_l2, 0.0, 1.0);
    return length(point - (pA + edge * t));
}

// Port of getWarpAmplification(pA, pB). Needs beta + observer_pos from the
// camera UBO, so it takes u_camera explicitly (GLSL read the global block).
inline float ge_tess_getWarpAmplification(thread const CameraBuffer& u_camera,
                                          float3 pA, float3 pB)
{
    float3 beta = getRelativityBetaVector(u_camera);
    if (dot(beta, beta) < 1e-6)
        return 1.0;
    float3 mid = 0.5 * (pA + pB) - u_relativity_observer_pos.xyz;
    float len2 = dot(mid, mid);
    if (len2 < 1e-6)
        return 8.0;
    float3 dir = mid * rsqrt(len2);   // GLSL inversesqrt
    return clamp(1.0 / max(1.0 + dot(beta, dir), 0.125), 1.0, 8.0);
}

// Port of getTessLevel(pA, pB).
inline float ge_tess_getTessLevel(thread const CameraBuffer& u_camera,
                                 float3 pA, float3 pB)
{
    float edge_l = length(pB - pA);
    float dist = ge_tess_getDistanceToEdge(u_relativity_bubble.xyz, pA, pB);
    float target_edge = GE_TESS_TARGET_EDGE_LENGTH_NEAR *
        max(1.0, dist / GE_TESS_FULL_TESSELLATION_RADIUS) /
        ge_tess_getWarpAmplification(u_camera, pA, pB);
    return clamp(edge_l / target_edge, 1.0, GE_TESS_MAX_TESS_LEVEL);
}

// One threadgroup-thread per patch (dispatch a threadgroup multiple of 32;
// bound-check against the patch count passed as a small push-constant struct).
struct GETessKernelParams
{
    uint m_patch_count;
};

kernel void ge_tess_factors_kernel(
    uint                                     pid            [[thread_position_in_grid]],
    constant CameraBuffer&                   u_camera       [[buffer(GE_MTL_BUF_CAMERA)]],
    const device ControlPoint*               u_control_pts  [[buffer(GE_MTL_BUF_OBJECT)]],
    device MTLTriangleTessellationFactorsHalf* factors      [[buffer(GE_MTL_BUF_SKINNING)]],
    constant GETessKernelParams&             params         [[buffer(GE_MTL_BUF_PUSH_CONSTANT)]])
{
    if (pid >= params.m_patch_count)
        return;

    // Three control points per triangle patch, packed consecutively.
    uint base = pid * 3u;
    float3 p0 = u_control_pts[base + 0u].world_position.xyz;
    float3 p1 = u_control_pts[base + 1u].world_position.xyz;
    float3 p2 = u_control_pts[base + 2u].world_position.xyz;

    // Exactly mirrors ge_tess.tesc gl_InvocationID==0 block:
    //   outer[0] = getTessLevel(p1,p2)
    //   outer[1] = getTessLevel(p2,p0)
    //   outer[2] = getTessLevel(p0,p1)
    //   inner[0] = max(max(level0,level1),level2)
    float level0 = ge_tess_getTessLevel(u_camera, p1, p2);
    float level1 = ge_tess_getTessLevel(u_camera, p2, p0);
    float level2 = ge_tess_getTessLevel(u_camera, p0, p1);

    MTLTriangleTessellationFactorsHalf f;
    f.edgeTessellationFactor[0] = half(level0);
    f.edgeTessellationFactor[1] = half(level1);
    f.edgeTessellationFactor[2] = half(level2);
    f.insideTessellationFactor  = half(max(max(level0, level1), level2));

    factors[pid] = f;
}

// ===========================================================================
// STAGE 3 — post-tessellation vertex function (port of ge_tess.tese).
//
// [[patch(triangle, 3)]]: runs once per tessellator-generated vertex, given the
// barycentric coordinate ([[position_in_patch]]) and the patch's 3 control
// points. Interpolates the per-vertex data, applies the relativistic warp at the
// interpolated world position, then projects with the camera projection*view.
//
// The pipeline state must set:
//   tessellationPartitionMode  = MTLTessellationPartitionModeFractionalOdd
//   tessellationOutputWindingOrder = MTLWindingClockwise   (cw, VK_FRONT_FACE_CW)
//   tessellationFactorFormat   = MTLTessellationFactorFormatHalf
// to match `layout(triangles, fractional_odd_spacing, cw)`.
// ===========================================================================
struct FragmentIn
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

[[patch(triangle, 3)]]
vertex FragmentIn ge_tess_post_vertex(
    patch_control_point<ControlPoint> patch_in,
    float3                            bc          [[position_in_patch]],
    constant CameraBuffer&            u_camera    [[buffer(GE_MTL_BUF_CAMERA)]])
{
    FragmentIn o;

    // GLSL: vec3 bc = gl_TessCoord;  (barycentric, sums to 1)
    ControlPoint c0 = patch_in[0];
    ControlPoint c1 = patch_in[1];
    ControlPoint c2 = patch_in[2];

    float4 world_pos =
        bc.x * c0.world_position +
        bc.y * c1.world_position +
        bc.z * c2.world_position;

    float3 raw_normal =
        bc.x * c0.normal +
        bc.y * c1.normal +
        bc.z * c2.normal;

    float3 raw_tangent =
        bc.x * c0.tangent +
        bc.y * c1.tangent +
        bc.z * c2.tangent;

    float3 raw_bitangent =
        bc.x * c0.bitangent +
        bc.y * c1.bitangent +
        bc.z * c2.bitangent;

    o.f_vertex_color =
        bc.x * c0.vertex_color +
        bc.y * c1.vertex_color +
        bc.z * c2.vertex_color;

    o.f_uv =
        bc.x * c0.uv +
        bc.y * c1.uv +
        bc.z * c2.uv;

    o.f_uv_two =
        bc.x * c0.uv_two +
        bc.y * c1.uv_two +
        bc.z * c2.uv_two;

    o.f_hue_change =
        bc.x * c0.hue_change +
        bc.y * c1.hue_change +
        bc.z * c2.hue_change;

    // Velocity and disable flag are per-instance, same for all patch vertices.
    float3 i_velocity   = c0.velocity;
    float  disable_rel  = c0.disable_rel;

    // Apply relativistic deformation at the interpolated world position.
    float visual_fade = getRelativisticVisualFade(u_camera, world_pos.xyz,
                            i_velocity, disable_rel);
    float4 deformed_pos = applyRelativisticVisualPosition(u_camera, world_pos,
                            i_velocity, visual_fade);

    float3 deformed_normal  = normalize(raw_normal);
    float3 deformed_tangent = normalize(raw_tangent);

    o.f_world_position = deformed_pos;
    o.f_normal    = deformed_normal;
    o.f_tangent   = deformed_tangent;
    o.f_bitangent = normalize(raw_bitangent);
    o.f_material_id = c0.material_id;

    // GLSL: gl_Position = u_camera.m_projection_view_matrix * deformed_pos;
    // Metal NDC z is [0,1]; keep the same GL->Metal z remap the rest of the
    // native 3D path uses (clip.z = (z + w) * 0.5). See correctness note (1).
    float4 clip = u_camera.m_projection_view_matrix * deformed_pos;
    clip.z = (clip.z + clip.w) * 0.5;
    o.position = clip;

    return o;
}

// ===========================================================================
// SKETCH — object + mesh shader variant.
//
// An alternative to classic tessellation on Apple GPUs that support mesh
// shaders (Metal 3, A15/M2+). It replaces the fixed-function tessellator with:
//   * an OBJECT shader that computes the same relativistic tess level per patch
//     (reusing ge_tess_getTessLevel) and dispatches a proportional grid of mesh
//     threadgroups, and
//   * a MESH shader that generates the sub-triangles: it evaluates the same
//     barycentric interpolation + applyRelativisticVisualPosition + projection
//     as ge_tess_post_vertex, but emits primitives directly.
//
// This is a structural sketch (not a drop-in): mesh-shader vertex emission is
// bounded per threadgroup, so a production path must tile large tess levels
// across multiple mesh groups (the object payload carries the sub-tile origin).
// Gated behind GE_TESS_MESH_SHADER so it never compiles into the default build.
// ===========================================================================
#ifdef GE_TESS_MESH_SHADER

#include <metal_mesh>

// Per-patch payload passed object -> mesh. Carries the 3 control points plus the
// tessellation level the object stage computed (so the mesh stage subdivides
// consistently and shared edges still agree — same level math, no cracks).
struct GETessPayload
{
    ControlPoint cp[3];
    float        tess_level;   // continuous level; mesh stage rounds per its tiling
};

// Object shader: one threadgroup per patch. Computes the max edge level exactly
// like ge_tess_factors_kernel (so the object+mesh path is visually consistent
// with the classic path) and dispatches the mesh grid.
//
// GE_TESS_MESH_MAX_TG = how many mesh threadgroups one patch may fan out to; a
// production version derives this from tess_level and the per-group vertex cap.
constant uint GE_TESS_MESH_MAX_TG = 1u;

[[object, max_total_threadgroups_per_mesh_grid(GE_TESS_MESH_MAX_TG)]]
void ge_tess_object_shader(
    object_data GETessPayload&       payload      [[payload]],
    mesh_grid_properties             mgp,
    uint                             pid          [[threadgroup_position_in_grid]],
    constant CameraBuffer&           u_camera     [[buffer(GE_MTL_BUF_CAMERA)]],
    const device ControlPoint*       u_control_pts[[buffer(GE_MTL_BUF_OBJECT)]])
{
    uint base = pid * 3u;
    ControlPoint c0 = u_control_pts[base + 0u];
    ControlPoint c1 = u_control_pts[base + 1u];
    ControlPoint c2 = u_control_pts[base + 2u];

    float level0 = ge_tess_getTessLevel(u_camera, c1.world_position.xyz, c2.world_position.xyz);
    float level1 = ge_tess_getTessLevel(u_camera, c2.world_position.xyz, c0.world_position.xyz);
    float level2 = ge_tess_getTessLevel(u_camera, c0.world_position.xyz, c1.world_position.xyz);

    payload.cp[0] = c0;
    payload.cp[1] = c1;
    payload.cp[2] = c2;
    payload.tess_level = max(max(level0, level1), level2);

    // Sketch: one mesh threadgroup per patch. A real path tiles the sub-triangle
    // fan (level^2 tris) across ceil(level^2 / MAX_PRIMS) mesh groups.
    mgp.set_threadgroups_per_grid(uint3(GE_TESS_MESH_MAX_TG, 1u, 1u));
}

// Mesh output caps for the sketch (one small tile per group). A production path
// sizes these to the device's per-threadgroup mesh limits.
constant uint GE_TESS_MESH_MAX_VERTS = 64u;
constant uint GE_TESS_MESH_MAX_PRIMS = 64u;

using GETessMesh = mesh<FragmentIn, void, GE_TESS_MESH_MAX_VERTS,
                        GE_TESS_MESH_MAX_PRIMS, topology::triangle>;

// Evaluate one tessellated vertex: identical to ge_tess_post_vertex's body,
// factored so the mesh stage can call it per emitted vertex.
inline FragmentIn ge_tess_eval_vertex(thread const ControlPoint& c0,
                                      thread const ControlPoint& c1,
                                      thread const ControlPoint& c2,
                                      float3 bc,
                                      thread const CameraBuffer& u_camera)
{
    FragmentIn o;
    float4 world_pos  = bc.x * c0.world_position + bc.y * c1.world_position + bc.z * c2.world_position;
    float3 raw_normal = bc.x * c0.normal + bc.y * c1.normal + bc.z * c2.normal;
    float3 raw_tan    = bc.x * c0.tangent + bc.y * c1.tangent + bc.z * c2.tangent;
    float3 raw_bitan  = bc.x * c0.bitangent + bc.y * c1.bitangent + bc.z * c2.bitangent;

    o.f_vertex_color = bc.x * c0.vertex_color + bc.y * c1.vertex_color + bc.z * c2.vertex_color;
    o.f_uv           = bc.x * c0.uv + bc.y * c1.uv + bc.z * c2.uv;
    o.f_uv_two       = bc.x * c0.uv_two + bc.y * c1.uv_two + bc.z * c2.uv_two;
    o.f_hue_change   = bc.x * c0.hue_change + bc.y * c1.hue_change + bc.z * c2.hue_change;

    float3 i_velocity  = c0.velocity;
    float  disable_rel = c0.disable_rel;
    float  visual_fade = getRelativisticVisualFade(u_camera, world_pos.xyz, i_velocity, disable_rel);
    float4 deformed    = applyRelativisticVisualPosition(u_camera, world_pos, i_velocity, visual_fade);

    o.f_world_position = deformed;
    o.f_normal    = normalize(raw_normal);
    o.f_tangent   = normalize(raw_tan);
    o.f_bitangent = normalize(raw_bitan);
    o.f_material_id = c0.material_id;

    float4 clip = u_camera.m_projection_view_matrix * deformed;
    clip.z = (clip.z + clip.w) * 0.5;   // GL [-1,1] -> Metal [0,1]
    o.position = clip;
    return o;
}

// Mesh shader: subdivides the patch into an NxN barycentric lattice and emits
// the sub-triangles with cw winding (matching VK_FRONT_FACE_CLOCKWISE). This is
// a fixed low-resolution sketch (N derived from payload.tess_level, capped by
// the vertex/prim limits); a production path uses fractional-odd-consistent
// stitching so shared edges match the classic tessellator exactly.
[[mesh, max_total_threads_per_threadgroup(64)]]
void ge_tess_mesh_shader(
    GETessMesh                       output,
    object_data const GETessPayload& payload      [[payload]],
    uint                             tid          [[thread_position_in_threadgroup]],
    constant CameraBuffer&           u_camera     [[buffer(GE_MTL_BUF_CAMERA)]])
{
    ControlPoint c0 = payload.cp[0];
    ControlPoint c1 = payload.cp[1];
    ControlPoint c2 = payload.cp[2];

    // Integer segments per edge (odd, matching fractional_odd's odd base), capped
    // so vertex/prim counts stay within the sketch limits.
    uint seg = (uint)clamp(payload.tess_level, 1.0, 7.0);
    seg = (seg | 1u);   // force odd (fractional_odd spacing keeps an odd core)

    // Vertex lattice: rows i=0..seg, each row has (seg - i + 1) verts
    // (total ((seg+1)(seg+2))/2 vertices, bounded by GE_TESS_MESH_MAX_VERTS below).
    uint pcount = seg * seg;   // full-triangle subdivision -> seg^2 sub-triangles
    if (tid == 0u)
        output.set_primitive_count(min(pcount, GE_TESS_MESH_MAX_PRIMS));

    // Emit vertices (one thread group; small N so a single loop is fine for the
    // sketch — a real path parallelizes over tid).
    if (tid == 0u)
    {
        uint vi = 0u;
        for (uint i = 0u; i <= seg; ++i)
        {
            for (uint j = 0u; j <= seg - i; ++j)
            {
                if (vi >= GE_TESS_MESH_MAX_VERTS) break;
                float u = float(i) / float(seg);
                float v = float(j) / float(seg);
                float3 bc = float3(1.0 - u - v, u, v);
                output.set_vertex(vi, ge_tess_eval_vertex(c0, c1, c2, bc, u_camera));
                ++vi;
            }
        }

        // Emit indices row by row, cw winding to match the classic path.
        // Row r starts at index r*(seg+1) - r*(r-1)/2 (each row r has
        // (seg - r + 1) vertices; this is the running prefix sum). Computed
        // inline rather than via a lambda to stay portable across MSL versions.
        uint pi = 0u;
        for (uint i = 0u; i < seg; ++i)
        {
            uint r0 = i * (seg + 1u) - (i * (i - 1u)) / 2u;          // = 0 when i==0
            uint r1 = (i + 1u) * (seg + 1u) - ((i + 1u) * i) / 2u;
            uint cols = seg - i;
            for (uint j = 0u; j < cols; ++j)
            {
                if (pi >= GE_TESS_MESH_MAX_PRIMS) break;
                // upward triangle (index values fit in uchar for the sketch's
                // small vertex count; set_index takes uchar).
                output.set_index(pi * 3u + 0u, (uchar)(r0 + j));
                output.set_index(pi * 3u + 1u, (uchar)(r1 + j));
                output.set_index(pi * 3u + 2u, (uchar)(r0 + j + 1u));
                ++pi;
                // downward triangle (skip on last column of the row)
                if (j + 1u < cols && pi < GE_TESS_MESH_MAX_PRIMS)
                {
                    output.set_index(pi * 3u + 0u, (uchar)(r0 + j + 1u));
                    output.set_index(pi * 3u + 1u, (uchar)(r1 + j));
                    output.set_index(pi * 3u + 2u, (uchar)(r1 + j + 1u));
                    ++pi;
                }
            }
        }
    }
}

#endif // GE_TESS_MESH_SHADER
