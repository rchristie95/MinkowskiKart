// depth_only.metal - native-Metal port of data/shaders/ge_shaders/depth_only.frag
//
// Purpose: depth prepass / shadow depth fragment.
//
// The GLSL source is an *empty* fragment shader:
//
//     void main()
//     {
//     }
//
// It declares no `in`/`out` locations and writes nothing. This is the pure
// depth-only pass: the fixed-function pipeline records depth from the rasterized
// primitive, the fragment stage produces no colour, and (unlike alphatest_depth)
// there is no texture fetch and no discard. The MSL port is therefore a faithful
// no-op fragment: it writes no colour attachment, so only the bound depth
// attachment is updated by the pipeline. Nothing here "improves" the GLSL — an
// empty shader ports to an empty shader.
//
// No descriptors are consumed by this stage, so no bindings from the shared
// header are needed. The paired vertex shader (which produces the depth/clip
// position) lives on the pipeline's vertex stage and applies the usual Metal
// [0,1] NDC-z remap baked into the projection; this fragment stage does not
// touch depth explicitly.
//
// The include of the shared bindings header is kept for consistency with the
// sibling ported shaders and so this translation unit compiles under the same
// predefine set; it has no effect on the empty body.

#include <metal_stdlib>
using namespace metal;

#include "shared/ge_metal_bindings.h"

// ---------------------------------------------------------------------------
// Faithful port of the empty GLSL `void main() {}`.
//
// A depth-only fragment writes no colour attachment. Declaring `void` as the
// return type (no [[color(n)]] output) is the MSL equivalent of the GLSL
// fragment that assigns nothing: the pipeline updates only the depth buffer.
// ---------------------------------------------------------------------------
fragment void depth_only_main()
{
}
