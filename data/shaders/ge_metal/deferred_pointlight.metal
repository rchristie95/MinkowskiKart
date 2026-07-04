// deferred_pointlight.metal — native-Metal port of the deferred point/spot
// light-volume pass.
//
// Ports:
//   * ge_shaders/deferred_pointlight.vert -> vertex ge_deferred_pointlight_vertex
//   * ge_shaders/deferred_pointlight.frag -> fragment ge_deferred_pointlight_fragment
//
// Purpose: for each on-screen point/spot light the CPU issues an instanced draw
// of a 4-vertex billboard quad (triangle-strip) pushed one light-radius toward
// the camera. The fragment stage reads the deferred G-buffer (color/normal/
// depth), reconstructs the view-space position, and adds this single light's
// PBR contribution via calculateLight(). The math is ported numerically
// identically to the GLSL; only the buffer/texture plumbing and the GL->Metal
// clip-z convention differ (see correctness notes).
//
// Binding contract
// ---------------------------------------------------------------------------
// Vertex stage:
//   buffer(GE_MTL_BUF_CAMERA)        u_camera        (GECameraBuffer)
//   buffer(GE_MTL_BUF_GLOBAL_LIGHT)  u_global_light  (GEGlobalLightBuffer)
//   buffer(GE_MTL_BUF_PUSH_CONSTANT) u_push_constants (PointLightPushConstants)
//   GLSL gl_InstanceIndex -> [[instance_id]]
//   GLSL gl_VertexIndex   -> [[vertex_id]]
//
// Fragment stage (per-pass G-buffer inputs, GLSL set = 0 bindings 0/1/2):
//   texture(0) u_color   (diffuse.rgb + pbr.z in .w)
//   texture(1) u_normal  (encoded normal.xy + pbr.xy in .zw)
//   texture(2) u_depth   (device depth)
//   buffer(GE_MTL_BUF_CAMERA)        u_camera        (GECameraBuffer)
//   buffer(GE_MTL_BUF_GLOBAL_LIGHT)  u_global_light  (GEGlobalLightBuffer)
//   GLSL gl_FragCoord -> [[position]]
//   GLSL texelFetch(tex, ivec2 px, 0) -> tex.read(uint2(px))

#include <metal_stdlib>
using namespace metal;

#include "shared/ge_metal_bindings.h"     // buffer/texture indices + GECameraBuffer / GEGlobalLightBuffer
#include "common/get_world_location.h"    // rotateVector / getWorldPosition
#include "common/unproject_position.h"    // getPosFromUVDepth
#include "common/decode_normal.h"         // DecodeNormal
#include "shared/pbr_light.h"             // calculateLight (+ PBRLight / accumulateLights)

// ---------------------------------------------------------------------------
// Push constants — MSL mirror of the GLSL `Constants` push_constant block:
//   vec4 m_billboard_rotation;  @0
//   int  m_fullscreen_light;    @16
// std430/push-constant layout: vec4 on a 16-byte boundary, trailing int at 16.
// Bound at GE_MTL_BUF_PUSH_CONSTANT (see shared/ge_metal_bindings.h).
// ---------------------------------------------------------------------------
struct PointLightPushConstants
{
    float4 m_billboard_rotation;
    int    m_fullscreen_light;
};

// ---------------------------------------------------------------------------
// Billboard quad corners — verbatim copy of the GLSL g_vertices[4]. Indexed by
// gl_VertexIndex / [[vertex_id]] (a 4-vertex triangle-strip draw).
// ---------------------------------------------------------------------------
constant float3 g_vertices[4] =
{
    float3( 1.0,  1.0, 0.0),
    float3( 1.0, -1.0, 0.0),
    float3(-1.0,  1.0, 0.0),
    float3(-1.0, -1.0, 0.0)
};

// ---------------------------------------------------------------------------
// Vertex output. GLSL emits `flat out int light_idx` -> [[flat]] here. The
// clip-space position carries gl_Position.
// ---------------------------------------------------------------------------
struct PointLightVOut
{
    float4 position   [[position]];
    int    light_idx  [[flat]];
};

