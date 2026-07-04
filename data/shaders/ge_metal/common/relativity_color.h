// relativity_color.h  (MSL port of data/shaders/utils/relativity_color.frag)
//
// Relativistic Doppler colour shift (OpenRelativity spectral model) plus the
// on-screen "scanner" instrument overlay, run in the fragment stage. Ported
// byte-for-byte from the GLSL: identical constants, curve math, NaN/inf guards
// and early-outs. Two mechanical MSL adaptations, neither changes numerics:
//
//   * gl_FragCoord.xy is not a global in MSL. The scanner helpers that need it
//     take a `float2 frag_coord` (the fragment [[position]].xy) as an explicit
//     argument. applyDopplerShift threads it through.
//   * The camera UBO is passed as `constant CameraBuffer& u_camera`; the
//     relativity_bridge.h aliases make the bodies read like the GLSL originals.
//
// Include order:
//     #include "../shared/relativity_bridge.h"   // CameraBuffer + u_* aliases
//     #include "../common/relativity_color.h"

#ifndef GE_METAL_RELATIVITY_COLOR_H
#define GE_METAL_RELATIVITY_COLOR_H

#include <metal_stdlib>
using namespace metal;

// ---------------------------------------------------------------------------
// Enable flags
// ---------------------------------------------------------------------------
inline bool relativityVisualsEnabled(constant CameraBuffer& u_camera)
{
    return u_relativity_params.x > 0.5;
}

inline bool dopplerVisualsEnabled(constant CameraBuffer& u_camera)
{
    return u_relativity_params.y > 0.5;
}

inline bool dopplerScannerEnabled(constant CameraBuffer& u_camera)
{
    return u_relativity_observer_pos.w > 0.5;
}

// Aspect-corrected distance from screen centre, in units where the scanner
// circle radii below are comparable across arbitrary resolutions.
// frag_coord is the fragment's window-space xy (MSL [[position]].xy), i.e.
// the equivalent of gl_FragCoord.xy in the GLSL original.
inline float dopplerScannerRadius(constant CameraBuffer& u_camera,
                                  float2 frag_coord)
{
    float2 uv = frag_coord / u_screen;
    float aspect = u_screen.x / max(u_screen.y, 1.0);
    float2 d = (uv - float2(0.5)) * float2(aspect, 1.0);
    return length(d);
}

// Scanner geometry. Kept in one place so tweaks stay consistent.
#define SCANNER_INNER_RADIUS 0.14
#define SCANNER_OUTER_RADIUS 0.18
#define SCANNER_RING_WIDTH   0.006
#define SCANNER_RING_COLOR   float3(0.55, 0.95, 1.0)

// Mask used to blend Doppler-shifted colour with the scanner interior:
// 1.0 outside the scanner ring (full Doppler), 0.0 inside the inner radius.
// Returns a constant 1.0 when scanner mode is off so the existing Doppler
// shift on other triggers (banana squash, attachments, plunger) is unaffected.
inline float dopplerScannerMask(constant CameraBuffer& u_camera,
                                float2 frag_coord)
{
    if (!dopplerScannerEnabled(u_camera)) return 1.0;
    return smoothstep(SCANNER_INNER_RADIUS, SCANNER_OUTER_RADIUS,
                      dopplerScannerRadius(u_camera, frag_coord));
}

// BT.709 luminance — the scanner interior is desaturated to a monochrome
// instrument readout instead of showing the raw scene colour.
inline float3 dopplerScannerMonochrome(float3 color)
{
    float y = dot(color, float3(0.2126, 0.7152, 0.0722));
    // Subtle green-tint bias for a CRT/oscilloscope feel without hiding detail.
    return y * float3(0.85, 1.0, 0.9);
}

// Bright ring drawn at the boundary of the scanner window to frame the view
// like instrumentation. Returns intensity in [0, 1] for additive mixing.
inline float dopplerScannerRing(constant CameraBuffer& u_camera,
                                float2 frag_coord)
{
    if (!dopplerScannerEnabled(u_camera)) return 0.0;
    float r = dopplerScannerRadius(u_camera, frag_coord);
    float center = 0.5 * (SCANNER_INNER_RADIUS + SCANNER_OUTER_RADIUS);
    float d = abs(r - center);
    return 1.0 - smoothstep(0.0, SCANNER_RING_WIDTH, d);
}

