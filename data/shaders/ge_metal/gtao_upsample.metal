#include <metal_stdlib>
using namespace metal;

// ============================================================================
// GTAO upsample compute — native Metal translation unit.
//
// Faithful MSL port of:
//   data/shaders/ge_shaders/gtao_upsample.comp      (#define AO_IMAGE_FORMAT r16f)
//   data/shaders/ge_shaders/gtao_upsample_r32.comp  (#define AO_IMAGE_FORMAT r32f)
// both of which are thin wrappers around
//   data/shaders/ge_shaders/utils/gtao_upsample_impl.glsl
//
// Purpose: full-res joint-bilateral upsample of the half-res GTAO output plus
// temporal reprojection against the previous frame's AO (u_history). This is the
// stage that carries the temporal-reprojection math:
//   currentAO()        : 3x3 joint-bilateral tap of the half-res u_input,
//                        weighted by depth (exp -3.0), normal (pow ^10) and a
//                        sub-texel Gaussian (exp -1.35*|frac_delta|^2); also
//                        returns the min/max AO of the neighbourhood for the
//                        history clamp.
//   reprojectHistory() : world = inverse_view * view_pos;
//                        prev_clip = previous_projection_view * world; reject if
//                        behind (w <= 1e-4) or off-screen; reproject depth
//                        (reject if |Δdepth| > max(0.18, depth*0.035)) and normal
//                        (reject if dot < 0.78).
//   main()             : far-plane pixels write 1.0; otherwise blend history
//                        (clamped to [min-0.04, max+0.04]) toward current by
//                        m_params0.z, gated off when m_params1.z (reset history)
//                        >= 0.5. Writes both u_out0 (result) and u_out1
//                        (next-frame history).
// All constants and control flow are reproduced verbatim.
//
// GLSL fetch semantics preserved (in the shared impl header):
//   - u_input        : texelFetch/point (.read) for the 3x3 taps
//   - u_history      : texture()/BILINEAR (.sample) at history_uv
//   - u_linear_depth : texelFetch/point (.read)
//   - u_depth/u_normal taps via viewPosFromScreen/viewNormalFromScreen (.read)
//   - textureSize(u_input,0) -> u_input.get_width()/get_height()
//   - imageSize(u_out0)      -> u_out0.get_width()/get_height()
//   - imageStore(u_out*, gid, v) -> u_out*.write(v, gid)
//   - lessThan/greaterThan   -> any(v < v)/any(v > v)
//
// The two GLSL variants differ ONLY by the AO_IMAGE_FORMAT image-format
// qualifier (r16f vs r32f) on the writeonly output images. Per the Metal
// porting rules the image-format qualifier is dropped — the CPU picks the
// MTLPixelFormat (r16Float or r32Float) when it creates the out0/out1 render
// targets and binds them — and writing a float4 to a
// texture2d<float, access::write> is format-agnostic, so a single kernel serves
// both variants. The r16f and r32f pipelines are built from this one entry point
// with different attachment formats.
//
// The full port (helpers + the `gtao_upsample_main` [[kernel]] with its
// [[buffer]]/[[texture]] bindings) lives in the shared impl header; this TU only
// needs to pull it in so the runtime `newLibraryWithSource:` compile emits the
// kernel function.
//
// Bindings (from shared/gtao_common.h, GLSL binding numbers kept verbatim):
//   u_depth         [[texture(GTAO_TEX_DEPTH=0)]]
//   u_normal        [[texture(GTAO_TEX_NORMAL=1)]]
//   u_linear_depth  [[texture(GTAO_TEX_LINEAR_DEPTH=2)]]
//   u_input         [[texture(GTAO_TEX_INPUT=3)]]
//   u_history       [[texture(GTAO_TEX_HISTORY=4)]]  (bilinear-sampled)
//   u_out0 (write)  [[texture(GTAO_TEX_OUT0=5)]]
//   u_out1 (write)  [[texture(GTAO_TEX_OUT1=6)]]
//   GTAOConstants   [[buffer(GTAO_BUF_CONSTANTS=GE_MTL_BUF_PUSH_CONSTANT=15)]]
// Dispatch mirrors GLSL local_size 8x8x1.
// ============================================================================

#include "shared/gtao_upsample_impl.h"

// The [[kernel]] gtao_upsample_main entry point is defined by the header above.
// No additional symbols are required in this translation unit.