// ---------------------------------------------------------------------------
// ge_deferred_pointlight_vertex — port of deferred_pointlight.vert main().
// ---------------------------------------------------------------------------
vertex PointLightVOut ge_deferred_pointlight_vertex(
    uint                              vid              [[vertex_id]],
    uint                              iid              [[instance_id]],
    constant GECameraBuffer&          u_camera         [[buffer(GE_MTL_BUF_CAMERA)]],
    constant GEGlobalLightBuffer&     u_global_light   [[buffer(GE_MTL_BUF_GLOBAL_LIGHT)]],
    constant PointLightPushConstants& u_push_constants [[buffer(GE_MTL_BUF_PUSH_CONSTANT)]])
{
    PointLightVOut o;

    // Get the light index from the instance ID
    int light_idx = int(iid) + u_push_constants.m_fullscreen_light;
    o.light_idx = light_idx;

    GELightData light = u_global_light.m_lights[light_idx];
    float4 pos_radius = light.m_position_radius;

    // Get camera position from inverse view matrix
    float3 camera_pos = u_camera.m_inverse_view_matrix[3].xyz;

    // Calculate vector from light to camera
    float3 light_to_camera = normalize(camera_pos - pos_radius.xyz);

    /* The lights which cover the whole screen have been rendered already
    // Calculate distance from light to camera
    float dist_to_camera = distance(camera_pos, pos_radius.xyz);

    // If camera is within light radius, move the billboard quad towards the
    // near plane
    if (dist_to_camera < pos_radius.w)
    {
        gl_Position = vec4(g_vertices[gl_VertexIndex], 1.0);
        return;
    }
    */

    // Move the billboard towards camera by one radius unit
    float4 world_pos = getWorldPosition(
        pos_radius.xyz + light_to_camera * pos_radius.w,
        u_push_constants.m_billboard_rotation,
        float3(pos_radius.w), g_vertices[vid]);

    float4 pv = u_camera.m_projection_view_matrix * world_pos;

    // GLSL clamps geometry behind the camera onto the near plane:
    //   if (pv.z < 0.0) gl_Position = vec4(pv.xy, 0.0, 1.0);
    //   else            gl_Position = pv;
    // Both branches produce GL-style clip space; the native Metal 3D path
    // remaps clip.z with (z + w) * 0.5 to Metal's [0,1] device-Z (same remap
    // spm.metal / spm_skinning.metal apply). Keep the GLSL clamp first, then
    // apply the shared remap. See correctness note (1).
    float4 clip = (pv.z < 0.0) ? float4(pv.xy, 0.0, 1.0) : pv;
    clip.z = (clip.z + clip.w) * 0.5;
    o.position = clip;

    return o;
}

// ---------------------------------------------------------------------------
// ge_deferred_pointlight_fragment — port of deferred_pointlight.frag main().
// ---------------------------------------------------------------------------
fragment float4 ge_deferred_pointlight_fragment(
    PointLightVOut                in             [[stage_in]],
    float4                        frag_coord     [[position]],
    texture2d<float>              u_color        [[texture(0)]],
    texture2d<float>              u_normal       [[texture(1)]],
    texture2d<float>              u_depth        [[texture(2)]],
    constant GECameraBuffer&      u_camera       [[buffer(GE_MTL_BUF_CAMERA)]],
    constant GEGlobalLightBuffer& u_global_light [[buffer(GE_MTL_BUF_GLOBAL_LIGHT)]])
{
    // GLSL: ivec2 px = ivec2(gl_FragCoord.xy);
    uint2 px = uint2(frag_coord.xy);

    float depth = u_depth.read(px, 0).x;
    if (depth == 1.0)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    float4 color_data  = u_color.read(px, 0);
    float4 normal_data = u_normal.read(px, 0);
    float3 diffuse_color = color_data.xyz;
    float3 pbr = float3(normal_data.zw, color_data.w);
    float3 world_normal = DecodeNormal(normal_data.xy);

    // GLSL feeds gl_FragCoord.xy (with the sampled depth as z) into
    // getPosFromUVDepth. Metal's [[position]].xy is window-space like
    // gl_FragCoord.xy. The inverse-projection matrix bakes the [0,1] device-Z,
    // so `depth` (raw device depth) is passed straight through as in the GLSL.
    float3 xpos = getPosFromUVDepth(float3(frag_coord.xy, depth),
        u_camera.m_viewport, u_camera.m_inverse_projection_matrix);

    float3 eyedir = -normalize(xpos);
    float3 normal = (u_camera.m_view_matrix * float4(world_normal, 0.0)).xyz;

    float3 light = calculateLight(in.light_idx, diffuse_color, normal, xpos,
        eyedir, 1.0 - pbr.x, pbr.y, u_camera, u_global_light);

    return float4(light, 1.0);
}
