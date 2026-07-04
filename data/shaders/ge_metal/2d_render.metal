// 2d_render.metal - native-Metal port of
//   data/shaders/ge_shaders/2d_render.vert
//   data/shaders/ge_shaders/2d_render.frag
//
// The 2D (UI / text / HUD) pipeline. A faithful, numerically-identical port of
// the GLSL, and of the already-working inline MSL string in ge_metal_driver.mm
// (entry points ge2d_vertex / ge2d_fragment are kept identical so the driver can
// load this file instead of the inline literal with no other change).
//
// Behaviour of the GLSL, preserved exactly:
//   * Vertex positions arrive already in clip space (z = 0, w = 1); the vertex
//     stage does not transform them.
//   * Colour is irrlicht SColor (uchar4 in B,G,R,A memory order). GLSL does
//     `f_color = v_color.zyxw` to reorder to RGBA; MSL uses the same `.zyxw`.
//   * The flat integer sampler index selects which bindless texture to sample
//     when BIND_TEXTURES_AT_ONCE is defined; otherwise a single texture is used.
//   * Output colour = sampled texel * vertex colour.
//
// GLSL -> MSL mapping notes:
//   * layout(location=n) in/out       -> struct fields + [[attribute(n)]] /
//                                        [[position]] / interpolated members.
//   * flat int v_sampler_index/f_...   -> `int ... [[flat]]`.
//   * #ifdef BIND_TEXTURES_AT_ONCE     -> same guard; bindless path uses a Metal
//     `sampler2D f_tex[SAMPLER_SIZE]`     argument buffer of texture2d handles.
//   * GE_SAMPLE_TEX_INDEX(id)          -> plain int index. In the Vulkan path this
//     macro is either `nonuniformEXT` or `int` (see ge_vulkan_shader_manager.cpp);
//     both are pure index passthroughs. Metal argument-buffer indexing does not
//     need a divergence qualifier, so the value is used directly, identical math.
//
// Metal NDC z is [0,1]; the GLSL writes z = 0.0 which is inside [0,1] as well, so
// no z remap is needed for 2D (unlike the 3D path).

#include <metal_stdlib>
using namespace metal;

// The shared bindings header is included for consistency with the sibling
// *.metal files. The 2D pipeline uses its own low texture/sampler slots (0..)
// rather than the engine descriptor slots, matching the GLSL `binding = 0`.
#include "shared/ge_metal_bindings.h"

// SAMPLER_SIZE is injected by the shader-manager predefines on the bindless
// path (see GEVulkanShaderManager / the Metal shader manager), matching the
// Vulkan `#define SAMPLER_SIZE ...`. Provide a fallback so the file is still
// well-formed if the bindless path is ever compiled without it.
#ifdef BIND_TEXTURES_AT_ONCE
  #ifndef SAMPLER_SIZE
  #define SAMPLER_SIZE 256
  #endif
#endif

// ---------------------------------------------------------------------------
// Vertex stage (2d_render.vert)
//   location 0 vec2 v_position       -> pos   [[attribute(0)]]
//   location 1 vec4 v_color          -> color [[attribute(1)]]  (BGRA in memory)
//   location 2 vec2 v_uv             -> uv    [[attribute(2)]]
//   location 3 int  v_sampler_index  -> sampler_index [[attribute(3)]]
//
//   location 0 out vec4 f_color
//   location 1 out vec2 f_uv
//   location 2 flat out int f_sampler_index
// ---------------------------------------------------------------------------
struct VIn
{
    float2 pos           [[attribute(0)]];
    float4 color         [[attribute(1)]];
    float2 uv            [[attribute(2)]];
    int    sampler_index [[attribute(3)]];
};

struct VOut
{
    float4 position      [[position]];
    float4 color;
    float2 uv;
    int    sampler_index [[flat]];
};

vertex VOut ge2d_vertex(VIn in [[stage_in]])
{
    VOut o;
    // gl_Position = vec4(v_position, 0.0, 1.0);
    o.position = float4(in.pos, 0.0, 1.0);
    // f_color = v_color.zyxw;
    o.color = in.color.zyxw;
    // f_uv = v_uv;
    o.uv = in.uv;
    // f_sampler_index = v_sampler_index;
    o.sampler_index = in.sampler_index;
    return o;
}

// ---------------------------------------------------------------------------
// Fragment stage (2d_render.frag)
//
//   #ifdef BIND_TEXTURES_AT_ONCE
//       sampler2D f_tex[SAMPLER_SIZE];   // binding = 0
//       tex_color = texture(f_tex[GE_SAMPLE_TEX_INDEX(f_sampler_index)], f_uv);
//   #else
//       sampler2D f_tex;                 // binding = 0
//       tex_color = texture(f_tex, f_uv);
//   #endif
//       o_color = tex_color * f_color;
//
// Bindless path: the texture array is delivered as one argument buffer of
// texture2d handles plus a single sampler, mirroring sample_mesh_texture.h.
// Fallback path: a single texture + sampler at slot 0, identical to the
// already-working inline ge2d_fragment.
// ---------------------------------------------------------------------------

#ifdef BIND_TEXTURES_AT_ONCE

struct GE2DTextures
{
    array<texture2d<float>, SAMPLER_SIZE> f_tex [[id(0)]];
};

fragment float4 ge2d_fragment(VOut in [[stage_in]],
                              constant GE2DTextures& textures [[buffer(0)]],
                              sampler samp [[sampler(0)]])
{
    // GE_SAMPLE_TEX_INDEX(f_sampler_index) -> plain int index.
    int idx = in.sampler_index;
    float4 tex_color = textures.f_tex[idx].sample(samp, in.uv);
    // o_color = tex_color * f_color;
    return tex_color * in.color;
}

#else

fragment float4 ge2d_fragment(VOut in [[stage_in]],
                              texture2d<float> f_tex [[texture(0)]],
                              sampler samp [[sampler(0)]])
{
    float4 tex_color = f_tex.sample(samp, in.uv);
    // o_color = tex_color * f_color;
    return tex_color * in.color;
}

#endif // BIND_TEXTURES_AT_ONCE