// OpenRelativity color shift constants
#define xla 0.39952807612909519
#define xlb 444.63156780935032
#define xlc 20.095464678736523

#define xha 1.1305579611401821
#define xhb 593.23109262398259
#define xhc 34.446036241271742

#define ya 1.0098874822455657
#define yb 556.03724875218927
#define yc 46.184868454550838

#define za 2.0648400466720593
#define zb 448.45126344558236
#define zc 22.357297606503543

#define IR_RANGE 400.0
#define IR_START 700.0
#define UV_RANGE 380.0
#define UV_START 0.0

inline float3 RGBToXYZC(float r, float g, float b)
{
    float3 xyz;
    xyz.x = 0.13514*r + 0.120432*g + 0.057128*b;
    xyz.y = 0.0668999*r + 0.232706*g + 0.0293946*b;
    xyz.z = 0.0*r + 0.0000218959*g + 0.358278*b;
    return xyz;
}

inline float3 XYZToRGBC(float x, float y, float z)
{
    float3 rgb;
    rgb.x = 9.94845*x - 5.1485*y - 1.16389*z;
    rgb.y = -2.86007*x + 5.77745*y - 0.0179627*z;
    rgb.z = 0.000174791*x - 0.000353084*y + 2.79113*z;
    return rgb;
}

inline float3 weightFromXYZCurves(float3 xyz)
{
    float3 returnVal;
    returnVal.x = 0.0735806 * xyz.x - 0.0380793 * xyz.y - 0.00860837 * xyz.z;
    returnVal.y = -0.0665378 * xyz.x + 0.134408 * xyz.y - 0.000417865 * xyz.z;
    returnVal.z = 0.00000299624 * xyz.x - 0.00000605249 * xyz.y + 0.0484424 * xyz.z;
    return returnVal;
}

inline float getXFromCurve(float3 param, float shift)
{
    float top1 = param.x * xla * exp(-(pow((param.y*shift) - xlb, 2.0)/(2.0*(pow(param.z*shift, 2.0)+pow(xlc, 2.0))))) * sqrt(2.0*3.14159265358979323);
    float bottom1 = sqrt(1.0/pow(param.z*shift, 2.0) + 1.0/pow(xlc, 2.0));

    float top2 = param.x * xha * exp(-(pow((param.y*shift) - xhb, 2.0)/(2.0*(pow(param.z*shift, 2.0)+pow(xhc, 2.0))))) * sqrt(2.0*3.14159265358979323);
    float bottom2 = sqrt(1.0/pow(param.z*shift, 2.0) + 1.0/pow(xhc, 2.0));

    return (top1/bottom1) + (top2/bottom2);
}

inline float getYFromCurve(float3 param, float shift)
{
    float top = param.x * ya * exp(-(pow((param.y*shift) - yb, 2.0)/(2.0*(pow(param.z*shift, 2.0)+pow(yc, 2.0))))) * sqrt(2.0*3.14159265358979323);
    float bottom = sqrt(1.0/pow(param.z*shift, 2.0) + 1.0/pow(yc, 2.0));
    return top/bottom;
}

inline float getZFromCurve(float3 param, float shift)
{
    float top = param.x * za * exp(-(pow((param.y*shift) - zb, 2.0)/(2.0*(pow(param.z*shift, 2.0)+pow(zc, 2.0))))) * sqrt(2.0*3.14159265358979323);
    float bottom = sqrt(1.0/pow(param.z*shift, 2.0) + 1.0/pow(zc, 2.0));
    return top/bottom;
}

