// deferred_pbr.metal - native-Metal port of
// data/shaders/ge_shaders/deferred_pbr.frag
//
// Full-screen deferred lighting composition pass. Reads the G-buffer
// (color / normal / depth), reconstructs the view-space position, decodes the
// world-space normal, evaluates image-based lighting + sun (with cascaded sun
// shadows) + ambient occlusion via handlePBRDeferred(), and accumulates the
// on-screen point/spot lights that were NOT drawn as light volumes (the first
// `m_fullscreen_light_count` lights) via accumulateLights().
//
// Ported FAITHFULLY / numerically identically from the GLSL. The GLSL descriptor
// globals become explicit MSL parameters (matching the sibling
// deferred_pointlight.metal / solid.metal / alphatest.metal conventions):
//
//   Fragment G-buffer inputs (GLSL set = 0, bindings 0/1/2 - unfiltered):
//     texture(0)  u_color   (diffuse.rgb + pbr.z metallic-ish in .w)
//     texture(1)  u_normal  (encoded normal.xy + roughness/metallic in .zw)
//     texture(2)  u_depth   (device depth)
//       texelFetch(tex, ivec2(gl_FragCoord.xy), 0) -> tex.read(uint2(px))
//
//   Ambient occlusion (GLSL set = 1, binding = 7 - bilinear):
//     texture(GE_MTL_TEX_AO=7)  u_ao + sampler
//       texture(u_ao, ao_uv) -> u_ao.sample(u_ao_sampler, ao_uv)
//
//   Sun shadow atlas (GLSL set = 1, bindings 5/6):
//     sampler2DShadow u_sun_shadow_pcf -> depth2d<float> + compare sampler
//     sampler2D       u_sun_shadow_raw -> texture2d<float> + sampler
//       These have no slot in shared/ge_metal_bindings.h (which maps b5/b6 to
//       *buffer* slots for a different path), so, like skybox.metal defines its
//       own GE_MTL_TEX_SKYBOX, this file defines local texture/sampler slots 3/4
//       (free: 0/1/2 = G-buffer, 7 = AO, 8/9 = IBL). See correctness note (4).
//
//   IBL cubemaps (GLSL set = 2, bindings 0/1):
//     texturecube<float> u_diffuse  + sampler  (tex 8)
//     texturecube<float> u_specular + sampler  (tex 9)
//
//   u_camera        (set = 1, binding = 0) -> constant GECameraBuffer&
//   u_global_light  (set = 1, binding = 3) -> constant GEGlobalLightBuffer&
//   push_constant Constants{ int m_fullscreen_light_count; }
//                                          -> constant DeferredPBRPushConstants&
//   gl_FragCoord    -> the fragment [[position]] (window-space pixel coords).
//   dFdx / dFdy     -> dfdx / dfdy (screen-space derivatives, identical).
//
// The lighting math (handlePBRDeferred, getSunShadowFactor, accumulateLights,
// DecodeNormal, getPosFromUVDepth) lives in the shared/common headers and is
// reused verbatim; only the mechanical GLSL->MSL builtin substitutions differ.

#include <metal_stdlib>
using namespace metal;

#include "shared/ge_metal_bindings.h"   // buffer/texture indices + GECameraBuffer / GEGlobalLightBuffer + spec constants
#include "common/unproject_position.h"  // getPosFromUVDepth
#include "shared/handle_pbr.h"          // handlePBRDeferred (+ accumulateLights via pbr_light.h)
#include "common/sun_shadow.h"          // getSunShadowFactor
#include "common/decode_normal.h"       // DecodeNormal

// ---------------------------------------------------------------------------
// Sun-shadow atlas texture/sampler slots. shared/ge_metal_bindings.h maps the
// Vulkan set=1 b5/b6 shadow descriptors to *buffer* slots (a different path),
// and does not expose them as texture slots. This deferred pass consumes them
// as textures, so - following skybox.metal's local GE_MTL_TEX_SKYBOX pattern -
// they are assigned free per-pass texture/sampler indices here.
//   0/1/2 -> G-buffer (color/normal/depth)
//   3     -> sun shadow PCF (depth-compare)  <-- here
//   4     -> sun shadow raw depth            <-- here
//   7     -> AO
//   8/9   -> IBL diffuse / specular
#define GE_MTL_TEX_DPBR_SHADOW_PCF  3
#define GE_MTL_TEX_DPBR_SHADOW_RAW  4

// ---------------------------------------------------------------------------
// Push constants - MSL mirror of the GLSL `Constants` push_constant block:
//   int m_fullscreen_light_count;  @0
// Bound at GE_MTL_BUF_PUSH_CONSTANT (see shared/ge_metal_bindings.h).
// ---------------------------------------------------------------------------
struct DeferredPBRPushConstants
{
    int m_fullscreen_light_count;
};

// ---------------------------------------------------------------------------
// Fragment output.
//   location 0 o_color
// ---------------------------------------------------------------------------
struct FragOut
{
    float4 o_color [[color(0)]];
};

