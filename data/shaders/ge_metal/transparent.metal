// transparent.metal - native-Metal port of data/shaders/ge_shaders/transparent.frag
//
// Alpha-blended transparent material fragment shader. It samples mesh texture 0,
// modulates by the interpolated vertex colour, then premultiplies the RGB by the
// alpha (the blend pipeline expects premultiplied colour: SRC_ALPHA-style output
// with the colour already scaled by alpha).
//
// Behaviour matches the GLSL exactly, including the optional PBR tone conversion
// selected by the #ifdef PBR_ENABLED predefine:
//   * PBR_ENABLED undefined -> mixed_color used as-is.
//   * PBR_ENABLED defined    -> mixed_color = convertColor(mixed_color)
//                               (ge_convertColor from handle_pbr.h, which honours
//                                the u_ibl function-constant, constant_id = 0).
//
// Ported FAITHFULLY / numerically identically from the GLSL. GLSL descriptor
// globals become explicit MSL parameters, bound at the indices declared in the
// shared bindings header. The shader manager supplies the same predefines the
// Vulkan path uses (PBR_ENABLED, BIND_MESH_TEXTURES_AT_ONCE, SAMPLER_SIZE,
// TOTAL_MESH_TEXTURE_LAYER, GE_SAMPLE_TEX_INDEX), so the two #ifdef paths mirror
// the GLSL exactly.

#include <metal_stdlib>
using namespace metal;

#include "shared/ge_metal_bindings.h"
#include "common/sample_mesh_texture.h"
#ifdef PBR_ENABLED
// Provides ge_convertColor() (the MSL name for constants_utils.glsl convertColor).
#include "shared/handle_pbr.h"
#endif

// ---------------------------------------------------------------------------
// Interpolated fragment inputs (matches transparent.frag's `in` locations).
//   location 0 f_vertex_color
//   location 1 f_uv
//   location 3 f_material_id (flat int)
// ---------------------------------------------------------------------------
struct FragIn
{
    float4 f_vertex_color [[user(locn0)]];
    float2 f_uv           [[user(locn1)]];
    int    f_material_id  [[user(locn3)]] [[flat]];
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
// Mesh-texture resource binding.
//
// Bindless path (BIND_MESH_TEXTURES_AT_ONCE): the whole table arrives as one
// argument buffer (GEMeshTextures) plus a single sampler, exactly like the GLSL
// f_mesh_textures[] array. Fallback path: texture 0 + its sampler at the low
// texture slot, matching the GLSL #else combined-image-sampler for binding 0.
// ---------------------------------------------------------------------------

fragment FragOut transparent_main(
    FragIn in [[stage_in]]
#ifdef BIND_MESH_TEXTURES_AT_ONCE
    ,
    constant GEMeshTextures& mesh_textures [[buffer(GE_MTL_BUF_MESH_TEXTURES)]],
    sampler mesh_sampler                   [[sampler(GE_MTL_TEX_MESH0)]]
#else
    ,
    texture2d<float> f_mesh_texture_0 [[texture(GE_MTL_TEX_MESH0 + 0)]],
    sampler          f_mesh_sampler_0 [[sampler(GE_MTL_TEX_MESH0 + 0)]]
#endif
    )
{
    FragOut out;

    // vec4 color = sampleMeshTexture0(f_material_id, f_uv) * f_vertex_color;
#ifdef BIND_MESH_TEXTURES_AT_ONCE
    float4 color =
        sampleMeshTexture0(mesh_textures, mesh_sampler, in.f_material_id, in.f_uv) *
        in.f_vertex_color;
#else
    float4 color =
        sampleMeshTexture0(f_mesh_texture_0, f_mesh_sampler_0, in.f_material_id, in.f_uv) *
        in.f_vertex_color;
#endif

    float3 mixed_color = color.xyz;
    float alpha = color.w;
#ifdef PBR_ENABLED
    mixed_color = ge_convertColor(mixed_color);
#endif
    out.o_color = float4(mixed_color * alpha, alpha);

    return out;
}
