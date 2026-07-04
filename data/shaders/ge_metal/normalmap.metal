// normalmap.metal - native-Metal port of
//   data/shaders/ge_shaders/normalmap.frag
//
// Purpose: tangent-space normal map -> world-space normal via the per-fragment
// TBN basis, feeding either the deferred G-buffer (o_color + encoded o_normal)
// or the forward PBR lighting path (handlePBR).
//
// This is a FAITHFUL, numerically-identical port of the GLSL fragment shader.
// The GLSL vertex->fragment varyings (spm.vert) become a [[stage_in]] struct;
// the two GLSL descriptor families are threaded in as MSL arguments per the
// shared binding contract:
//   * mesh textures  -> sample_mesh_texture.h (bindless arg-buffer OR <=16
//                       sampler fallback, guarded by BIND_MESH_TEXTURES_AT_ONCE
//                       exactly like the GLSL include).
//   * camera UBO      set=1 b0  -> constant GECameraBuffer&        (buffer 0)
//   * global light    set=1 b3  -> constant GEGlobalLightBuffer&   (buffer 3)
//   * IBL cubemaps    set=2 b0/1-> texturecube<float> + sampler    (tex 8/9)
//
// Spec constant u_deferred (constant_id = 2) -> [[function_constant(2)]],
// declared in the shared bindings header. PBR_ENABLED / BIND_MESH_TEXTURES_AT_ONCE
// are CPU predefines emitted by ge_metal_shader_manager, matching the Vulkan path.
//
// IMPORTANT header-order note: the shared ge_metal_bindings.h (which defines
// GECameraBuffer / GEGlobalLightBuffer / the six [[function_constant]] spec
// constants used by handle_pbr.h) must be seen first. The common/ headers below
// pull in a same-guarded ge_metal_bindings.h whose re-include is suppressed;
// they reference none of the common-header GE_MTL_* defines in their bodies
// (only SAMPLER_SIZE / TOTAL_MESH_TEXTURE_LAYER predefines), so this is safe.

#include <metal_stdlib>
using namespace metal;

#include "shared/ge_metal_bindings.h"      // GECameraBuffer, GEGlobalLightBuffer,
                                            // u_deferred (function_constant 2), ...
#include "common/sample_mesh_texture.h"    // sampleMeshTexture0/2/3
#include "common/rgb_conversion.h"         // rgbToHsv / hsvToRgb
#ifdef PBR_ENABLED
#include "shared/handle_pbr.h"             // handlePBR (forward PBR path)
#include "common/encode_normal.h"          // EncodeNormal (deferred G-buffer)
#endif

// ---------------------------------------------------------------------------
// Fragment stage input. Mirrors the GLSL `layout(location = N) in ...` varyings
// written by spm.vert. GLSL location -> MSL [[user(locN)]]; the `flat in int`
// material id -> [[flat]]. The PBR-only varyings are compiled in only when
// PBR_ENABLED, matching the GLSL #ifdef guard on the fragment inputs.
// ---------------------------------------------------------------------------
struct NormalMapIn
{
    float4 f_vertex_color [[user(loc0)]];
    float2 f_uv           [[user(loc1)]];
    int    f_material_id  [[user(loc3)]] [[flat]];
    float  f_hue_change   [[user(loc4)]];
#ifdef PBR_ENABLED
    float3 f_normal       [[user(loc5)]];
    float3 f_tangent      [[user(loc6)]];
    float3 f_bitangent    [[user(loc7)]];
    float4 f_world_position [[user(loc8)]];
#endif
};

// ---------------------------------------------------------------------------
// Fragment outputs. Non-PBR / forward-PBR write a single colour attachment;
// the deferred path additionally writes the encoded-normal + roughness/metal
// G-buffer attachment (GLSL o_normal at location 1).
// ---------------------------------------------------------------------------
struct NormalMapOut
{
    float4 o_color  [[color(0)]];
#if defined(PBR_ENABLED)
    float4 o_normal [[color(1)]];
#endif
};

