// ghost.metal - native-Metal port of data/shaders/ge_shaders/ghost.frag
//
// Ghost replay-kart fragment shader. It samples mesh texture 0, optionally
// re-hues the "team-colour" masked region (f_hue_change), multiplies by the
// interpolated vertex colour, then outputs the kart at half brightness / half
// alpha so the replay ghost renders as a translucent overlay:
//
//     o_color = vec4(mixed_color * 0.5, 0.5);
//
// The hue-change block is identical to solid.frag; the optional PBR tone
// conversion (convertColor) is identical to transparent.frag. Both are selected
// exactly like the GLSL #ifdef PBR_ENABLED:
//   * PBR_ENABLED undefined -> saturation boost is mask * 1.825, mixed_color
//                              used as-is.
//   * PBR_ENABLED defined    -> saturation boost is mask * 2.5, and
//                              mixed_color = convertColor(mixed_color)
//                              (ge_convertColor from handle_pbr.h, which honours
//                               the u_ibl function-constant, constant_id = 0).
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
#include "common/rgb_conversion.h"
#ifdef PBR_ENABLED
// Provides ge_convertColor() (the MSL name for constants_utils.glsl convertColor).
#include "shared/handle_pbr.h"
#endif

// ---------------------------------------------------------------------------
// Interpolated fragment inputs (matches ghost.frag's `in` locations).
//   location 0 f_vertex_color
//   location 1 f_uv
//   location 3 f_material_id (flat int)
//   location 4 f_hue_change
// ---------------------------------------------------------------------------
struct FragIn
{
    float4 f_vertex_color [[user(locn0)]];
    float2 f_uv           [[user(locn1)]];
    int    f_material_id  [[user(locn3)]] [[flat]];
    float  f_hue_change   [[user(locn4)]];
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

fragment FragOut ghost_main(
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

    // vec4 tex_color = sampleMeshTexture0(f_material_id, f_uv);
#ifdef BIND_MESH_TEXTURES_AT_ONCE
    float4 tex_color =
        sampleMeshTexture0(mesh_textures, mesh_sampler, in.f_material_id, in.f_uv);
#else
    float4 tex_color =
        sampleMeshTexture0(f_mesh_texture_0, f_mesh_sampler_0, in.f_material_id, in.f_uv);
#endif

    if (in.f_hue_change > 0.0)
    {
        float mask = tex_color.a;
        float3 old_hsv = rgbToHsv(tex_color.rgb);
        float mask_step = step(mask, 0.5);
#ifndef PBR_ENABLED
        // For similar color
        float saturation = mask * 1.825; // 2.5 * 0.5 ^ (1. / 2.2)
#else
        float saturation = mask * 2.5;
#endif
        float2 new_xy = mix(float2(old_hsv.x, old_hsv.y),
            float2(in.f_hue_change, max(old_hsv.y, saturation)),
            float2(mask_step, mask_step));
        float3 new_color = hsvToRgb(float3(new_xy.x, new_xy.y, old_hsv.z));
        tex_color = float4(new_color.r, new_color.g, new_color.b, 1.0);
    }

    float3 mixed_color = tex_color.xyz * in.f_vertex_color.xyz;
#ifdef PBR_ENABLED
    mixed_color = ge_convertColor(mixed_color);
#endif
    out.o_color = float4(mixed_color * 0.5, 0.5);

    return out;
}
