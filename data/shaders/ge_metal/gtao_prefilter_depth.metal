#include <metal_stdlib>
using namespace metal;

// ============================================================================
// GTAO depth prefilter compute (top-level entry translation unit).
//
// Faithful MSL port of:
//   data/shaders/ge_shaders/gtao_prefilter_depth.comp      (#define AO_IMAGE_FORMAT r16f)
//   data/shaders/ge_shaders/gtao_prefilter_depth_r32.comp  (#define AO_IMAGE_FORMAT r32f)
// both of which #include utils/gtao_prefilter_depth_impl.glsl.
//
// Half-res linear-depth prefilter: for each half-res output texel, take the
// nearest (minimum view-space depth) of the corresponding full-res 2x2 block,
// skipping any texel on the far plane (raw device depth >= 1.0). If the whole
// block is on the far plane, write 0.0.
//
// The two GLSL .comp files differ only in the AO_IMAGE_FORMAT image-format
// qualifier (r16f vs r32f) on the writeonly output image. Per the Metal
// porting rules the image-format qualifier is dropped entirely -- the pixel
// format is a CPU-side MTLPixelFormat choice on the bound output texture -- so
// both GLSL variants collapse to the single kernel below. The CPU picks the
// r16Float or r32Float render target and binds it at GTAO_TEX_OUT0; the shader
// is identical for both.
//
// The actual kernel body + the numerically-identical reconstruction math live
// in the shared impl/common headers (ported by the sibling GTAO task). This
// file is the thin top-level compilation unit that instantiates the
// `gtao_prefilter_depth_main` [[kernel]] entry point so the Metal shader
// manager can build it via newLibraryWithSource:. Keeping the body in the
// shared header avoids duplicating the kernel across translation units.
//
// GLSL -> MSL builtin mapping (applied in the shared headers):
//   layout(local_size_x=8, local_size_y=8, local_size_z=1)
//       -> dispatched with an 8x8x1 threadgroup; the CPU dispatches
//          ceil(out_size / 8) threadgroups. gl_GlobalInvocationID.xy
//          -> [[thread_position_in_grid]] (uint2 gid).
//   layout(binding=0) sampler2D u_depth
//       -> texture2d<float> u_depth [[texture(GTAO_TEX_DEPTH)]]
//          (only texelFetch is used -> integer .read(uint2(px)), no sampler)
//   layout(binding=5, AO_IMAGE_FORMAT) writeonly image2D u_out0
//       -> texture2d<float, access::write> u_out0 [[texture(GTAO_TEX_OUT0)]]
//          (image-format qualifier dropped; MTLPixelFormat set CPU-side)
//   layout(std140, binding=7) uniform GTAOConstants u_pc
//       -> constant GTAOConstants& u_pc [[buffer(GTAO_BUF_CONSTANTS)]]
//   imageSize(u_out0)          -> int2(get_width(), get_height())
//   texelFetch(u_depth, px, 0) -> u_depth.read(uint2(px))
//   imageStore(u_out0, gid, v) -> u_out0.write(v, gid)
//
// Device-Z note: viewPosFromScreen() feeds the raw sampled device depth `z`
// straight into m_inverse_projection, exactly as the GLSL does. This is correct
// only if the CPU builds m_inverse_projection for the Metal [0,1] device-Z
// range (the Vulkan matrix already assumes [0,1]); see the concern flagged on
// the shared common header.
// ============================================================================

#include "shared/gtao_prefilter_depth_impl.h"

// The [[kernel]] gtao_prefilter_depth_main entry point is defined by the header
// above. No additional symbols are required in this translation unit.
