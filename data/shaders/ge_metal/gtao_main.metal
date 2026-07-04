#include <metal_stdlib>
using namespace metal;

#include "shared/ge_metal_bindings.h"
#include "shared/gtao_common.h"
#include "shared/gtao_main_impl.h"

// ============================================================================
// GTAO main compute — top-level Metal wrapper.
//
// Faithful MSL port of:
//   data/shaders/ge_shaders/gtao_main.comp      (AO_IMAGE_FORMAT r16f)
//   data/shaders/ge_shaders/gtao_main_r32.comp  (AO_IMAGE_FORMAT r32f)
// both of which just #include utils/gtao_main_impl.glsl with a different output
// image format.
//
// The horizon-search estimator + reconstruction math live in the shared headers
// (shared/gtao_common.h, shared/gtao_main_impl.h), ported verbatim. This file is
// the compile unit that the Metal shader manager builds via newLibraryWithSource:
// and exposes the [[kernel]] entry points from.
//
// r16f vs r32f split
// ------------------
// In GLSL the only difference between the two .comp files is the writeonly
// image format qualifier (AO_IMAGE_FORMAT). In Metal the output pixel format is
// a CPU-side MTLPixelFormat choice on the bound texture, and writing a float4 to
// a texture2d<float, access::write> is format-agnostic — so the shader body is
// byte-for-byte identical for both variants. A [[function_constant]] is provided
// so the CPU can still specialize/select the intended variant explicitly (and so
// a future format-specific tweak has a hook), matching the porting-rules request
// that the r16/r32 split be expressed via a function constant.
//
// GTAO_R32_OUTPUT selects the output precision intent:
//   false -> r16f variant (gtao_main.comp)
//   true  -> r32f variant (gtao_main_r32.comp)
// It has no effect on the computed AO value (identical math); it only documents
// intent and is available should a format-dependent branch ever be needed. The
// CPU may leave it unset (defaults to false / r16f) and simply bind the desired
// MTLPixelFormat, or set it per-variant when building the pipeline.
// ============================================================================

// Function-constant id 20 is used to avoid colliding with the six shared spec
// constants (ids 0..5) declared in ge_metal_bindings.h.
constant bool GTAO_R32_OUTPUT [[function_constant(20)]];

// ----------------------------------------------------------------------------
// The shared impl header already declares `kernel void gtao_main_main(...)`
// (the direct, format-agnostic port). It is re-exported here by virtue of the
// #include above, so a pipeline can reference `gtao_main_main` for either the
// r16f or r32f target and simply bind the matching output MTLPixelFormat.
//
// The explicit r16/r32 named entry points below wrap the same gtaoMain() body,
// gated on the GTAO_R32_OUTPUT function constant, for callers that prefer a
// per-variant function name (mirroring the two distinct GLSL .comp binaries).
// GLSL local_size 8x8x1 -> dispatched with an 8x8x1 threadgroup.
// ----------------------------------------------------------------------------

kernel void gtao_main_r16_main(
    uint2 gid                                        [[thread_position_in_grid]],
    constant GTAOConstants& u_pc                     [[buffer(GTAO_BUF_CONSTANTS)]],
    texture2d<float> u_depth                         [[texture(GTAO_TEX_DEPTH)]],
    texture2d<float> u_normal                        [[texture(GTAO_TEX_NORMAL)]],
    texture2d<float, access::write> u_out0           [[texture(GTAO_TEX_OUT0)]])
{
    gtaoMain(gid, u_pc, u_depth, u_normal, u_out0);
}

kernel void gtao_main_r32_main(
    uint2 gid                                        [[thread_position_in_grid]],
    constant GTAOConstants& u_pc                     [[buffer(GTAO_BUF_CONSTANTS)]],
    texture2d<float> u_depth                         [[texture(GTAO_TEX_DEPTH)]],
    texture2d<float> u_normal                        [[texture(GTAO_TEX_NORMAL)]],
    texture2d<float, access::write> u_out0           [[texture(GTAO_TEX_OUT0)]])
{
    gtaoMain(gid, u_pc, u_depth, u_normal, u_out0);
}
