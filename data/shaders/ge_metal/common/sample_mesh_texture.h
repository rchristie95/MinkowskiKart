// sample_mesh_texture.h - MSL port of
// data/shaders/ge_shaders/utils/sample_mesh_texture.glsl
//
// Two paths, guarded exactly like the GLSL #ifdef BIND_MESH_TEXTURES_AT_ONCE:
//
//   * Bindless (BIND_MESH_TEXTURES_AT_ONCE defined): the whole mesh-texture
//     table is delivered as one Metal argument buffer of texture2d handles
//     (GEMeshTextures below, bound at GE_MTL_BUF_MESH_TEXTURES). The GLSL index
//     math (TOTAL_MESH_TEXTURE_LAYER * material_id + layer) is preserved. The
//     GLSL GE_SAMPLE_TEX_INDEX(id) macro wraps the index in nonuniformEXT for
//     divergent indexing; in MSL argument-buffer indexing is already dynamically
//     uniform-agnostic, so it becomes a plain int index.
//
//   * Fallback (<=16 samplers, BIND_MESH_TEXTURES_AT_ONCE undefined): the
//     individual mesh textures are passed in. Mirrors the GLSL #else branch,
//     which only declares textures 0..1 unless PBR_ENABLED adds 2..7.
//
// All functions take an explicit sampler so the caller controls filtering/wrap
// (GLSL combined-image-samplers are split into texture + sampler in MSL).
#ifndef GE_METAL_SAMPLE_MESH_TEXTURE_H
#define GE_METAL_SAMPLE_MESH_TEXTURE_H

#include <metal_stdlib>
using namespace metal;

#include "../shared/ge_metal_bindings.h"

// SAMPLER_SIZE and TOTAL_MESH_TEXTURE_LAYER are injected by the shader manager
// predefines (see GEMetalShaderManager::init), matching the Vulkan path.
#ifndef TOTAL_MESH_TEXTURE_LAYER
#define TOTAL_MESH_TEXTURE_LAYER 2
#endif

#ifdef BIND_MESH_TEXTURES_AT_ONCE

// Argument buffer holding the full mesh-texture table. SAMPLER_SIZE *
// TOTAL_MESH_TEXTURE_LAYER matches the GLSL array size; the concrete count is
// filled in from the predefines by the shader manager.
struct GEMeshTextures
{
    array<texture2d<float>, (SAMPLER_SIZE * TOTAL_MESH_TEXTURE_LAYER)>
        f_mesh_textures [[id(0)]];
};

inline float4 sampleMeshTexture0(constant GEMeshTextures& t, sampler s,
                                 int material_id, float2 uv)
{
    int id = (TOTAL_MESH_TEXTURE_LAYER * material_id) + 0;
    return t.f_mesh_textures[id].sample(s, uv);
}

inline float4 sampleMeshTexture1(constant GEMeshTextures& t, sampler s,
                                 int material_id, float2 uv)
{
    int id = (TOTAL_MESH_TEXTURE_LAYER * material_id) + 1;
    return t.f_mesh_textures[id].sample(s, uv);
}

inline float4 sampleMeshTexture2(constant GEMeshTextures& t, sampler s,
                                 int material_id, float2 uv)
{
    int id = (TOTAL_MESH_TEXTURE_LAYER * material_id) + 2;
    return t.f_mesh_textures[id].sample(s, uv);
}

inline float4 sampleMeshTexture3(constant GEMeshTextures& t, sampler s,
                                 int material_id, float2 uv)
{
    int id = (TOTAL_MESH_TEXTURE_LAYER * material_id) + 3;
    return t.f_mesh_textures[id].sample(s, uv);
}

inline float4 sampleMeshTexture4(constant GEMeshTextures& t, sampler s,
                                 int material_id, float2 uv)
{
    int id = (TOTAL_MESH_TEXTURE_LAYER * material_id) + 4;
    return t.f_mesh_textures[id].sample(s, uv);
}

inline float4 sampleMeshTexture5(constant GEMeshTextures& t, sampler s,
                                 int material_id, float2 uv)
{
    int id = (TOTAL_MESH_TEXTURE_LAYER * material_id) + 5;
    return t.f_mesh_textures[id].sample(s, uv);
}

inline float4 sampleMeshTexture6(constant GEMeshTextures& t, sampler s,
                                 int material_id, float2 uv)
{
    int id = (TOTAL_MESH_TEXTURE_LAYER * material_id) + 6;
    return t.f_mesh_textures[id].sample(s, uv);
}

inline float4 sampleMeshTexture7(constant GEMeshTextures& t, sampler s,
                                 int material_id, float2 uv)
{
    int id = (TOTAL_MESH_TEXTURE_LAYER * material_id) + 7;
    return t.f_mesh_textures[id].sample(s, uv);
}

#else  // ---- <=16-sampler fallback (mirrors GLSL #else branch) -------------

// The fallback declares only textures 0..1 unless PBR_ENABLED. Rather than fix a
// binding layout here (Metal textures are function parameters, not globals), the
// caller passes the bound mesh textures in. material_id is unused in this path,
// exactly like the GLSL fallback which samples fixed bindings.

inline float4 sampleMeshTexture0(texture2d<float> f_mesh_texture_0, sampler s,
                                 int material_id, float2 uv)
{
    return f_mesh_texture_0.sample(s, uv);
}

inline float4 sampleMeshTexture1(texture2d<float> f_mesh_texture_1, sampler s,
                                 int material_id, float2 uv)
{
    return f_mesh_texture_1.sample(s, uv);
}

#ifdef PBR_ENABLED
inline float4 sampleMeshTexture2(texture2d<float> f_mesh_texture_2, sampler s,
                                 int material_id, float2 uv)
{
    return f_mesh_texture_2.sample(s, uv);
}

inline float4 sampleMeshTexture3(texture2d<float> f_mesh_texture_3, sampler s,
                                 int material_id, float2 uv)
{
    return f_mesh_texture_3.sample(s, uv);
}

inline float4 sampleMeshTexture4(texture2d<float> f_mesh_texture_4, sampler s,
                                 int material_id, float2 uv)
{
    return f_mesh_texture_4.sample(s, uv);
}

inline float4 sampleMeshTexture5(texture2d<float> f_mesh_texture_5, sampler s,
                                 int material_id, float2 uv)
{
    return f_mesh_texture_5.sample(s, uv);
}

inline float4 sampleMeshTexture6(texture2d<float> f_mesh_texture_6, sampler s,
                                 int material_id, float2 uv)
{
    return f_mesh_texture_6.sample(s, uv);
}

inline float4 sampleMeshTexture7(texture2d<float> f_mesh_texture_7, sampler s,
                                 int material_id, float2 uv)
{
    return f_mesh_texture_7.sample(s, uv);
}
#endif  // PBR_ENABLED

#endif  // BIND_MESH_TEXTURES_AT_ONCE

#endif // GE_METAL_SAMPLE_MESH_TEXTURE_H
