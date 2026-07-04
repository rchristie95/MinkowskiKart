// unlit.metal - native-Metal port of data/shaders/ge_shaders/unlit.frag
//
// "Unlit" material fragment shader. Ported FAITHFULLY / numerically identically
// from the GLSL; no math was changed.
//
// Note on structure: in the GLSL the *entire* main() body is wrapped in
// #ifdef PBR_ENABLED, so when PBR_ENABLED is undefined the shader writes nothing
// (no color attachment). This port preserves that exactly -- when PBR_ENABLED is
// not defined the fragment function is a valid-but-empty entry point that returns
// a zero-initialised FragOut, mirroring the GLSL's do-nothing main().
//
// The name "unlit" refers only to how the *deferred* branch tags the G-buffer
// (o_color.a = 0.4, o_normal.zw = 0) -- the non-deferred branch still runs the
// full handlePBR() lighting, identical to the GLSL. This is a byte-for-byte
// behavioural port, not a reinterpretation.
//
// GLSL descriptor globals become explicit MSL parameters, bound at the indices
// declared in the shared bindings header (shared/ge_metal_bindings.h). The shader
// manager supplies the same predefines the Vulkan path uses (PBR_ENABLED,
// BIND_MESH_TEXTURES_AT_ONCE, SAMPLER_SIZE, TOTAL_MESH_TEXTURE_LAYER,
// GE_SAMPLE_TEX_INDEX, u_deferred function-constant), so the #ifdef paths mirror
// the GLSL exactly.

#include <metal_stdlib>
using namespace metal;

// IMPORTANT: shared/ge_metal_bindings.h is included FIRST. It shares the include
// guard GE_METAL_BINDINGS_H with common/ge_metal_bindings.h, so whichever is
// included first "wins"; including the shared one first (as the sibling *.metal
// files do) is what defines GE_MTL_TEX_MESH0 / GE_MTL_TEX_IBL_* and the slot-15
// push-constant contract that this file and handle_pbr.h rely on. The common/
// headers' own re-include of "ge_metal_bindings.h" is then a no-op.
#include "shared/ge_metal_bindings.h"
#include "common/sample_mesh_texture.h"
#include "common/rgb_conversion.h"
#ifdef PBR_ENABLED
#include "shared/handle_pbr.h"
#include "common/encode_normal.h"
#endif

// ---------------------------------------------------------------------------
// Interpolated fragment inputs (matches unlit.frag's `in` locations).
//   location 0 f_vertex_color
//   location 1 f_uv
//   location 3 f_material_id  (flat int)
//   location 4 f_hue_change
//   location 5 f_normal          (used by the PBR body)
//   location 8 f_world_position  (used by the PBR body)
//
// f_normal / f_world_position are only read inside the #ifdef PBR_ENABLED body,
// so they are only declared there -- when PBR is off the whole main() is empty.
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
// Fragment output.
//   location 0 o_color
//   location 1 o_normal (written meaningfully only in the deferred branch)
// Both attachments are declared so the deferred function-constant can choose
// which are meaningfully written, matching the GLSL's two `out` locations.
// ---------------------------------------------------------------------------
struct FragOut
{
    float4 o_color  [[color(0)]];
    float4 o_normal [[color(1)]];
};

// ---------------------------------------------------------------------------
// Mesh-texture resource binding.
//
// unlit.frag only ever samples mesh texture 0 (sampleMeshTexture0). The PBR
// input is a hard-coded constant vec3(0.0, 0.0, 0.4) in the GLSL, so -- unlike
// solid.frag -- there is no texture-2 (roughness/metalness) sample here, and no
// texture-2 binding is needed on either path.
//
// Bindless path (BIND_MESH_TEXTURES_AT_ONCE): the whole table arrives as one
// argument buffer (GEMeshTextures) plus a single sampler, exactly like the GLSL
// f_mesh_textures[] array. Fallback path: mesh texture 0 + its sampler at the
// low texture slot, matching the GLSL #else combined-image-sampler.
// ---------------------------------------------------------------------------

fragment FragOut unlit_main(
    FragIn in [[stage_in]]
#ifdef PBR_ENABLED
  #ifdef BIND_MESH_TEXTURES_AT_ONCE
    ,
    constant GEMeshTextures& mesh_textures [[buffer(GE_MTL_BUF_MESH_TEXTURES)]],
    sampler mesh_sampler                   [[sampler(GE_MTL_TEX_MESH0)]]
  #else
    ,
    texture2d<float> f_mesh_texture_0 [[texture(GE_MTL_TEX_MESH0 + 0)]],
    sampler          f_mesh_sampler_0 [[sampler(GE_MTL_TEX_MESH0 + 0)]]
  #endif
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
    FragOut out = {};

#ifdef PBR_ENABLED
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

    if (in.f_hue_change > 0.0)
    {
        float3 old_hsv = rgbToHsv(tex_color.rgb);
        float2 new_xy = float2(in.f_hue_change, old_hsv.y);
        float3 new_color = hsvToRgb(float3(new_xy.x, new_xy.y, old_hsv.z));
        tex_color = float4(new_color.r, new_color.g, new_color.b, tex_color.a);
    }

    float3 diffuse_color = tex_color.xyz * in.f_vertex_color.xyz;
    float3 normal = normalize(in.f_normal.xyz);
    if (u_deferred)
    {
        out.o_color = float4(diffuse_color, 0.4);
        out.o_normal.xy = EncodeNormal(normal);
        out.o_normal.zw = float2(0.0);
    }
    else
    {
        float3 pbr = float3(0.0, 0.0, 0.4);
        out.o_color = float4(handlePBR(diffuse_color, pbr, in.f_world_position,
            normal, u_camera, u_global_light,
            u_diffuse, u_diffuse_sampler, u_specular, u_specular_sampler),
            1.0);
    }
#endif

    return out;
}
