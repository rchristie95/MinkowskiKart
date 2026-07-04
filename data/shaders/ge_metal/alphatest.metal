// alphatest.metal - native-Metal port of
//   data/shaders/ge_shaders/alphatest.frag        -> fragment alphatest_main
//   data/shaders/ge_shaders/alphatest_depth.frag  -> fragment alphatest_depth_main
//
// Alpha-cutout material. Both entry points sample mesh texture 0, multiply the
// alpha by the vertex-colour alpha and discard the fragment when the product is
// below 0.5 (hard cutout). The colour variant then does the full solid.frag-style
// shading; the depth variant does nothing after the discard (it is used for the
// depth/shadow pre-pass, where only the cutout matters).
//
// Ported FAITHFULLY / numerically identically from the GLSL. GLSL descriptor
// globals become explicit MSL parameters, bound at the indices declared in the
// shared bindings header. The shader manager supplies the same predefines the
// Vulkan path uses (PBR_ENABLED, BIND_MESH_TEXTURES_AT_ONCE, SAMPLER_SIZE,
// TOTAL_MESH_TEXTURE_LAYER, GE_SAMPLE_TEX_INDEX), so the two #ifdef paths mirror
// the GLSL exactly.
//
// NOTE on the hue-change block: alphatest.frag uses the *simple* hue swap
// (replace H and S directly, keep tex_color.a) - this is deliberately different
// from solid.frag's masked variant. This port reproduces alphatest.frag's block
// verbatim; do not copy solid.metal's masked version here.

#include <metal_stdlib>
using namespace metal;

#include "shared/ge_metal_bindings.h"
#include "common/sample_mesh_texture.h"
#include "common/rgb_conversion.h"
#ifdef PBR_ENABLED
#include "shared/handle_pbr.h"
#include "common/encode_normal.h"
#endif

// ---------------------------------------------------------------------------
// Interpolated fragment inputs (matches alphatest.frag's `in` locations).
//   location 0 f_vertex_color
//   location 1 f_uv
//   location 3 f_material_id  (flat int)
//   location 4 f_hue_change
//   location 5 f_normal          (PBR only)
//   location 8 f_world_position  (PBR only)
// ---------------------------------------------------------------------------
struct FragIn
{
    float4 f_vertex_color   [[user(locn0)]];
    float2 f_uv             [[user(locn1)]];
    int    f_material_id    [[user(locn3)]] [[flat]];
    float  f_hue_change     [[user(locn4)]];
#ifdef PBR_ENABLED
    float3 f_normal         [[user(locn5)]];
    float4 f_world_position [[user(locn8)]];
#endif
};

// ---------------------------------------------------------------------------
// alphatest_depth.frag inputs. Only the three interpolants needed for the
// cutout are present in the GLSL (locations 0/1/3); there is no PBR path.
// ---------------------------------------------------------------------------
struct DepthFragIn
{
    float4 f_vertex_color [[user(locn0)]];
    float2 f_uv           [[user(locn1)]];
    int    f_material_id  [[user(locn3)]] [[flat]];
};

// ---------------------------------------------------------------------------
// Fragment output (colour variant).
//   location 0 o_color
//   location 1 o_normal (PBR deferred only)
// ---------------------------------------------------------------------------
struct FragOut
{
    float4 o_color  [[color(0)]];
#ifdef PBR_ENABLED
    float4 o_normal [[color(1)]];
#endif
};

