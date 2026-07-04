// deferred_convert_color.metal - native-Metal port of
// data/shaders/ge_shaders/deferred_convert_color.frag
//
// Full-screen HDR resolve pass: reads the HDR accumulation target, applies the
// ACES-fitted filmic tonemap (Narkowicz approximation) with the exposure knob,
// a gentle S-curve + saturation grade, and finally the relativistic Doppler
// colour shift when relativistic visuals are active.
//
// Ported FAITHFULLY / numerically identically from the GLSL. The GLSL descriptor
// globals become explicit MSL parameters:
//
//   * layout(binding = 0) sampler2D u_hdr  (set = 0 per-pass HDR target)
//         -> texture2d<float> u_hdr [[texture(GE_MTL_TEX_MESH0)]]
//            It is read with texelFetch(..., ivec2(gl_FragCoord.xy), 0), so the
//            MSL uses u_hdr.read(uint2(...)) — an unfiltered integer-coordinate
//            fetch; no sampler is required.
//   * u_camera (set = 1, binding = 0 CameraBuffer)
//         -> constant CameraBuffer& u_camera [[buffer(GE_MTL_BUF_CAMERA)]]
//            (the relativity_bridge.h CameraBuffer, whose field aliases the
//             Doppler helper reads).
//   * gl_FragCoord -> the fragment [[position]] (window-space pixel coords, the
//     same origin/convention as gl_FragCoord). Its .xy is threaded into the
//     Doppler scanner helpers exactly as the GLSL passed gl_FragCoord.xy.
//
// The math (filmicToneMap, gradeColor, the NDC->world view-direction
// reconstruction, and applyDopplerShift) is copied verbatim; only the mechanical
// GLSL->MSL builtin substitutions are applied (clamp/mix/dot/normalize are
// spelled identically in MSL).

#include <metal_stdlib>
using namespace metal;

#include "shared/ge_metal_bindings.h"
// relativity_bridge.h defines the MSL CameraBuffer struct + the u_* field
// aliases (u_relativity_observer_pos, u_projection_view_matrix, u_screen, ...).
#include "shared/relativity_bridge.h"
// relativity_color.h provides applyDopplerShift(u_camera, frag_coord, color, dir).
#include "common/relativity_color.h"

// ---------------------------------------------------------------------------
// ACES-fitted filmic tonemap (Narkowicz approximation). The exposure knob
// comes from the settings (default 2.2 puts mid grey where the previous
// rational curve had it); the filmic shoulder rolls highlights off instead
// of clipping them.
// ---------------------------------------------------------------------------
static inline float3 filmicToneMap(constant CameraBuffer& u_camera, float3 x)
{
    x *= u_camera.m_beauty_params.x;
    return clamp((x * (2.51 * x + 0.03)) /
        (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

// ---------------------------------------------------------------------------
// Gentle grade: a touch of S-curve contrast plus the saturation knob.
// ---------------------------------------------------------------------------
static inline float3 gradeColor(constant CameraBuffer& u_camera, float3 c)
{
    c = mix(c, c * c * (3.0 - 2.0 * c), 0.15);
    float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
    return clamp(mix(float3(luma), c, u_camera.m_beauty_params.y), 0.0, 1.0);
}

// ---------------------------------------------------------------------------
// Fragment output.
//   location 0 o_color
// ---------------------------------------------------------------------------
struct FragOut
{
    float4 o_color [[color(0)]];
};

fragment FragOut deferred_convert_color_main(
    float4 frag_coord            [[position]],
    constant CameraBuffer& u_camera [[buffer(GE_MTL_BUF_CAMERA)]],
    texture2d<float> u_hdr          [[texture(GE_MTL_TEX_MESH0)]])
{
    FragOut out;

    // vec3 hdr = gradeColor(filmicToneMap(
    //     texelFetch(u_hdr, ivec2(gl_FragCoord.xy), 0).xyz));
    float3 hdr_texel = u_hdr.read(uint2(int2(frag_coord.xy))).xyz;
    float3 hdr = gradeColor(u_camera,
                            filmicToneMap(u_camera, hdr_texel));

    // Apply Doppler colour shift when relativistic visuals are active.
    // Compute the world-space view direction for this pixel from NDC.
    float2 ndc = (frag_coord.xy / u_screen) * 2.0 - float2(1.0);
    float4 world_far = u_camera.m_inverse_projection_view_matrix *
                       float4(ndc, 1.0, 1.0);
    float3 view_dir = normalize(world_far.xyz / world_far.w
                                - u_relativity_observer_pos.xyz);

    out.o_color = float4(
        applyDopplerShift(u_camera, frag_coord.xy, hdr, view_dir), 1.0);

    return out;
}
