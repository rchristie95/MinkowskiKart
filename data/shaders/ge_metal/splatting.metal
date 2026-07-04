// splatting.metal - native-Metal port of
//   data/shaders/ge_shaders/splatting.frag
//
// Terrain splat-blend fragment shader. A per-vertex splat map (texture layer 1,
// sampled with the second UV set) weights four detail layers (layers 2..5),
// each sampled at two resolutions and cross-faded by a camera-distance
// "mitigation" factor to break up repetitive tiling in the distance.
//
// Ported FAITHFULLY / numerically identically from the GLSL. The GLSL body is
// wholly inside `#ifdef PBR_ENABLED`; that guard is preserved here. When PBR is
// disabled the GLSL main() is empty and writes no attachments, so the Metal
// entry point returns a zeroed FragOut in that case (a Metal fragment must
// return a value; leaving the color undefined is not permitted).
//
// GLSL -> Metal mapping:
//   * Stage inputs (spm_layout.vert outputs) become a [[stage_in]] struct.
//       location 1 f_uv, 2 f_uv_two, 3 flat f_material_id, 5 f_normal,
//       8 f_world_position.  (locations 0/4/6/7 are unused by this frag.)
//   * Two color outputs (o_color @0, o_normal @1) -> FragOut with [[color(0)]]
//     / [[color(1)]].  In the forward (non-deferred) branch only o_color is
//     written; o_normal is left zeroed, matching the GLSL which never touches it
//     on that path.
//   * set=1 binding=0 u_camera        -> constant GECameraBuffer&      (buffer 0)
//   * set=1 binding=3 u_global_light  -> constant GEGlobalLightBuffer& (buffer 3)
//   * set=2 IBL cubes + shadow/AO textures are threaded through to handlePBR,
//     exactly as its ported signature requires.
//   * Mesh textures follow sample_mesh_texture.h: the bindless argument-buffer
//     table under BIND_MESH_TEXTURES_AT_ONCE, else the <=16-sampler fallback.
//   * u_deferred is [[function_constant(2)]] (from ge_metal_bindings.h).
//
// See shared/ge_metal_bindings.h for every [[buffer(n)]]/[[texture(n)]] index.

#include <metal_stdlib>
using namespace metal;

#include "shared/ge_metal_bindings.h"
#include "common/sample_mesh_texture.h"
#include "shared/handle_pbr.h"
#include "common/encode_normal.h"

// Multi-resolution cross-fade constants (from the GLSL #define HIGH/LOW_RES).
#define HIGH_RES_SAMPLER 1.0f
#define LOW_RES_SAMPLER  0.5f

// ---------------------------------------------------------------------------
// Fragment stage input. Locations match spm_layout.vert / splatting.frag.
struct SplatV2F
{
    float4 position [[position]];       // gl_FragCoord (unused here)
    float2 f_uv           [[user(loc1)]];
    float2 f_uv_two       [[user(loc2)]];
    int    f_material_id  [[user(loc3)]] [[flat]];
    float3 f_normal       [[user(loc5)]];
    float4 f_world_position [[user(loc8)]];
};

// Two-target G-buffer / forward output. o_normal only meaningful in the
// deferred branch (o_normal.zw are set to 0 there, matching the GLSL).
struct SplatFragOut
{
    float4 o_color  [[color(0)]];
    float4 o_normal [[color(1)]];
};

#ifdef PBR_ENABLED

// ---------------------------------------------------------------------------
// Multi-resolution detail samplers. These wrap sampleMeshTextureN from
// sample_mesh_texture.h, whose parameter list differs between the bindless and
// fallback builds, so these wrappers are likewise guarded on
// BIND_MESH_TEXTURES_AT_ONCE and forward the corresponding texture source.
//
// GLSL:
//   mix(sampleMeshTextureN(id, uv*HIGH), sampleMeshTextureN(id, uv*LOW), factor)
// Preserved verbatim (Metal mix == GLSL mix == lerp with the same operand
// order, so numerically identical).

#ifdef BIND_MESH_TEXTURES_AT_ONCE