inline float3 constrainRGB(float r, float g, float b)
{
    float w;
    w = (0.0 < r) ? 0.0 : r;
    w = (w < g) ? w : g;
    w = (w < b) ? w : b;
    w = -w;

    if (w > 0.0) {
        r += w;  g += w; b += w;
    }
    w = r;
    w = (w < g) ? g : w;
    w = (w < b) ? b : w;

    if (w > 1.0)
    {
        r /= w;
        g /= w;
        b /= w;
    }
    return float3(r, g, b);
}

// Composite the scanner overlay (monochrome interior + bright framing ring)
// on top of `shifted` (Doppler-shifted colour) using `color` as the unshifted
// reference for the instrument readout. When scanner mode is off this is a
// straight pass-through of `shifted`, so every early-return below can just
// run its own result through this helper and still draw the ring / interior
// when the shift itself is a no-op (low speed, view-aligned, etc.).
inline float3 applyScannerOverlay(constant CameraBuffer& u_camera,
                                  float2 frag_coord, float3 shifted,
                                  float3 color)
{
    if (!dopplerScannerEnabled(u_camera)) return shifted;
    float3 interior = dopplerScannerMonochrome(color);
    float3 composed = mix(interior, shifted,
                          dopplerScannerMask(u_camera, frag_coord));
    return mix(composed, SCANNER_RING_COLOR,
               dopplerScannerRing(u_camera, frag_coord));
}

inline float3 applyDopplerShift(constant CameraBuffer& u_camera,
                                float2 frag_coord, float3 color,
                                float3 view_dir)
{
    if (!dopplerVisualsEnabled(u_camera)) return color;

    float3 beta = u_relativity_beta.xyz;
    float beta2 = dot(beta, beta);
    if (beta2 < 1e-6 || beta2 >= 1.0)
        return applyScannerOverlay(u_camera, frag_coord, color, color);

    float gamma = clamp(u_relativity_params.z, 1.0, 100.0);
    float shift = gamma * (1.0 - dot(beta, view_dir));

    if (shift < 0.01 || shift > 100.0)
        return applyScannerOverlay(u_camera, frag_coord, color, color);
    if (shift > 0.999 && shift < 1.001)
        return applyScannerOverlay(u_camera, frag_coord, color, color);

    float3 xyz = RGBToXYZC(color.r, color.g, color.b);
    if (any(isnan(xyz)) || any(isinf(xyz)))
        return applyScannerOverlay(u_camera, frag_coord, color, color);

    float3 weights = weightFromXYZCurves(xyz);
    float3 rParam = float3(weights.x, 615.0, 8.0);
    float3 gParam = float3(weights.y, 550.0, 4.0);
    float3 bParam = float3(weights.z, 463.0, 5.0);
    float3 UVParam = float3(0.02, UV_START + UV_RANGE*0.0, 5.0);
    float3 IRParam = float3(0.02, IR_START + IR_RANGE*0.0, 5.0);

    float invShift = 1.0 / shift;
    float shift3 = invShift * invShift * invShift;

    float xf = shift3 * (getXFromCurve(rParam, shift) + getXFromCurve(gParam, shift) + getXFromCurve(bParam, shift) + getXFromCurve(IRParam, shift) + getXFromCurve(UVParam, shift));
    float yf = shift3 * (getYFromCurve(rParam, shift) + getYFromCurve(gParam, shift) + getYFromCurve(bParam, shift) + getYFromCurve(IRParam, shift) + getYFromCurve(UVParam, shift));
    float zf = shift3 * (getZFromCurve(rParam, shift) + getZFromCurve(gParam, shift) + getZFromCurve(bParam, shift) + getZFromCurve(IRParam, shift) + getZFromCurve(UVParam, shift));

    if (isnan(xf) || isnan(yf) || isnan(zf) || isinf(xf) || isinf(yf) || isinf(zf))
        return applyScannerOverlay(u_camera, frag_coord, color, color);

    float3 rgbFinal = XYZToRGBC(xf, yf, zf);
    float3 shifted = constrainRGB(rgbFinal.x, rgbFinal.y, rgbFinal.z);
    return applyScannerOverlay(u_camera, frag_coord, shifted, color);
}

#endif // GE_METAL_RELATIVITY_COLOR_H
