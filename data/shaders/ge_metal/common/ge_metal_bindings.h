// ge_metal_bindings.h
// Shared Metal buffer/texture/sampler index constants for the native GEMetal
// backend. This is the MSL counterpart of the Vulkan descriptor layout used by
// data/shaders/ge_shaders/*: the per-frame "set = 1" uniform/SSBO block plus the
// per-pass "set = 0" textures.
//
// Vulkan (set = 1) binding  ->  Metal [[buffer(n)]]
//   camera UBO        b0     ->  GE_MTL_BUF_CAMERA        (buffer 0)
//   object SSBO       b1     ->  GE_MTL_BUF_OBJECT        (buffer 1)
//   skinning SSBO     b2     ->  GE_MTL_BUF_SKINNING      (buffer 2)
//   global light UBO  b3     ->  GE_MTL_BUF_GLOBAL_LIGHT  (buffer 3)
//   material-id SSBO  b4     ->  GE_MTL_BUF_MATERIAL_IDS  (buffer 4)
//
// Push constants (Vulkan) -> a plain constant struct bound at
//   GE_MTL_BUF_PUSH (buffer 8). Vertex geometry (S3DVertexSkinnedMesh /
//   S3DVertex) is delivered through the [[stage_in]] vertex descriptor, so
//   buffers 0..8 stay free for the uniform/SSBO blocks above regardless of the
//   MTLBuffer index the vertex stream itself occupies (the CPU offsets the
//   vertex buffer past these on the argument table).
//
// Sun shadow atlas (Vulkan set = 1, b5/b6) and AO (b7) are textures in Metal
// (a Metal argument-table texture slot, not a buffer slot), so they live in the
// GE_MTL_TEX_* range below alongside the per-pass "set = 0" textures.

#ifndef GE_METAL_BINDINGS_H
#define GE_METAL_BINDINGS_H

// ---- Per-frame uniform / storage buffers (Vulkan set = 1) ------------------
#define GE_MTL_BUF_CAMERA        0   // CameraBuffer        (camera.glsl)
#define GE_MTL_BUF_OBJECT        1   // ObjectBuffer  SSBO  (spm_data.glsl)
#define GE_MTL_BUF_SKINNING      2   // SkinningMatrices    (spm_data.glsl)
#define GE_MTL_BUF_GLOBAL_LIGHT  3   // GlobalLightBuffer   (global_light_data.glsl)
#define GE_MTL_BUF_MATERIAL_IDS  4   // MaterialIDs   SSBO  (spm_data.glsl)

// ---- Push-constant equivalent ----------------------------------------------
// GLSL layout(push_constant) blocks map to a constant struct at this index.
#define GE_MTL_BUF_PUSH          8

// ---- Bindless mesh-texture argument buffer ---------------------------------
// When BIND_MESH_TEXTURES_AT_ONCE is defined the whole mesh-texture table is
// delivered as one argument buffer of texture2d handles at this buffer slot.
#define GE_MTL_BUF_MESH_TEXTURES 10

// ---- Per-pass textures (Vulkan set = 0) ------------------------------------
// Deferred-lighting / post passes bind their input attachments here. The
// non-bindless (<=16 sampler) fallback also binds mesh textures at 0..15.
#define GE_MTL_TEX_0             0
#define GE_MTL_TEX_1             1
#define GE_MTL_TEX_2             2
#define GE_MTL_TEX_3             3
#define GE_MTL_TEX_4             4
#define GE_MTL_TEX_5             5
#define GE_MTL_TEX_6             6
#define GE_MTL_TEX_7             7

// Sun shadow atlas (Vulkan set = 1, b5 depth-compare / b6 raw depth) and AO
// (Vulkan set = 1, b7). Placed after the 8 mesh-texture fallback slots so the
// two families never collide on the argument table.
#define GE_MTL_TEX_SUN_SHADOW_PCF  16  // sampler2DShadow u_sun_shadow_pcf (b5)
#define GE_MTL_TEX_SUN_SHADOW_RAW  17  // sampler2D       u_sun_shadow_raw (b6)
#define GE_MTL_TEX_AO              18  // AO (b7)

// ---- Samplers --------------------------------------------------------------
#define GE_MTL_SAMP_0              0
#define GE_MTL_SAMP_SUN_SHADOW_PCF 16  // depth-compare sampler for the PCF atlas
#define GE_MTL_SAMP_SUN_SHADOW_RAW 17

#endif // GE_METAL_BINDINGS_H
