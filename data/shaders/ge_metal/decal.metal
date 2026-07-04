// decal.metal - native-Metal port of data/shaders/ge_shaders/decal.frag
//
// Purpose: decal blend. Layer-two texture (premultiplied by its own alpha) is
// composited over the base texture:
//     final = layer2.rgb*layer2.a + base.rgb*(1 - layer2.a)
//
// Two build variants, guarded exactly like the GLSL #ifdef PBR_ENABLED:
//   * forward/UI (PBR_ENABLED undefined): opaque colour out, alpha forced to 1.
//   * PBR:
//       - deferred (u_deferred == true): writes final colour + roughness to
//         o_color, and encoded normal + metallic/AO to o_normal (MRT).
//       - forward   (u_deferred == false): runs the full PBR lighting path.
//
// GLSL -> MSL binding map (see shared/ge_metal_bindings.h):
//   set=1 b0 CameraBuffer        -> constant GECameraBuffer&        [[buffer(0)]]
//   set=1 b3 GlobalLightBuffer   -> constant GEGlobalLightBuffer&   [[buffer(3)]]
//   set=2 b0 u_diffuse  (cube)   -> texturecube [[texture(8)]] + sampler
//   set=2 b1 u_specular (cube)   -> texturecube [[texture(9)]] + sampler
//   mesh textures                -> bindless arg buffer (GE_MTL_BUF_MESH_TEXTURES)
//                                   or <=16-sampler fallback (GE_MTL_TEX_MESH0..)
//
// The blend / composite math is copied verbatim from the GLSL; nothing here
// "improves" it.

#include <metal_stdlib>
using namespace metal;

#include "shared/ge_metal_bindings.h"
#include "common/sample_mesh_texture.h"
#ifdef PBR_ENABLED
#include "shared/handle_pbr.h"
#include "common/encode_normal.h"
#endif

// ---------------------------------------------------------------------------
// Interpolants coming from spm.vert (the SPM vertex shader that feeds the decal
// pass). Locations match the GLSL `in` declarations in decal.frag:
//   location 1 f_uv, 2 f_uv_two, 3 flat f_material_id,
//   5 f_normal, 8 f_world_position (PBR only).
// ---------------------------------------------------------------------------
struct DecalVaryings
{
    float2 f_uv          [[user(locn1)]];
    float2 f_uv_two      [[user(locn2)]];
    int    f_material_id [[user(locn3)]] [[flat]];
#ifdef PBR_ENABLED
    float3 f_normal         [[user(locn5)]];
    float4 f_world_position [[user(locn8)]];
#endif
};

// ---------------------------------------------------------------------------
// Fragment output. Single colour target in the non-PBR / forward-PBR case; the
// deferred-PBR case also writes an encoded-normal target (GLSL o_normal,
// location 1).
// ---------------------------------------------------------------------------
#if defined(PBR_ENABLED)
struct DecalOut
{
    float4 o_color  [[color(0)]];
    float4 o_normal [[color(1)]];
};
#endif

// ---------------------------------------------------------------------------
// Mesh-texture access is threaded through as function parameters: bindless
// argument buffer, or the <=16-sampler fallback. Matches the two paths in
// common/sample_mesh_texture.h (guarded by BIND_MESH_TEXTURES_AT_ONCE).
// ---------------------------------------------------------------------------
fragment
#if defined(PBR_ENABLED)
DecalOut
#else
float4
#endif
decal_main(
    DecalVaryings in [[stage_in]],

#ifdef BIND_MESH_TEXTURES_AT_ONCE
    constant GEMeshTextures& u_mesh_textures
        [[buffer(GE_MTL_BUF_MESH_TEXTURES)]],
    sampler u_mesh_sampler [[sampler(GE_MTL_TEX_MESH0)]]
#else
    texture2d<float> f_mesh_texture_0 [[texture(GE_MTL_TEX_MESH0 + 0)]],
    sampler          f_mesh_sampler_0 [[sampler(GE_MTL_TEX_MESH0 + 0)]],
    texture2d<float> f_mesh_texture_1 [[texture(GE_MTL_TEX_MESH0 + 1)]],
    sampler          f_mesh_sampler_1 [[sampler(GE_MTL_TEX_MESH0 + 1)]]
#ifdef PBR_ENABLED
    ,
    texture2d<float> f_mesh_texture_2 [[texture(GE_MTL_TEX_MESH0 + 2)]],
    sampler          f_mesh_sampler_2 [[sampler(GE_MTL_TEX_MESH0 + 2)]]
#endif
#endif // BIND_MESH_TEXTURES_AT_ONCE

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
    // --- sample base (layer 0) + decal (layer 1) ---------------------------
#ifdef BIND_MESH_TEXTURES_AT_ONCE
    float4 color =
        sampleMeshTexture0(u_mesh_textures, u_mesh_sampler, in.f_material_id, in.f_uv);
    float4 layer_two_tex =
        sampleMeshTexture1(u_mesh_textures, u_mesh_sampler, in.f_material_id, in.f_uv_two);
#else
    float4 color =
        sampleMeshTexture0(f_mesh_texture_0, f_mesh_sampler_0, in.f_material_id, in.f_uv);
    float4 layer_two_tex =
        sampleMeshTexture1(f_mesh_texture_1, f_mesh_sampler_1, in.f_material_id, in.f_uv_two);
#endif

    // Premultiply the decal by its own alpha, then over-composite onto base.
    layer_two_tex.rgb = layer_two_tex.a * layer_two_tex.rgb;
    float3 final_color = layer_two_tex.rgb + color.rgb * (1.0 - layer_two_tex.a);

#ifndef PBR_ENABLED
    return float4(final_color, 1.0);
#else
    float3 normal = normalize(in.f_normal.xyz);
#ifdef BIND_MESH_TEXTURES_AT_ONCE
    float3 pbr =
        sampleMeshTexture2(u_mesh_textures, u_mesh_sampler, in.f_material_id, in.f_uv).xyz;
#else
    float3 pbr =
        sampleMeshTexture2(f_mesh_texture_2, f_mesh_sampler_2, in.f_material_id, in.f_uv).xyz;
#endif

    DecalOut out;
    if (u_deferred)
    {
        out.o_color = float4(final_color, pbr.z);
        out.o_normal.xy = EncodeNormal(normal);
        out.o_normal.zw = pbr.xy;
    }
    else
    {
        out.o_color = float4(
            handlePBR(final_color, pbr, in.f_world_position, normal,
                      u_camera, u_global_light,
                      u_diffuse, u_diffuse_sampler,
                      u_specular, u_specular_sampler),
            1.0);
        // o_normal is undefined in the forward path in the GLSL too (only
        // o_color is written); keep it deterministic to avoid a garbage MRT
        // write when both attachments are bound.
        out.o_normal = float4(0.0);
    }
    return out;
#endif
}