fragment NormalMapOut normalmap_fragment(
    NormalMapIn in [[stage_in]]
#ifdef BIND_MESH_TEXTURES_AT_ONCE
  , constant GEMeshTextures& f_mesh_textures [[buffer(GE_MTL_BUF_MESH_TEXTURES)]]
  , sampler                  f_mesh_sampler  [[sampler(GE_MTL_TEX_MESH0)]]
#else
  // <=16-sampler fallback: GLSL binds mesh layer N at binding N; the shared
  // bindings header expresses that base as GE_MTL_TEX_MESH0 (= 0) with layers
  // at +N. The matching sampler shares the texture's index in Metal.
  , texture2d<float> f_mesh_texture_0 [[texture(GE_MTL_TEX_MESH0 + 0)]]
  , sampler          f_mesh_sampler_0 [[sampler(GE_MTL_TEX_MESH0 + 0)]]
  #ifdef PBR_ENABLED
  , texture2d<float> f_mesh_texture_2 [[texture(GE_MTL_TEX_MESH0 + 2)]]
  , sampler          f_mesh_sampler_2 [[sampler(GE_MTL_TEX_MESH0 + 2)]]
  , texture2d<float> f_mesh_texture_3 [[texture(GE_MTL_TEX_MESH0 + 3)]]
  , sampler          f_mesh_sampler_3 [[sampler(GE_MTL_TEX_MESH0 + 3)]]
  #endif
#endif
#ifdef PBR_ENABLED
  , constant GECameraBuffer&      u_camera       [[buffer(GE_MTL_BUF_CAMERA)]]
  , constant GEGlobalLightBuffer& u_global_light [[buffer(GE_MTL_BUF_GLOBAL_LIGHT)]]
  , texturecube<float> u_diffuse          [[texture(GE_MTL_TEX_IBL_DIFFUSE)]]
  , sampler            u_diffuse_sampler  [[sampler(GE_MTL_TEX_IBL_DIFFUSE)]]
  , texturecube<float> u_specular         [[texture(GE_MTL_TEX_IBL_SPECULAR)]]
  , sampler            u_specular_sampler [[sampler(GE_MTL_TEX_IBL_SPECULAR)]]
#endif
)
{
    NormalMapOut out;

    // --- Mesh base colour (layer 0) ---------------------------------------
#ifdef BIND_MESH_TEXTURES_AT_ONCE
    float4 tex_color = sampleMeshTexture0(f_mesh_textures, f_mesh_sampler,
                                          in.f_material_id, in.f_uv);
#else
    float4 tex_color = sampleMeshTexture0(f_mesh_texture_0, f_mesh_sampler_0,
                                          in.f_material_id, in.f_uv);
#endif

    // --- Hue-change recolour (identical branch/math to the GLSL) ----------
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

#ifndef PBR_ENABLED
    float3 mixed_color = tex_color.xyz * in.f_vertex_color.xyz;
    out.o_color = float4(mixed_color, 1.0);
#else
    float3 diffuse_color = tex_color.xyz * in.f_vertex_color.xyz;

    // --- Tangent-space normal map -> world normal via TBN -----------------
    #ifdef BIND_MESH_TEXTURES_AT_ONCE
    float4 layer_3 = sampleMeshTexture3(f_mesh_textures, f_mesh_sampler,
                                        in.f_material_id, in.f_uv);
    #else
    float4 layer_3 = sampleMeshTexture3(f_mesh_texture_3, f_mesh_sampler_3,
                                        in.f_material_id, in.f_uv);
    #endif
    float3 tangent_space_normal = 2.0 * layer_3.xyz - 1.0;
    float3 frag_tangent   = normalize(in.f_tangent);
    float3 frag_bitangent = normalize(in.f_bitangent);
    float3 frag_normal    = normalize(in.f_normal);
    // GLSL mat3(t, b, n) is column-major: columns are tangent/bitangent/normal.
    // MSL float3x3(c0, c1, c2) is likewise column-major, so this matches and
    // t_b_n * v == v.x*T + v.y*B + v.z*N exactly as in the original.
    float3x3 t_b_n = float3x3(frag_tangent, frag_bitangent, frag_normal);

    float3 normal = normalize(t_b_n * tangent_space_normal);

    // --- PBR material params (layer 2: roughness/metal/emit) --------------
    #ifdef BIND_MESH_TEXTURES_AT_ONCE
    float3 pbr = sampleMeshTexture2(f_mesh_textures, f_mesh_sampler,
                                    in.f_material_id, in.f_uv).xyz;
    #else
    float3 pbr = sampleMeshTexture2(f_mesh_texture_2, f_mesh_sampler_2,
                                    in.f_material_id, in.f_uv).xyz;
    #endif

    if (u_deferred)
    {
        out.o_color = float4(diffuse_color, pbr.z);
        out.o_normal.xy = EncodeNormal(normal);
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