// ===========================================================================
// alphatest_main - port of alphatest.frag main().
// ===========================================================================
fragment FragOut alphatest_main(
    FragIn in [[stage_in]],
#ifdef BIND_MESH_TEXTURES_AT_ONCE
    constant GEMeshTextures& mesh_textures [[buffer(GE_MTL_BUF_MESH_TEXTURES)]],
    sampler mesh_sampler                   [[sampler(GE_MTL_TEX_MESH0)]]
#else
    texture2d<float> f_mesh_texture_0 [[texture(GE_MTL_TEX_MESH0 + 0)]],
    sampler          f_mesh_sampler_0 [[sampler(GE_MTL_TEX_MESH0 + 0)]]
  #ifdef PBR_ENABLED
    ,
    texture2d<float> f_mesh_texture_2 [[texture(GE_MTL_TEX_MESH0 + 2)]],
    sampler          f_mesh_sampler_2 [[sampler(GE_MTL_TEX_MESH0 + 2)]]
  #endif
#endif
#ifdef PBR_ENABLED
    ,
    constant GECameraBuffer&      u_camera       [[buffer(GE_MTL_BUF_CAMERA)]],
    constant GEGlobalLightBuffer& u_global_light [[buffer(GE_MTL_BUF_GLOBAL_LIGHT)]],
    texturecube<float> u_diffuse          [[texture(GE_MTL_TEX_IBL_DIFFUSE)]],
    sampler            u_diffuse_sampler  [[sampler(GE_MTL_TEX_IBL_DIFFUSE)]],
    texturecube<float> u_specular         [[texture(GE_MTL_TEX_IBL_SPECULAR)]],
    sampler            u_specular_sampler [[sampler(GE_MTL_TEX_IBL_SPECULAR)]]
#endif
    )
{
    FragOut out;

    // vec4 tex_color = sampleMeshTexture0(f_material_id, f_uv);
#ifdef BIND_MESH_TEXTURES_AT_ONCE
    float4 tex_color =
        sampleMeshTexture0(mesh_textures, mesh_sampler, in.f_material_id, in.f_uv);
#else
    float4 tex_color =
        sampleMeshTexture0(f_mesh_texture_0, f_mesh_sampler_0, in.f_material_id, in.f_uv);
#endif

    // if (tex_color.a * f_vertex_color.a < 0.5) discard;
    if (tex_color.a * in.f_vertex_color.a < 0.5)
        discard_fragment();

    // Simple hue swap (alphatest.frag variant): keep tex_color.a.
    if (in.f_hue_change > 0.0)
    {
        float3 old_hsv = rgbToHsv(tex_color.rgb);
        float2 new_xy = float2(in.f_hue_change, old_hsv.y);
        float3 new_color = hsvToRgb(float3(new_xy.x, new_xy.y, old_hsv.z));
        tex_color = float4(new_color.r, new_color.g, new_color.b, tex_color.a);
    }

#ifndef PBR_ENABLED
    float3 mixed_color = tex_color.xyz * in.f_vertex_color.xyz;
    out.o_color = float4(mixed_color, 1.0);
#else
    float3 diffuse_color = tex_color.xyz * in.f_vertex_color.xyz;
    float3 normal = normalize(in.f_normal.xyz);

  #ifdef BIND_MESH_TEXTURES_AT_ONCE
    float3 pbr =
        sampleMeshTexture2(mesh_textures, mesh_sampler, in.f_material_id, in.f_uv).xyz;
  #else
    float3 pbr =
        sampleMeshTexture2(f_mesh_texture_2, f_mesh_sampler_2, in.f_material_id, in.f_uv).xyz;
  #endif

    if (u_deferred)
    {
        out.o_color = float4(diffuse_color, pbr.z);
        float2 enc = EncodeNormal(normal);
        out.o_normal.xy = enc;
        out.o_normal.zw = pbr.xy;
    }
    else
    {
        out.o_color = float4(handlePBR(diffuse_color, pbr, in.f_world_position,
            normal, u_camera, u_global_light,
            u_diffuse, u_diffuse_sampler, u_specular, u_specular_sampler),
            1.0);
    }
#endif

    return out;
}

// ===========================================================================
// alphatest_depth_main - port of alphatest_depth.frag main().
//
// Depth/shadow pre-pass cutout: sample, discard on failed alpha test, write no
// colour. This function has no colour attachment (matches the GLSL, which
// declares no `out` variable). Depth is written by fixed-function output.
// ===========================================================================
fragment void alphatest_depth_main(
    DepthFragIn in [[stage_in]],
#ifdef BIND_MESH_TEXTURES_AT_ONCE
    constant GEMeshTextures& mesh_textures [[buffer(GE_MTL_BUF_MESH_TEXTURES)]],
    sampler mesh_sampler                   [[sampler(GE_MTL_TEX_MESH0)]]
#else
    texture2d<float> f_mesh_texture_0 [[texture(GE_MTL_TEX_MESH0 + 0)]],
    sampler          f_mesh_sampler_0 [[sampler(GE_MTL_TEX_MESH0 + 0)]]
#endif
    )
{
    // vec4 tex_color = sampleMeshTexture0(f_material_id, f_uv);
#ifdef BIND_MESH_TEXTURES_AT_ONCE
    float4 tex_color =
        sampleMeshTexture0(mesh_textures, mesh_sampler, in.f_material_id, in.f_uv);
#else
    float4 tex_color =
        sampleMeshTexture0(f_mesh_texture_0, f_mesh_sampler_0, in.f_material_id, in.f_uv);
#endif

    // if (tex_color.a * f_vertex_color.a < 0.5) discard;
    if (tex_color.a * in.f_vertex_color.a < 0.5)
        discard_fragment();
}