// ---------------------------------------------------------------------------
// deferred_pbr_main - port of deferred_pbr.frag main().
// ---------------------------------------------------------------------------
fragment FragOut deferred_pbr_main(
    float4                        frag_coord       [[position]],
    // G-buffer (per-pass, GLSL set = 0)
    texture2d<float>              u_color          [[texture(0)]],
    texture2d<float>              u_normal         [[texture(1)]],
    texture2d<float>              u_depth          [[texture(2)]],
    // Sun shadow atlas (GLSL set = 1 b5/b6) - local per-pass slots
    depth2d<float>                u_sun_shadow_pcf [[texture(GE_MTL_TEX_DPBR_SHADOW_PCF)]],
    sampler                       pcf_sampler      [[sampler(GE_MTL_TEX_DPBR_SHADOW_PCF)]],
    texture2d<float>              u_sun_shadow_raw [[texture(GE_MTL_TEX_DPBR_SHADOW_RAW)]],
    sampler                       raw_sampler      [[sampler(GE_MTL_TEX_DPBR_SHADOW_RAW)]],
    // Ambient occlusion (GLSL set = 1 b7)
    texture2d<float>              u_ao             [[texture(GE_MTL_TEX_AO)]],
    sampler                       u_ao_sampler     [[sampler(GE_MTL_TEX_AO)]],
    // IBL cubemaps (GLSL set = 2 b0/b1)
    texturecube<float>            u_diffuse          [[texture(GE_MTL_TEX_IBL_DIFFUSE)]],
    sampler                       u_diffuse_sampler  [[sampler(GE_MTL_TEX_IBL_DIFFUSE)]],
    texturecube<float>            u_specular         [[texture(GE_MTL_TEX_IBL_SPECULAR)]],
    sampler                       u_specular_sampler [[sampler(GE_MTL_TEX_IBL_SPECULAR)]],
    // Engine descriptors (GLSL set = 1)
    constant GECameraBuffer&      u_camera         [[buffer(GE_MTL_BUF_CAMERA)]],
    constant GEGlobalLightBuffer& u_global_light   [[buffer(GE_MTL_BUF_GLOBAL_LIGHT)]],
    constant DeferredPBRPushConstants& u_push_constants [[buffer(GE_MTL_BUF_PUSH_CONSTANT)]])
{
    FragOut out;

    // GLSL: ivec2 px = ivec2(gl_FragCoord.xy);
    uint2 px = uint2(frag_coord.xy);
    float depth = u_depth.read(px, 0).x;

    // GLSL: if (!u_has_skybox && depth == 1.0) discard;
    if (!u_has_skybox && depth == 1.0)
        discard_fragment();

    float4 color_data  = u_color.read(px, 0);
    float4 normal_data = u_normal.read(px, 0);
    float3 diffuse_color = color_data.xyz;
    float3 pbr = float3(normal_data.zw, color_data.w);
    float3 world_normal = DecodeNormal(normal_data.xy);

    // GLSL feeds gl_FragCoord.xy (with the sampled device depth as z) into
    // getPosFromUVDepth; [[position]].xy is window-space like gl_FragCoord.xy,
    // and the inverse-projection matrix bakes the [0,1] device-Z, so `depth`
    // (raw device depth) passes straight through as in the GLSL.
    float3 xpos = getPosFromUVDepth(float3(frag_coord.xy, depth),
        u_camera.m_viewport, u_camera.m_inverse_projection_matrix);
    float3 eyedir = -normalize(xpos);
    float3 normal = (u_camera.m_view_matrix * float4(world_normal, 0.0)).xyz;
    float3 world_pos = (u_camera.m_inverse_view_matrix *
        float4(xpos, 1.0)).xyz;

    // Geometric normal from screen-space derivatives for the shadow bias
    // (the G-buffer normal includes normal mapping, which would underbias
    // grazing surfaces, like the SP/OpenGL sunlightshadow geo_norm).
    float3 pos_dx = dfdx(world_pos);
    float3 pos_dy = dfdy(world_pos);
    float3 geo_normal = cross(pos_dy, pos_dx);
    float geo_len = length(geo_normal);
    geo_normal = geo_len > 1e-8 ? geo_normal / geo_len : world_normal;
    if (dot(geo_normal, world_normal) < 0.0)
        geo_normal = -geo_normal;

    float sun_shadow = getSunShadowFactor(world_pos, world_normal,
        geo_normal, xpos.z, frag_coord.xy,
        u_camera, u_global_light,
        u_sun_shadow_pcf, pcf_sampler, u_sun_shadow_raw, raw_sampler);

    float2 ao_uv = (frag_coord.xy - u_camera.m_viewport.xy) /
        u_camera.m_viewport.zw;
    float ao = clamp(u_ao.sample(u_ao_sampler, ao_uv).r, 0.0, 1.0);

    float3 hdr = handlePBRDeferred(diffuse_color, pbr, world_normal, eyedir,
        normal, 1.0 - pbr.x, sun_shadow, ao,
        u_camera, u_global_light,
        u_diffuse, u_diffuse_sampler, u_specular, u_specular_sampler);
    hdr += accumulateLights(u_push_constants.m_fullscreen_light_count,
        diffuse_color, normal, xpos, eyedir, 1.0 - pbr.x, pbr.y,
        u_camera, u_global_light);

    out.o_color = float4(hdr, 1.0);
    return out;
}
