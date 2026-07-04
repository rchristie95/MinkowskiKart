#ifndef GE_METAL_BINDINGS_H
#define GE_METAL_BINDINGS_H

// ============================================================================
// Native-Metal binding index constants + shared UBO/SSBO struct layouts.
//
// This header is the Metal counterpart to the GLSL descriptor-set scheme used
// by the ge_shaders/ Vulkan pipeline. It exists so the ported MSL headers
// (handle_pbr.h, pbr_light.h, etc.) can name their [[buffer(n)]] / [[texture(n)]]
// attachment indices symbolically instead of hard-coding raw numbers.
//
// GLSL descriptor scheme -> Metal index mapping
// ---------------------------------------------------------------------------
//   set = 1, binding = 0  CameraBuffer            -> buffer(GE_MTL_BUF_CAMERA)
//   set = 1, binding = 1  ObjectBuffer (SSBO)     -> buffer(GE_MTL_BUF_OBJECT)
//   set = 1, binding = 2  SkinningMatrices (SSBO) -> buffer(GE_MTL_BUF_SKINNING)
//   set = 1, binding = 3  GlobalLightBuffer       -> buffer(GE_MTL_BUF_GLOBAL_LIGHT)
//   set = 1, binding = 4  MaterialIDs (SSBO)      -> buffer(GE_MTL_BUF_MATERIAL_ID)
//   set = 1, binding = 5  sun shadow (near)       -> buffer(GE_MTL_BUF_SHADOW_NEAR)
//   set = 1, binding = 6  sun shadow (far)        -> buffer(GE_MTL_BUF_SHADOW_FAR)
//   set = 1, binding = 7  ambient occlusion       -> texture(GE_MTL_TEX_AO)
//
//   set = 2, binding = 0  u_diffuse  (samplerCube irradiance) -> texture(GE_MTL_TEX_IBL_DIFFUSE)
//   set = 2, binding = 1  u_specular (samplerCube radiance)   -> texture(GE_MTL_TEX_IBL_SPECULAR)
//
//   set = 0 (per-pass) mesh textures:
//     BIND_MESH_TEXTURES_AT_ONCE -> Metal argument buffer at
//       buffer(GE_MTL_BUF_MESH_TEXTURES) (bindless, tier 2)
//     <=16-sampler fallback      -> texture(GE_MTL_TEX_MESH0 .. +7) + matching
//       samplers at the same slot.
//
// Metal NDC z is [0,1]; the projection matrix (or the caller's clip.z remap)
// already bakes the z remap, so nothing here touches it.
// ============================================================================

// ---- Fragment/vertex buffer slots (set = 1 "engine" descriptors) -----------
#define GE_MTL_BUF_CAMERA        0
#define GE_MTL_BUF_OBJECT        1
#define GE_MTL_BUF_SKINNING      2
#define GE_MTL_BUF_GLOBAL_LIGHT  3
#define GE_MTL_BUF_MATERIAL_ID   4
#define GE_MTL_BUF_SHADOW_NEAR   5
#define GE_MTL_BUF_SHADOW_FAR    6

// Argument buffer holding the bindless mesh-texture table (tier-2 path).
#define GE_MTL_BUF_MESH_TEXTURES 7

// Push-constant equivalent: a small constant struct at a fixed, high slot so it
// never collides with the engine descriptors above.
#define GE_MTL_BUF_PUSH_CONSTANT 15

// ---- Texture slots ---------------------------------------------------------
// Per-pass mesh textures (16-sampler fallback path). Metal binds each texture
// and its sampler at the same index.
#define GE_MTL_TEX_MESH0         0   // slots 0..7 for the 8 PBR mesh layers

// Screen-space / engine textures.
#define GE_MTL_TEX_AO            7   // set=1 binding=7 ambient occlusion

// IBL cubemaps (set = 2).
#define GE_MTL_TEX_IBL_DIFFUSE   8   // u_diffuse  (irradiance)
#define GE_MTL_TEX_IBL_SPECULAR  9   // u_specular (radiance)

// ---- Specialization constants (GLSL layout(constant_id=n)) -----------------
// GLSL: constants_utils.glsl. MSL uses [[function_constant(n)]] with matching
// ids. Declared here so every ported header sees the same defaults.
constant bool  u_ibl                     [[function_constant(0)]];
constant float u_specular_levels_minus_one [[function_constant(1)]];
constant bool  u_deferred                [[function_constant(2)]];
constant bool  u_has_skybox              [[function_constant(3)]];
constant bool  u_ssr                     [[function_constant(4)]];
constant uint  u_hiz_iterations          [[function_constant(5)]];

// ============================================================================
// Shared UBO / SSBO struct layouts.
//
// Field order + types match the GLSL std140 blocks exactly. Metal's default
// alignment for float4/float4x4 matches std140 for these all-vec4/mat4 blocks,
// so no manual padding is required beyond what the GLSL already carries.
// ============================================================================

// camera.glsl : set = 1, binding = 0
struct GECameraBuffer
{
    metal::float4x4 m_view_matrix;
    metal::float4x4 m_projection_matrix;
    metal::float4x4 m_inverse_view_matrix;
    metal::float4x4 m_inverse_projection_matrix;
    metal::float4x4 m_projection_view_matrix;
    metal::float4x4 m_inverse_projection_view_matrix;
    metal::float4   m_viewport;
    metal::float2   m_screensize;
    metal::float2   m_padding;
    metal::float4   m_relativity_params;
    metal::float4   m_relativity_beta;
    metal::float4   m_relativity_observer_pos;
    metal::float4   m_relativity_bubble;
    metal::float4   m_black_holes[4];
    metal::float4   m_wormhole;
    metal::float4   m_grav_wave;
    metal::float4x4 m_previous_pv_matrix;
    metal::float4   m_motion_blur;
    metal::float4   m_compactification;
    metal::float4   m_godrays_pos;
    metal::float4   m_godrays_color;
    metal::float4   m_postfx_flags;
    metal::float4x4 m_sun_shadow_matrix;
    metal::float4   m_shadow_params;
    metal::float4   m_postfx_flags2;
    metal::float4x4 m_sun_shadow_matrix_far;
    metal::float4   m_shadow_params_far;
    metal::float4   m_beauty_params;
};

// global_light_data.glsl : set = 1, binding = 3
#define GE_MTL_MAX_LIGHT 32

struct GELightData
{
    metal::float4 m_position_radius;
    metal::float4 m_color_inverse_square_range;
    metal::float4 m_direction_scale_offset; // Spotlight only
};

struct GEGlobalLightBuffer
{
    metal::packed_float3 m_ambient_color;
    float                m_sun_scatter;
    metal::packed_float3 m_sun_color;
    float                m_sun_angle_tan_half;
    metal::packed_float3 m_sun_direction;
    float                m_fog_density;
    metal::float4        m_fog_color;
    metal::packed_float3 m_skytop_color;
    int                  m_light_count;
    GELightData          m_lights[GE_MTL_MAX_LIGHT];
};

#endif // GE_METAL_BINDINGS_H