inline float4 sampleMultiResTextureLayer2(constant GEMeshTextures& t, sampler s,
    float factor, int material_id, float2 uv)
{
    return mix(sampleMeshTexture2(t, s, material_id, uv * HIGH_RES_SAMPLER),
               sampleMeshTexture2(t, s, material_id, uv * LOW_RES_SAMPLER), factor);
}
inline float4 sampleMultiResTextureLayer3(constant GEMeshTextures& t, sampler s,
    float factor, int material_id, float2 uv)
{
    return mix(sampleMeshTexture3(t, s, material_id, uv * HIGH_RES_SAMPLER),
               sampleMeshTexture3(t, s, material_id, uv * LOW_RES_SAMPLER), factor);
}
inline float4 sampleMultiResTextureLayer4(constant GEMeshTextures& t, sampler s,
    float factor, int material_id, float2 uv)
{
    return mix(sampleMeshTexture4(t, s, material_id, uv * HIGH_RES_SAMPLER),
               sampleMeshTexture4(t, s, material_id, uv * LOW_RES_SAMPLER), factor);
}
inline float4 sampleMultiResTextureLayer5(constant GEMeshTextures& t, sampler s,
    float factor, int material_id, float2 uv)
{
    return mix(sampleMeshTexture5(t, s, material_id, uv * HIGH_RES_SAMPLER),
               sampleMeshTexture5(t, s, material_id, uv * LOW_RES_SAMPLER), factor);
}

#else  // ---- <=16-sampler fallback ----------------------------------------

inline float4 sampleMultiResTextureLayer2(texture2d<float> tex, sampler s,
    float factor, int material_id, float2 uv)
{
    return mix(sampleMeshTexture2(tex, s, material_id, uv * HIGH_RES_SAMPLER),
               sampleMeshTexture2(tex, s, material_id, uv * LOW_RES_SAMPLER), factor);
}
inline float4 sampleMultiResTextureLayer3(texture2d<float> tex, sampler s,
    float factor, int material_id, float2 uv)
{
    return mix(sampleMeshTexture3(tex, s, material_id, uv * HIGH_RES_SAMPLER),
               sampleMeshTexture3(tex, s, material_id, uv * LOW_RES_SAMPLER), factor);
}
inline float4 sampleMultiResTextureLayer4(texture2d<float> tex, sampler s,
    float factor, int material_id, float2 uv)
{
    return mix(sampleMeshTexture4(tex, s, material_id, uv * HIGH_RES_SAMPLER),
               sampleMeshTexture4(tex, s, material_id, uv * LOW_RES_SAMPLER), factor);
}
inline float4 sampleMultiResTextureLayer5(texture2d<float> tex, sampler s,
    float factor, int material_id, float2 uv)
{
    return mix(sampleMeshTexture5(tex, s, material_id, uv * HIGH_RES_SAMPLER),
               sampleMeshTexture5(tex, s, material_id, uv * LOW_RES_SAMPLER), factor);
}

#endif // BIND_MESH_TEXTURES_AT_ONCE

#endif // PBR_ENABLED

