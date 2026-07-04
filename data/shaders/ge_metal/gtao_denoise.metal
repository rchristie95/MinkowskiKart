#include <metal_stdlib>
using namespace metal;

// ============================================================================
// GTAO denoise compute — native Metal translation unit.
//
// Faithful MSL port of:
//   data/shaders/ge_shaders/gtao_denoise.comp      (#define AO_IMAGE_FORMAT r16f)
//   data/shaders/ge_shaders/gtao_denoise_r32.comp  (#define AO_IMAGE_FORMAT r32f)
// both of which are thin wrappers around
//   data/shaders/ge_shaders/utils/gtao_denoise_impl.glsl
//
// Purpose: half-res, edge-aware 5x5 blur of the raw GTAO output. Each tap is
// weighted by depth similarity (exp(-|dz|*2.5)), normal similarity
// (pow(max(dot(n,n0),0), 8)) and a Gaussian spatial term (exp(-|o|^2*0.22));
// the AO term is bilinearly sampled from u_input while depth/normal taps are
// point-fetched. All constants and control flow are reproduced verbatim.
//
// The two GLSL variants differ ONLY by the AO_IMAGE_FORMAT image-format
// qualifier on the writeonly image (r16f vs r32f). Per the Metal porting rules,
// image-format qualifiers are dropped — the CPU picks the MTLPixelFormat when
// it creates the out0 render target — so a single kernel serves both. The r16f
// and r32f pipelines are built from this one entry point with different
// attachment formats.
//
// The full port (helpers + the `gtao_denoise_main` [[kernel]] with its
// [[buffer]]/[[texture]] bindings) lives in the shared impl header; this TU
// only needs to pull it in so the runtime `newLibraryWithSource:` compile
// emits the kernel function.
//
// Bindings (from shared/gtao_common.h, GLSL binding numbers kept verbatim):
//   u_normal        [[texture(GTAO_TEX_NORMAL=1)]]
//   u_linear_depth  [[texture(GTAO_TEX_LINEAR_DEPTH=2)]]
//   u_input         [[texture(GTAO_TEX_INPUT=3)]]  (bilinear-sampled)
//   u_out0 (write)  [[texture(GTAO_TEX_OUT0=5)]]
//   GTAOConstants   [[buffer(GTAO_BUF_CONSTANTS=GE_MTL_BUF_PUSH_CONSTANT=15)]]
// Dispatch mirrors GLSL local_size 8x8x1.
// ============================================================================

#include "shared/gtao_denoise_impl.h"