// ---------------------------------------------------------------------------
// Fragment entry point.
//
// Buffer bindings (shared/ge_metal_bindings.h):
//   GE_MTL_BUF_CAMERA        (0)  constant GECameraBuffer&
//   GE_MTL_BUF_GLOBAL_LIGHT  (3)  constant GEGlobalLightBuffer&
//   GE_MTL_BUF_MESH_TEXTURES (7)  bindless mesh-texture arg buffer (if enabled)
//
// Texture bindings:
//   GE_MTL_TEX_MESH0 (0..)   fallback per-layer mesh textures (fallback build)
//   GE_MTL_TEX_IBL_DIFFUSE  (8)  u_diffuse  irradiance cube
//   GE_MTL_TEX_IBL_SPECULAR (9)  u_specular radiance cube
//
// Samplers: index 0..7 pair with the fallback mesh textures; the two IBL cubes
// take samplers 8/9. In the bindless build the single mesh sampler is 0.
fragment SplatFragOut splatting_main(
    SplatV2F in [[stage_in]],
    constant GECameraBuffer&      u_camera       [[buffer(GE_MTL_BUF_CAMERA)]],
    constant GEGlobalLightBuffer& u_global_light [[buffer(GE_MTL_BUF_GLOBAL_LIGHT)]],
#ifdef BIND_MESH_TEXTURES_AT_ONCE
    constant GEMeshTextures&      mesh_textures  [[buffer(GE_MTL_BUF_MESH_TEXTURES)]],
    sampler                       mesh_sampler   [[sampler(GE_MTL_TEX_MESH0)]],
#else
    texture2d<float> f_mesh_texture_1 [[texture(GE_MTL_TEX_MESH0 + 1)]],
    texture2d<float> f_mesh_texture_2 [[texture(GE_MTL_TEX_MESH0 + 2)]],
    texture2d<float> f_mesh_texture_3 [[texture(GE_MTL_TEX_MESH0 + 3)]],
    texture2d<float> f_mesh_texture_4 [[texture(GE_MTL_TEX_MESH0 + 4)]],
    texture2d<float> f_mesh_texture_5 [[texture(GE_MTL_TEX_MESH0 + 5)]],
    sampler          mesh_sampler_1 [[sampler(GE_MTL_TEX_MESH0 + 1)]],
    sampler          mesh_sampler_2 [[sampler(GE_MTL_TEX_MESH0 + 2)]],
    sampler          mesh_sampler_3 [[sampler(GE_MTL_TEX_MESH0 + 3)]],
    sampler          mesh_sampler_4 [[sampler(GE_MTL_TEX_MESH0 + 4)]],
    sampler          mesh_sampler_5 [[sampler(GE_MTL_TEX_MESH0 + 5)]],
#endif
    texturecube<float> u_diffuse          [[texture(GE_MTL_TEX_IBL_DIFFUSE)]],
    sampler            u_diffuse_sampler  [[sampler(GE_MTL_TEX_IBL_DIFFUSE)]],
    texturecube<float> u_specular         [[texture(GE_MTL_TEX_IBL_SPECULAR)]],
    sampler            u_specular_sampler [[sampler(GE_MTL_TEX_IBL_SPECULAR)]])
{
    SplatFragOut out;
    out.o_color  = float4(0.0);
    out.o_normal = float4(0.0);

#ifdef PBR_ENABLED
    // mitigate repetitive patterns
    float cam_dist = length(u_camera.m_view_matrix * in.f_world_position);
    float mitigation = clamp(pow(cam_dist * 0.01, 2.0) - 0., 0., 1.);

    // Splatting part.
    // GLSL: sampleMeshTexture1(f_material_id, f_uv_two) and the detail layers
    // 2..5 via the multi-res helpers. The mesh-texture source differs by build.
#ifdef BIND_MESH_TEXTURES_AT_ONCE
    float4 splatting = sampleMeshTexture1(mesh_textures, mesh_sampler,
        in.f_material_id, in.f_uv_two);
    float4 detail0 = sampleMultiResTextureLayer2(mesh_textures, mesh_sampler,
        mitigation, in.f_material_id, in.f_uv);
    float4 detail1 = sampleMultiResTextureLayer3(mesh_textures, mesh_sampler,
        mitigation, in.f_material_id, in.f_uv);
    float4 detail2 = sampleMultiResTextureLayer4(mesh_textures, mesh_sampler,
        mitigation, in.f_material_id, in.f_uv);
    float4 detail3 = sampleMultiResTextureLayer5(mesh_textures, mesh_sampler,
        mitigation, in.f_material_id, in.f_uv);
#else
    float4 splatting = sampleMeshTexture1(f_mesh_texture_1, mesh_sampler_1,
        in.f_material_id, in.f_uv_two);
    float4 detail0 = sampleMultiResTextureLayer2(f_mesh_texture_2, mesh_sampler_2,
        mitigation, in.f_material_id, in.f_uv);
    float4 detail1 = sampleMultiResTextureLayer3(f_mesh_texture_3, mesh_sampler_3,
        mitigation, in.f_material_id, in.f_uv);
    float4 detail2 = sampleMultiResTextureLayer4(f_mesh_texture_4, mesh_sampler_4,
        mitigation, in.f_material_id, in.f_uv);
    float4 detail3 = sampleMultiResTextureLayer5(f_mesh_texture_5, mesh_sampler_5,
        mitigation, in.f_material_id, in.f_uv);
#endif

    float4 splatted = splatting.r * detail0 +
        splatting.g * detail1 +
        splatting.b * detail2 +
        max(0.0, (1.0 - splatting.r - splatting.g - splatting.b)) * detail3;

    float3 normal = normalize(in.f_normal.xyz);
    if (u_deferred)
    {
        out.o_color = float4(splatted.xyz, 0.0);
        out.o_normal.xy = EncodeNormal(normal);
        out.o_normal.zw = float2(0.0);
    }
    else
    {
        out.o_color = float4(handlePBR(splatted.xyz, float3(0.0),
            in.f_world_position, normal,
            u_camera, u_global_light,
            u_diffuse, u_diffuse_sampler,
            u_specular, u_specular_sampler), 1.0);
    }
#endif // PBR_ENABLED

    return out;
}
