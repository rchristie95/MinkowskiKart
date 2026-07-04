// displace.metal — native-Metal port of the screen-space relativity FX pass.
//
// Ports (all three GLSL fragment shaders + their two shared utility headers):
//   * ge_shaders/displace_color.frag        -> fragment displace_color_main
//   * ge_shaders/displace_mask.frag         -> fragment displace_mask_main
//   * ge_shaders/displace_transparent.frag  -> fragment displace_transparent_main
//   * utils/displace_utils.frag             -> getDisplaceShift / getDisplaceUV
//   * utils/screen_space_reflection.frag    -> CalcCoordFromPosition / GetEdgeFade
//                                              / RayCast (+ the mask's HiZ trace)
//
// Purpose: the whole screen-space relativity look — black-hole / wormhole
// gravitational lensing, Calabi-Yau compactification warp, boost motion blur,
// FXAA + CAS, bloom, god rays / lens flare, per-object glow outlines,
// volumetric light scattering, Kerr accretion disks, gravitational-wave
// ripples, DoF, distance fog, vignette — plus the displace-material heat
// shimmer mask and its screen-space reflection buffer.
//
// This is a FAITHFUL, numerically-identical port of the GLSL. Every constant,
// clamp, epsilon, branch order and loop count is preserved verbatim; only the
// plumbing changes:
//
//   * GLSL global `u_camera` block        -> `constant CameraBuffer&` at
//                                            GE_MTL_BUF_CAMERA (relativity_bridge.h).
//   * GLSL global `u_global_light` block  -> `constant GEGlobalLightBuffer&` at
//                                            GE_MTL_BUF_GLOBAL_LIGHT.
//   * GLSL push_constant blocks           -> a `constant DisplaceColorPush&` /
//                                            `constant DisplacePush&` at
//                                            GE_MTL_BUF_PUSH_CONSTANT.
//   * per-pass combined-image-samplers    -> explicit texture2d<float> + sampler
//                                            params at the GLSL binding indices.
//   * gl_FragCoord                        -> [[position]] (window-space px, same
//                                            origin as gl_FragCoord).
//   * texture(t, uv)                      -> t.sample(s, uv)
//   * texelFetch(t, ivec2, 0)             -> t.read(uint2(...))
//   * textureSize / textureQueryLevels    -> get_width()/get_height()/get_num_mip_levels()
//   * sampler2DShadow + texture()         -> depth2d<float> + sample_compare()
//   * inversesqrt()                       -> rsqrt()
//   * GLSL constant_id specialisations    -> [[function_constant(n)]] (u_ssr,
//                                            u_hiz_iterations; see bindings header).
//
// The math for the relativistic sun aberration comes from the shared header
// common/relativity_visual.h (applyRelativisticVisualPosition), exactly as the
// GLSL used relativity_bridge.glsl + relativity_visual.vert.
//
// Metal NDC z is [0,1] like the Vulkan projection this pass was authored for, so
// the depth-buffer reconstruction paths are copied unchanged (the GLSL already
// assumed VULKAN [0,1] device Z in CalcCoordFromPosition and viewPosAt).

#include <metal_stdlib>
using namespace metal;

#include "shared/ge_metal_bindings.h"     // GE_MTL_BUF_* + GEGlobalLightBuffer + function constants
#include "shared/relativity_bridge.h"     // CameraBuffer + u_* field aliases
#include "common/relativity_visual.h"     // applyRelativisticVisualPosition
#include "common/sample_mesh_texture.h"   // sampleMeshTexture0 / sampleMeshTexture2
#ifdef PBR_ENABLED
#include "shared/handle_pbr.h"            // ge_convertColor (constants_utils.glsl convertColor)
#endif

// ===========================================================================
// utils/displace_utils.frag  ->  getDisplaceShift / getDisplaceUV
// ===========================================================================
// getDisplaceShift is pure math, unchanged. getDisplaceUV reads gl_FragCoord,
// so the fragment window-space coordinate is passed in as `frag_coord` and the
// displace-mask texture + sampler are passed explicitly.

inline float2 getDisplaceShift(float horiz, float vert)
{
    float2 offset = float2(horiz, vert);
    offset = 2.0 * offset - 1.0;

    float4 shiftval;
    shiftval.r = step(offset.x, 0.0) * -offset.x;
    shiftval.g = step(0.0, offset.x) * offset.x;
    shiftval.b = step(offset.y, 0.0) * -offset.y;
    shiftval.a = step(0.0, offset.y) * offset.y;

    float2 shift;
    shift.x = -shiftval.x + shiftval.y;
    shift.y = -shiftval.z + shiftval.w;
    return shift;
}

inline int2 getDisplaceUV(float2 shift, float4 viewport, float2 frag_coord,
                          texture2d<float> displace_mask, sampler displace_mask_s)
{
    int2 uv = int2(frag_coord);
    shift *= 0.02 * viewport.zw;
    int2 lo = int2(viewport.xy);
    int2 hi = int2(viewport.xy + viewport.zw);
    int2 suv = clamp(int2(frag_coord) + int2(shift), lo, hi);
    float2 new_mask = displace_mask.read(uint2(suv), 0).xy;
    if (!(new_mask.x == 0.0 && new_mask.y == 0.0))
        uv = suv;
    return uv;
}

// ===========================================================================
// utils/screen_space_reflection.frag  ->  CalcCoordFromPosition / GetEdgeFade
//                                          / RayCast
// ===========================================================================
// The GLSL took a `#if defined(VULKAN)` branch that skips the Z*0.5+0.5 remap
// (Vulkan's projection already emits Z in [0,1]). Native Metal shares that
// [0,1] device-Z convention, so the VULKAN branch is the one ported here.

inline float3 CalcCoordFromPosition(float3 pos, float4x4 projection_matrix,
                                    float2 viewport_scale, float2 viewport_offset)
{
    float4 projectedCoord = projection_matrix * float4(pos, 1.0);
    projectedCoord.xyz /= projectedCoord.w;
    // VULKAN / Metal: map X,Y from -1..+1 into 0..1; Z is already in [0,1].
    projectedCoord.xy = projectedCoord.xy * 0.5 + 0.5;
    // scale and offset by viewport
    projectedCoord.xy = projectedCoord.xy * viewport_scale + viewport_offset;
    return projectedCoord.xyz;
}

// Fade out edges of screen buffer tex. 1 = full render tex, 0 = full IBL tex.
inline float GetEdgeFade(float2 coords, float2 viewport_scale, float2 viewport_offset)
{
    float2 viewport_coords = (coords - viewport_offset) / viewport_scale;
    float gradL = smoothstep(0.0, 0.4, viewport_coords.x);
    float gradR = 1.0 - smoothstep(0.6, 1.0, viewport_coords.x);
    float gradT = smoothstep(0.0, 0.4, viewport_coords.y);
    float gradB = 1.0 - smoothstep(0.6, 1.0, viewport_coords.y);
    return min(min(gradL, gradR), min(gradT, gradB));
}

#ifndef GE_DISABLE_DISPLACE_SSR
// GLSL sampler2DShadow depth -> MSL depth2d<float> + a compare sampler. The
// caller supplies a sampler whose compare_func matches the GL/Vulkan LEQUAL
// depth-compare so texture(depth, projectedCoord) == sample_compare(coord.z).
inline float2 RayCast(float3 dir, float3 hitCoord, float4x4 projection_matrix,
                      float2 viewport_scale, float2 viewport_offset,
                      depth2d<float> depth, sampler depth_s)
{
    dir *= 0.5;
    hitCoord += dir;

    float3 projectedCoord = CalcCoordFromPosition(hitCoord, projection_matrix,
                            viewport_scale, viewport_offset);
    float factor = 1.0;

    for (int i = 0; i < 32; i++)
    {
        float direction = depth.sample_compare(depth_s, projectedCoord.xy,
                                               projectedCoord.z);
        factor *= direction;
        dir = dir * (0.5 + 0.5 * factor);
        hitCoord += dir * (2.0 * direction - 1.0);
        projectedCoord = CalcCoordFromPosition(hitCoord, projection_matrix,
                         viewport_scale, viewport_offset);
    }

    return projectedCoord.xy;
}
#endif // !GE_DISABLE_DISPLACE_SSR

// ===========================================================================
// Shared fragment I/O structs
// ===========================================================================

// displace_color.frag: layout(location 0) in vec2 f_uv.
struct DisplaceColorIn
{
    float4 gl_FragCoord [[position]];   // window-space xy (== gl_FragCoord)
    float2 f_uv         [[user(locn0)]];
};

// displace_mask.frag inputs:
//   location 1 f_uv, location 5 f_normal, location 8 f_world_position,
//   location 3 flat f_material_id.
struct DisplaceMaskIn
{
    float4 gl_FragCoord     [[position]];
    float2 f_uv             [[user(locn1)]];
    float3 f_normal         [[user(locn5)]];
    float4 f_world_position [[user(locn8)]];
    int    f_material_id    [[user(locn3)]] [[flat]];
};

// displace_transparent.frag inputs:
//   location 0 f_vertex_color, location 1 f_uv, location 3 flat f_material_id.
struct DisplaceTransparentIn
{
    float4 gl_FragCoord   [[position]];
    float4 f_vertex_color [[user(locn0)]];
    float2 f_uv           [[user(locn1)]];
    int    f_material_id  [[user(locn3)]] [[flat]];
};

// displace_color.frag: single colour attachment.
struct ColorOut
{
    float4 o_color [[color(0)]];
};

// displace_mask.frag: two attachments (mask.xy + ssr.xyzw).
struct MaskOut
{
    float2 o_displace_mask [[color(0)]];
    float4 o_displace_ssr  [[color(1)]];
};

// Push-constant block for displace_color.frag ( bool m_has_displace ).
struct DisplaceColorPush
{
    int m_has_displace;   // GLSL `bool`; std430 pushes a 4-byte scalar.
};

// Push-constant block for displace_mask / displace_transparent
// ( vec4 m_displace_direction ).
struct DisplacePush
{
    float4 m_displace_direction;
};

// ###########################################################################
// #  displace_color.frag                                                    #
// ###########################################################################
//
// This is a post-process fragment run over the full screen. All of its helper
// functions read `u_camera` / `u_global_light` and the per-pass textures, so —
// unlike GLSL free functions that could see the global blocks — every helper
// takes those as explicit parameters. The bodies are otherwise verbatim.

#ifdef PBR_ENABLED

namespace displace_color_ns
{

// The skybox is aberrated by the observer's relativistic motion, so the god-ray
// / lens-flare sun must be aberrated the same way to stay locked to the sky sun.
inline float3 relativisticSunWorldPos(thread const CameraBuffer& u_camera)
{
    return applyRelativisticVisualPosition(u_camera,
        float4(u_camera.m_godrays_pos.xyz, 1.0)).xyz;
}

inline float3 sampleScene(thread const CameraBuffer& u_camera,
                          texture2d<float> u_displace_color, sampler samp, float2 px)
{
    return u_displace_color.sample(samp, px / u_camera.m_screensize).rgb;
}

inline float sceneLuma(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// View-space position reconstruction from the depth buffer (irrlicht
// convention: +z forward).
inline float3 viewPosAt(thread const CameraBuffer& u_camera,
                        texture2d<float> u_depth, sampler depth_s, float2 px)
{
    float2 vp_xy = u_camera.m_viewport.xy;
    float2 vp_wh = u_camera.m_viewport.zw;
    float2 ndc = ((px - vp_xy) / vp_wh) * 2.0 - 1.0;
    float z = u_depth.sample(depth_s, px / u_camera.m_screensize).x;
    float4 clip = float4(ndc, z, 1.0);
    float4 view_pos = u_camera.m_inverse_projection_matrix * clip;
    return view_pos.xyz / view_pos.w;
}

// ---- Anti-aliasing (FXAA) ----
inline float3 antialiasScene(thread const CameraBuffer& u_camera,
                             texture2d<float> u_displace_color, sampler samp, float2 px)
{
    float3 rgbM  = sampleScene(u_camera, u_displace_color, samp, px);
    float3 rgbNW = sampleScene(u_camera, u_displace_color, samp, px + float2(-1.0, -1.0));
    float3 rgbNE = sampleScene(u_camera, u_displace_color, samp, px + float2( 1.0, -1.0));
    float3 rgbSW = sampleScene(u_camera, u_displace_color, samp, px + float2(-1.0,  1.0));
    float3 rgbSE = sampleScene(u_camera, u_displace_color, samp, px + float2( 1.0,  1.0));

    float lumaM  = sceneLuma(rgbM);
    float lumaNW = sceneLuma(rgbNW);
    float lumaNE = sceneLuma(rgbNE);
    float lumaSW = sceneLuma(rgbSW);
    float lumaSE = sceneLuma(rgbSE);

    float luma_min = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float luma_max = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
    if (luma_max - luma_min < max(0.0312, luma_max * 0.125))
        return rgbM;

    float2 dir = float2(-((lumaNW + lumaNE) - (lumaSW + lumaSE)),
                    ((lumaNW + lumaSW) - (lumaNE + lumaSE)));
    float dir_reduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.03125,
                           1.0 / 128.0);
    float rcp_dir_min = 1.0 / (min(abs(dir.x), abs(dir.y)) + dir_reduce);
    dir = clamp(dir * rcp_dir_min, float2(-8.0), float2(8.0));

    float3 rgbA = 0.5 * (sampleScene(u_camera, u_displace_color, samp, px + dir * (1.0 / 3.0 - 0.5)) +
                       sampleScene(u_camera, u_displace_color, samp, px + dir * (2.0 / 3.0 - 0.5)));
    float3 rgbB = rgbA * 0.5 + 0.25 * (sampleScene(u_camera, u_displace_color, samp, px + dir * -0.5) +
                                     sampleScene(u_camera, u_displace_color, samp, px + dir * 0.5));
    float lumaB = sceneLuma(rgbB);
    if (lumaB < luma_min || lumaB > luma_max)
        return rgbA;
    return rgbB;
}

// ---- Contrast-adaptive sharpening (AMD CAS style) ----
inline float3 casSharpen(thread const CameraBuffer& u_camera,
                         texture2d<float> u_displace_color, sampler samp,
                         float2 px, float3 center)
{
    float3 up    = sampleScene(u_camera, u_displace_color, samp, px + float2( 0.0, -1.0));
    float3 down  = sampleScene(u_camera, u_displace_color, samp, px + float2( 0.0,  1.0));
    float3 left  = sampleScene(u_camera, u_displace_color, samp, px + float2(-1.0,  0.0));
    float3 right = sampleScene(u_camera, u_displace_color, samp, px + float2( 1.0,  0.0));
    float l_up = sceneLuma(up), l_down = sceneLuma(down);
    float l_left = sceneLuma(left), l_right = sceneLuma(right);
    float l_c = sceneLuma(center);
    float mn = min(min(l_up, l_down), min(min(l_left, l_right), l_c));
    float mx = max(max(l_up, l_down), max(max(l_left, l_right), l_c));
    float amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, 1e-3), 0.0, 1.0));
    float w = amp * (-u_camera.m_beauty_params.w);
    float3 sharpened = (center + (up + down + left + right) * w) /
        (1.0 + 4.0 * w);
    float3 lo = min(min(up, down), min(min(left, right), center));
    float3 hi = max(max(up, down), max(max(left, right), center));
    return clamp(sharpened, lo, hi);
}

// ---- Bloom ----
inline float3 brightPass(float3 c)
{
    return c * smoothstep(0.90, 1.0, sceneLuma(c));
}

inline float3 bloomGather(thread const CameraBuffer& u_camera,
                          texture2d<float> u_displace_color, sampler samp, float2 px)
{
    float s = u_camera.m_viewport.w / 540.0;
    float3 accum = brightPass(sampleScene(u_camera, u_displace_color, samp, px)) * 0.5;
    const float2 DIRS[8] = {
        float2(1.0, 0.0), float2(0.7071, 0.7071), float2(0.0, 1.0),
        float2(-0.7071, 0.7071), float2(-1.0, 0.0), float2(-0.7071, -0.7071),
        float2(0.0, -1.0), float2(0.7071, -0.7071)};
    for (int i = 0; i < 8; i++)
    {
        accum += brightPass(sampleScene(u_camera, u_displace_color, samp, px + DIRS[i] * (4.0 * s))) * 0.25;
        accum += brightPass(sampleScene(u_camera, u_displace_color, samp, px + DIRS[i] * (9.0 * s))) * 0.125;
        accum += brightPass(sampleScene(u_camera, u_displace_color, samp, px + DIRS[i] * (16.0 * s))) * 0.0625;
    }
    return accum * (0.25 / 4.0);
}

// ---- Depth of field (dof.frag port) ----
inline float3 applyDOF(thread const CameraBuffer& u_camera,
                       texture2d<float> u_displace_color, sampler samp,
                       texture2d<float> u_depth, sampler depth_s,
                       float3 col_in, float2 px)
{
    const float FOCAL_DEPTH = 10.0;
    const float MAX_BLUR = 1.0;
    const float RANGE = 100.0;

    float depth = viewPosAt(u_camera, u_depth, depth_s, px).z;
    float blur = clamp(abs(depth - FOCAL_DEPTH) / RANGE, -MAX_BLUR, MAX_BLUR);

    float o = 10.0 * blur;
    float3 col = col_in;

    const float2 TAPS[16] = {
        float2(0.0, 0.4), float2(0.15, 0.37), float2(0.29, 0.29),
        float2(-0.37, 0.15), float2(0.4, 0.0), float2(0.37, -0.15),
        float2(0.29, -0.29), float2(-0.15, -0.37), float2(0.0, -0.4),
        float2(-0.15, 0.37), float2(-0.29, 0.29), float2(0.37, 0.15),
        float2(-0.4, 0.0), float2(-0.37, -0.15), float2(-0.29, -0.29),
        float2(0.15, -0.37)};
    for (int i = 0; i < 16; i++)
        col += sampleScene(u_camera, u_displace_color, samp, px + TAPS[i] * o);

    const float2 TAPS9[8] = {
        float2(0.15, 0.37), float2(-0.37, 0.15), float2(0.37, -0.15),
        float2(-0.15, -0.37), float2(-0.15, 0.37), float2(0.37, 0.15),
        float2(-0.37, -0.15), float2(0.15, -0.37)};
    for (int i = 0; i < 8; i++)
        col += sampleScene(u_camera, u_displace_color, samp, px + TAPS9[i] * (o * 0.9));

    const float2 TAPS7[8] = {
        float2(0.29, 0.29), float2(0.4, 0.0), float2(0.29, -0.29),
        float2(0.0, -0.4), float2(-0.29, 0.29), float2(-0.4, 0.0),
        float2(-0.29, -0.29), float2(0.0, 0.4)};
    for (int i = 0; i < 8; i++)
    {
        col += sampleScene(u_camera, u_displace_color, samp, px + TAPS7[i] * (o * 0.7));
        col += sampleScene(u_camera, u_displace_color, samp, px + TAPS7[i] * (o * 0.4));
    }

    col /= 41.0;
    float focus = clamp(max(1.1666 - (depth / 240.0), depth - 2000.0),
                        0.0, 1.0);
    return col_in * focus + col * (1.0 - focus);
}

// ---- Track god rays / light shafts ----
inline float3 godRays(thread const CameraBuffer& u_camera,
                      texture2d<float> u_depth, sampler depth_s, float2 px)
{
    float opacity = u_camera.m_godrays_pos.w;
    if (opacity <= 0.001)
        return float3(0.0);

    float3 sun_world = relativisticSunWorldPos(u_camera);
    float4 sun_clip = u_camera.m_projection_view_matrix *
        float4(sun_world, 1.0);
    if (sun_clip.w <= 0.001 || sun_clip.z <= 0.0)
        return float3(0.0);

    float2 vp_xy = u_camera.m_viewport.xy;
    float2 vp_wh = u_camera.m_viewport.zw;
    float2 sun_ndc = sun_clip.xy / sun_clip.w;
    float2 sun_screen = vp_xy + (sun_ndc * 0.5 + 0.5) * vp_wh;
    float sun_vz = (u_camera.m_view_matrix *
        float4(sun_world, 1.0)).z;
    float sun_margin = 3.0;

    float3 cam_right = float3(u_camera.m_view_matrix[0][0],
                          u_camera.m_view_matrix[1][0],
                          u_camera.m_view_matrix[2][0]);
    float4 rim_clip = u_camera.m_projection_view_matrix *
        float4(sun_world +
             cam_right * u_camera.m_godrays_color.w, 1.0);
    float2 rim_ndc = rim_clip.xy / max(rim_clip.w, 0.001);
    float2 rim_screen = vp_xy + (rim_ndc * 0.5 + 0.5) * vp_wh;
    float R_px = max(length(rim_screen - sun_screen), 4.0);

    float sun_vis = 0.0;
    float r_occ = 0.01 * vp_wh.y;
    for (int k = 0; k < 8; k++)
    {
        float a = float(k) * 0.7853981634;
        float2 t = clamp(sun_screen + float2(cos(a), sin(a)) * r_occ,
                       vp_xy, vp_xy + vp_wh);
        if (viewPosAt(u_camera, u_depth, depth_s, t).z >= sun_vz - sun_margin)
            sun_vis += 0.125;
    }
    if (sun_vis <= 0.001)
        return float3(0.0);

    float px_dist = length(px - sun_screen);
    if (px_dist > R_px * 14.0)
        return float3(0.0);

    const int N = 24;
    const float DECAY = 0.90;
    float2 step_px = (sun_screen - px) / (float(N) * 1.12);
    float2 cur = px;
    float decay = 1.0;
    float accum = 0.0;
    for (int i = 0; i < N; i++)
    {
        cur += step_px;
        float2 sample_px = clamp(cur, vp_xy, vp_xy + vp_wh);
        if (viewPosAt(u_camera, u_depth, depth_s, sample_px).z >= sun_vz - sun_margin)
        {
            float r = length(sample_px - sun_screen) / R_px;
            accum += exp(-r * r * 2.0) * decay;
        }
        decay *= DECAY;
    }

    return u_camera.m_godrays_color.rgb * (accum * 0.30 * opacity * sun_vis);
}

// ---- Sun lens flare ----
inline float3 lensFlare(thread const CameraBuffer& u_camera,
                        texture2d<float> u_depth, sampler depth_s, float2 px)
{
    float opacity = u_camera.m_godrays_pos.w;
    if (opacity <= 0.001)
        return float3(0.0);

    float3 sun_world = relativisticSunWorldPos(u_camera);
    float4 sun_clip = u_camera.m_projection_view_matrix *
        float4(sun_world, 1.0);
    if (sun_clip.w <= 0.001 || sun_clip.z <= 0.0)
        return float3(0.0);

    float2 vp_xy = u_camera.m_viewport.xy;
    float2 vp_wh = u_camera.m_viewport.zw;
    float2 sun_ndc = sun_clip.xy / sun_clip.w;
    if (abs(sun_ndc.x) > 1.1 || abs(sun_ndc.y) > 1.1)
        return float3(0.0);
    float2 sun_screen = vp_xy + (sun_ndc * 0.5 + 0.5) * vp_wh;

    float2 center = vp_xy + vp_wh * 0.5;
    float2 axis = center - sun_screen;
    float3 tint = normalize(u_camera.m_godrays_color.rgb + 0.24) * 1.45;
    float3 flare = float3(0.0);

    const float4 GHOSTS[5] = {
        float4(0.45, 0.055, 0.42, 0.0),
        float4(0.85, 0.032, 0.34, 0.5),
        float4(1.25, 0.085, 0.28, 0.2),
        float4(1.65, 0.044, 0.24, 0.8),
        float4(2.05, 0.120, 0.18, 0.4)};
    for (int i = 0; i < 5; i++)
    {
        float2 ghost_pos = sun_screen + axis * GHOSTS[i].x;
        float size = GHOSTS[i].y * vp_wh.y;
        float d = length(px - ghost_pos) / size;
        float shape = exp(-d * d * 1.8);
        float3 ghost_col = mix(tint, tint.bgr, GHOSTS[i].w);
        flare += ghost_col * (shape * GHOSTS[i].z);
    }

    float2 dp = px - sun_screen;
    float streak = exp(-pow(dp.y / (0.006 * vp_wh.y), 2.0)) *
        exp(-pow(dp.x / (0.22 * vp_wh.x), 2.0));
    flare += tint * (streak * 0.58);

    float edge_fade = (1.0 - smoothstep(0.85, 1.1, abs(sun_ndc.x))) *
        (1.0 - smoothstep(0.85, 1.1, abs(sun_ndc.y)));
    flare *= edge_fade * opacity * u_camera.m_postfx_flags2.z;

    if (dot(flare, float3(1.0)) < 0.002)
        return float3(0.0);

    float sun_vz = (u_camera.m_view_matrix *
        float4(sun_world, 1.0)).z;
    float sun_margin = 3.0;
    float vis = 0.0;
    float r_vis = 0.012 * vp_wh.y;
    const float2 VIS_TAPS[5] = {
        float2(0.0, 0.0), float2(1.0, 0.0), float2(-1.0, 0.0),
        float2(0.0, 1.0), float2(0.0, -1.0)};
    for (int i = 0; i < 5; i++)
    {
        float2 tap = clamp(sun_screen + VIS_TAPS[i] * r_vis,
            vp_xy, vp_xy + vp_wh);
        if (viewPosAt(u_camera, u_depth, depth_s, tap).z >= sun_vz - sun_margin)
            vis += 0.2;
    }

    return flare * vis;
}

// ---- Per-object glow outlines ----
inline float3 glowOutline(thread const CameraBuffer& u_camera,
                          texture2d<float> u_glow, sampler glow_s,
                          float3 col_in, float2 px)
{
    float2 guv = px / u_camera.m_screensize;
    float4 center = u_glow.sample(glow_s, guv);
    float s = u_camera.m_viewport.w / 540.0;
    const float2 DIRS[8] = {
        float2(1.0, 0.0), float2(0.7071, 0.7071), float2(0.0, 1.0),
        float2(-0.7071, 0.7071), float2(-1.0, 0.0), float2(-0.7071, -0.7071),
        float2(0.0, -1.0), float2(0.7071, -0.7071)};
    float4 blur = center * 0.25;
    float weight = 0.25;
    for (int i = 0; i < 8; i++)
    {
        blur += u_glow.sample(glow_s,
            (px + DIRS[i] * (4.0 * s)) / u_camera.m_screensize) * 0.125;
        blur += u_glow.sample(glow_s,
            (px + DIRS[i] * (9.0 * s)) / u_camera.m_screensize) * 0.0625;
        weight += 0.1875;
    }
    blur /= weight;
    if (blur.a < 0.004)
        return col_in;
    float3 glow_col = blur.rgb / max(blur.a, 0.001);
    float a = clamp(blur.a * 1.5, 0.0, 1.0) * 0.6 * (1.0 - center.a);
    return mix(col_in, min(glow_col * 2.0, float3(1.0)), a);
}

// ---- Volumetric light scattering (pointlightscatter.frag port) ----
inline float3 lightScatter(thread const CameraBuffer& u_camera,
                           constant GEGlobalLightBuffer& u_global_light,
                           texture2d<float> u_depth, sampler depth_s, float2 px)
{
    float density = u_camera.m_postfx_flags2.y;
    if (density <= 0.0001)
        return float3(0.0);

    float3 pixelpos = viewPosAt(u_camera, u_depth, depth_s, px);
    float pixel_len = length(pixelpos);
    if (pixel_len < 0.01)
        return float3(0.0);
    float3 eyedir = -normalize(pixelpos);

    float3 fog = float3(0.0);
    for (int i = 0; i < u_global_light.m_light_count; i++)
    {
        float3 light_pos = (u_camera.m_view_matrix *
            float4(u_global_light.m_lights[i].m_position_radius.xyz, 1.0)).xyz;
        float light_radius = u_global_light.m_lights[i].m_position_radius.w;
        float radius = max(light_radius * 2.0, light_radius + 4.0);
        float energy_scale = (light_radius * light_radius) /
            (radius * radius);
        float t_center = dot(-eyedir, light_pos);
        float t_far = min(t_center + radius, pixel_len);
        float t_near = t_center - radius;
        if (t_far <= max(t_near, 0.0))
            continue;
        float3 farthestpoint = -eyedir * t_far;
        float3 closestpoint = -eyedir * t_near;
        if (closestpoint.z < 1.0)
            closestpoint = float3(0.0);

        const int STEPS = 8;
        float stepsize = length(farthestpoint - closestpoint) / float(STEPS);
        float3 light_col =
            u_global_light.m_lights[i].m_color_inverse_square_range.xyz;
        float3 fog_factor = light_col * density * stepsize * 20.0 *
            energy_scale * 56.0;
        float3 xpos = farthestpoint;
        float3 xpos_step = eyedir * stepsize;

        float sscale =
            u_global_light.m_lights[i].m_direction_scale_offset.z;
        float3 sdir = float3(0.0);
        if (sscale != 0.0)
        {
            sdir = float3(
                u_global_light.m_lights[i].m_direction_scale_offset.xy, 0.0);
            sdir.z = sqrt(max(1.0 - dot(sdir, sdir), 0.0)) * sign(sscale);
            sdir = (u_camera.m_view_matrix * float4(sdir, 0.0)).xyz;
        }

        for (int j = 0; j < STEPS; j++)
        {
            float3 light_to_pos = light_pos - xpos;
            float d = length(light_to_pos);
            float l = float(STEPS - j) * stepsize;
            float3 base_att = fog_factor / (1.0 + d * d) *
                max((radius - d) / radius, 0.0) *
                exp(-density * d) * exp(-density * l);
            if (sscale != 0.0)
            {
                float offset =
                    u_global_light.m_lights[i].m_direction_scale_offset.w;
                float sattenuation = clamp(dot(-sdir,
                    normalize(light_to_pos)) * abs(sscale) + offset,
                    0.0, 1.0);
                base_att *= sattenuation * sattenuation;
            }
            fog += base_att;
            xpos += xpos_step;
        }
    }
    return fog;
}

// ---- Kerr black hole accretion helpers ----
inline float bhHash(float2 p)
{
    return fract(sin(dot(p, float2(41.31, 289.17))) * 43758.5453);
}
inline float bhNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(bhHash(i), bhHash(i + float2(1.0, 0.0)), f.x),
               mix(bhHash(i + float2(0.0, 1.0)), bhHash(i + float2(1.0, 1.0)),
               f.x), f.y);
}
inline float bhFbm(float2 p)
{
    float v = 0.0, amp = 0.5;
    for (int i = 0; i < 4; i++)
    {
        v += amp * bhNoise(p);
        p *= 2.02;
        amp *= 0.5;
    }
    return v;
}

inline float3 bhBlackbody(float t01)
{
    t01 = clamp(t01, 0.0, 1.0);
    float3 c0 = float3(0.75, 0.10, 0.02);   // coolest rim (deep red)
    float3 c1 = float3(1.20, 0.45, 0.10);   // orange
    float3 c2 = float3(1.35, 1.18, 0.88);   // white-hot
    float3 c3 = float3(1.05, 1.20, 1.55);   // blue-white inner edge
    if (t01 < 0.40)
        return mix(c0, c1, t01 / 0.40);
    if (t01 < 0.75)
        return mix(c1, c2, (t01 - 0.40) / 0.35);
    return mix(c2, c3, (t01 - 0.75) / 0.25);
}

inline float3 diskSample(thread const CameraBuffer& u_camera,
                         float a, float b, float doppler, thread float& bright)
{
    const float DISK_IN = 1.45;   // inner edge (~ISCO) in R_E units
    const float DISK_OUT = 3.6;   // nominal outer radius
    float rho = sqrt(a * a + b * b);
    bright = 0.0;
    if (rho < DISK_IN * 0.6 || rho > DISK_OUT * 2.4)
        return float3(0.0);
    float t = u_camera.m_postfx_flags2.w;
    float phi = atan2(b, a);

    float r_phys  = max(rho, DISK_IN);
    float omega   = 1.6 * pow(r_phys / DISK_IN, -1.5);
    float u = phi + t * omega;               // along-orbit coordinate
    float v = rho * 0.8 - t * 0.16;          // slow inward drift
    float2 tc = float2(cos(u), sin(u)) * (1.2 + rho * 0.5) + float2(0.0, v);
    float warp = bhFbm(tc * 1.1);
    float turb = bhFbm(tc * 2.0 + warp * 1.0);
    turb = mix(0.55, turb, 0.8);

    float inner = smoothstep(DISK_IN * 0.6, DISK_IN * 1.08, rho + turb * 0.45);
    float outer = exp(-pow(max(rho - DISK_IN, 0.0) / (DISK_OUT - DISK_IN),
                  1.25) * 1.35);
    float env = inner * outer;
    float prof = env * (0.7 + 1.0 * turb);

    float t_ss   = pow(r_phys / DISK_IN, -0.75) *
                   pow(max(1.0 - sqrt(DISK_IN / r_phys), 0.0), 0.25);
    float temp01 = clamp(t_ss * 1.35, 0.0, 1.0);

    float beta   = clamp(0.55 * sqrt(DISK_IN / r_phys), 0.0, 0.85);
    float gamma  = 1.0 / sqrt(max(1.0 - beta * beta, 1e-3));
    float D      = 1.0 / (gamma * (1.0 - beta * doppler));
    float g_grav = sqrt(max(1.0 - DISK_IN / (r_phys + DISK_IN * 0.5), 0.05));
    float shift  = D * g_grav;               // >1 boosted/blue, <1 dim/red

    float temp_obs = clamp(temp01 * (0.6 + 0.4 * shift), 0.0, 1.0);
    float3  col = bhBlackbody(temp_obs);
    col = mix(col, float3(1.4, 1.34, 1.25), clamp((turb - 0.6) * 1.5, 0.0, 0.6));

    float beam = clamp(pow(shift, 4.0), 0.3, 4.0);
    bright = max(prof, 0.0) * beam;
    return col;
}

inline float3 bhEmissionAt(thread const CameraBuffer& u_camera,
                           texture2d<float> u_depth, sampler depth_s, float2 px)
{
    float3 emission = float3(0.0);
    float2 vp_xy = u_camera.m_viewport.xy;
    float2 vp_wh = u_camera.m_viewport.zw;
    float3 cam_pos = u_camera.m_inverse_view_matrix[3].xyz;

    for (int i = 0; i < 4; i++)
    {
        float bh_r = u_camera.m_black_holes[i].w;
        if (bh_r <= 0.001)
            continue;
        float3 bh_pos = u_camera.m_black_holes[i].xyz;

        float4 bh_clip = u_camera.m_projection_view_matrix * float4(bh_pos, 1.0);
        if (bh_clip.w <= 0.001 || bh_clip.z <= 0.0)
            continue;
        float2 bh_screen = vp_xy + (bh_clip.xy / bh_clip.w * 0.5 + 0.5) * vp_wh;

        float3 cam_right = float3(u_camera.m_view_matrix[0][0],
                              u_camera.m_view_matrix[1][0],
                              u_camera.m_view_matrix[2][0]);
        float4 rim_clip = u_camera.m_projection_view_matrix *
            float4(bh_pos + cam_right * bh_r, 1.0);
        float2 rim_screen = vp_xy +
            (rim_clip.xy / max(rim_clip.w, 0.001) * 0.5 + 0.5) * vp_wh;
        float R_E = max(length(rim_screen - bh_screen), 2.0);

        float2 d = px - bh_screen;
        float rr = length(d) / R_E;
        if (rr > 9.0)
            continue;
        float close_fade = 1.0 - smoothstep(0.32, 0.5, R_E / vp_wh.y);
        if (close_fade <= 0.0)
            continue;

        float bh_vz = (u_camera.m_view_matrix * float4(bh_pos, 1.0)).z;
        bool is_sky = u_depth.sample(depth_s, px / u_camera.m_screensize).x >= 1.0;
        if (!is_sky && viewPosAt(u_camera, u_depth, depth_s, px).z < bh_vz - bh_r * 10.0)
            continue;

        float4 up_clip = u_camera.m_projection_view_matrix *
            float4(bh_pos + float3(0.0, bh_r, 0.0), 1.0);
        float2 up_screen = vp_xy +
            (up_clip.xy / max(up_clip.w, 0.001) * 0.5 + 0.5) * vp_wh;
        float2 minor = normalize(up_screen - bh_screen + float2(0.0, 1e-4));
        float2 major = float2(-minor.y, minor.x);

        float3 to_bh = normalize(bh_pos - cam_pos);
        float incl = clamp(abs(to_bh.y), 0.42, 0.85);

        float a = dot(d, major) / R_E;
        float b = dot(d, minor) / R_E;
        float3 bw = cross(float3(0.0, 1.0, 0.0), to_bh);
        float4 bw_clip = u_camera.m_projection_view_matrix *
            float4(bh_pos + bw * bh_r, 1.0);
        float2 bw_screen = vp_xy +
            (bw_clip.xy / max(bw_clip.w, 0.001) * 0.5 + 0.5) * vp_wh;
        float2 bright_dir = normalize(bw_screen - bh_screen + float2(1e-4, 0.0));
        float doppler = dot(d / max(length(d), 1e-3), bright_dir);
        float dwarm = clamp(doppler * 0.5 + 0.5, 0.0, 1.0);

        float3 bh_em = float3(0.0);

        float bd;
        float3 dcol = diskSample(u_camera, a, b / incl, doppler, bd);
        bh_em += dcol * bd;

        float vert = b / max(rr, 1e-3);            // +1 top, -1 bottom
        float arc_band = exp(-pow((rr - 1.22) * 3.0, 2.0));
        float top_arc = arc_band * smoothstep(0.0, 0.55, vert) * 1.35;
        float bot_arc = arc_band * smoothstep(0.1, 0.7, -vert) * 0.8;
        float3 arc_warm = mix(float3(1.15, 0.55, 0.18), float3(1.4, 1.3, 1.15),
            dwarm);
        bh_em += arc_warm * (top_arc + bot_arc) * mix(0.6, 1.7, dwarm);

        float theta_r = atan2(d.y, d.x);
        float mod_az  = 0.85 + 0.15 * cos(theta_r * 2.0);
        float ring = exp(-pow((rr - 1.04) * 9.0, 2.0));
        float3 ring_col = mix(float3(1.1, 0.8, 0.5), float3(1.45, 1.35, 1.15), dwarm);
        bh_em += ring_col * ring * mod_az * mix(0.8, 2.1, dwarm);

        emission += bh_em * close_fade;
    }
    return emission;
}

inline float2 bhScreenVelocity(thread const CameraBuffer& u_camera, float2 px)
{
    float2 vp_xy = u_camera.m_viewport.xy;
    float2 vp_wh = u_camera.m_viewport.zw;
    float best = 1e9;
    float2 vel = float2(0.0);
    for (int i = 0; i < 4; i++)
    {
        if (u_camera.m_black_holes[i].w <= 0.001)
            continue;
        float3 p = u_camera.m_black_holes[i].xyz;
        float4 cur_clip = u_camera.m_projection_view_matrix * float4(p, 1.0);
        if (cur_clip.w <= 0.001 || cur_clip.z <= 0.0)
            continue;
        float2 cur = vp_xy + (cur_clip.xy / cur_clip.w * 0.5 + 0.5) * vp_wh;
        float4 prev_clip = u_camera.m_previous_pv_matrix * float4(p, 1.0);
        if (prev_clip.w <= 0.001)
            continue;
        float2 prev = vp_xy +
            (prev_clip.xy / prev_clip.w * 0.5 + 0.5) * vp_wh;
        float dsc = length(px - cur);
        if (dsc < best)
        {
            best = dsc;
            vel = cur - prev;
        }
    }
    return vel;
}

inline float3 bhLensFlare(thread const CameraBuffer& u_camera,
                          texture2d<float> u_depth, sampler depth_s, float2 px)
{
    float knob = u_camera.m_postfx_flags2.z;
    if (knob <= 0.001)
        return float3(0.0);
    float2 vp_xy = u_camera.m_viewport.xy;
    float2 vp_wh = u_camera.m_viewport.zw;
    float2 center = vp_xy + vp_wh * 0.5;
    float3 flare = float3(0.0);

    for (int i = 0; i < 4; i++)
    {
        float bh_r = u_camera.m_black_holes[i].w;
        if (bh_r <= 0.001)
            continue;
        float3 bh_pos = u_camera.m_black_holes[i].xyz;
        float4 bh_clip = u_camera.m_projection_view_matrix * float4(bh_pos, 1.0);
        if (bh_clip.w <= 0.001 || bh_clip.z <= 0.0)
            continue;
        float2 bh_ndc = bh_clip.xy / bh_clip.w;
        if (abs(bh_ndc.x) > 1.2 || abs(bh_ndc.y) > 1.2)
            continue;
        float2 bh_screen = vp_xy + (bh_ndc * 0.5 + 0.5) * vp_wh;

        float bh_vz = (u_camera.m_view_matrix * float4(bh_pos, 1.0)).z;
        bool is_sky = u_depth.sample(depth_s,
            bh_screen / u_camera.m_screensize).x >= 1.0;
        if (!is_sky && viewPosAt(u_camera, u_depth, depth_s, bh_screen).z < bh_vz - bh_r * 10.0)
            continue;

        float4 rim_clip = u_camera.m_projection_view_matrix * float4(bh_pos +
            float3(u_camera.m_view_matrix[0][0], u_camera.m_view_matrix[1][0],
            u_camera.m_view_matrix[2][0]) * bh_r, 1.0);
        float2 rim_screen = vp_xy +
            (rim_clip.xy / max(rim_clip.w, 0.001) * 0.5 + 0.5) * vp_wh;
        float R_E = max(length(rim_screen - bh_screen), 2.0);
        float close_fade = 1.0 - smoothstep(0.30, 0.5, R_E / vp_wh.y);
        float edge = (1.0 - smoothstep(0.9, 1.2, abs(bh_ndc.x))) *
            (1.0 - smoothstep(0.9, 1.2, abs(bh_ndc.y)));
        float vis = close_fade * edge;
        if (vis <= 0.0)
            continue;

        float2 axis = center - bh_screen;
        float3 warm = float3(1.25, 0.7, 0.3);
        float3 f = float3(0.0);
        const float4 G[4] = {
            float4(0.30, 0.05, 0.18, 0.0),
            float4(0.62, 0.028, 0.14, 0.6),
            float4(1.15, 0.07, 0.10, 0.2),
            float4(1.55, 0.04, 0.07, 0.8)};
        for (int g = 0; g < 4; g++)
        {
            float2 gp = bh_screen + axis * G[g].x;
            float sz = G[g].y * vp_wh.y;
            float d = length(px - gp) / sz;
            float shape = exp(-d * d * 1.8);
            float3 gc = mix(warm, warm.bgr, G[g].w);
            f += gc * (shape * G[g].z);
        }
        float hd = length(px - bh_screen);
        f += warm * exp(-pow(hd / (0.045 * vp_wh.y), 2.0)) * 0.10;
        float2 dp = px - bh_screen;
        float streak = exp(-pow(dp.y / (0.004 * vp_wh.y), 2.0)) *
            exp(-pow(dp.x / (0.16 * vp_wh.x), 2.0));
        f += float3(1.3, 1.0, 0.7) * streak * 0.14;

        flare += f * vis;
    }
    return flare * knob;
}

inline float3 kerrAccretion(thread const CameraBuffer& u_camera,
                            texture2d<float> u_depth, sampler depth_s, float2 px)
{
    float3 em = bhEmissionAt(u_camera, u_depth, depth_s, px);
    float2 vel = bhScreenVelocity(u_camera, px);
    if (dot(vel, vel) > 0.25)
    {
        const int N = 5;
        const float TRAIL = 4.0;   // frames of smear
        for (int k = 1; k <= N; k++)
        {
            float t = float(k) / float(N);
            em += bhEmissionAt(u_camera, u_depth, depth_s, px + vel * (t * TRAIL)) * (0.3 * (1.0 - t));
        }
    }

    em += bhLensFlare(u_camera, u_depth, depth_s, px);

    return em / (1.0 + sceneLuma(em) * 0.4);
}

} // namespace displace_color_ns

// ---------------------------------------------------------------------------
// displace_color_main  (GLSL displace_color.frag main())
// ---------------------------------------------------------------------------
// Per-pass textures (GLSL binding indices verbatim):
//   binding 0 u_displace_mask, 2 u_displace_color, 3 u_depth, 4 u_glow.
// u_depth is a plain sampler2D here (NOT the shadow sampler of the mask pass).
fragment ColorOut displace_color_main(
    DisplaceColorIn in                       [[stage_in]],
    constant CameraBuffer&        u_camera        [[buffer(GE_MTL_BUF_CAMERA)]],
    constant GEGlobalLightBuffer& u_global_light  [[buffer(GE_MTL_BUF_GLOBAL_LIGHT)]],
    constant DisplaceColorPush&   u_push_constants [[buffer(GE_MTL_BUF_PUSH_CONSTANT)]],
    texture2d<float> u_displace_mask  [[texture(0)]],
    sampler          u_displace_mask_s [[sampler(0)]],
    texture2d<float> u_displace_color [[texture(2)]],
    sampler          u_displace_color_s [[sampler(2)]],
    texture2d<float> u_depth          [[texture(3)]],
    sampler          u_depth_s        [[sampler(3)]],
    texture2d<float> u_glow           [[texture(4)]],
    sampler          u_glow_s         [[sampler(4)]])
{
    using namespace displace_color_ns;

    ColorOut out;

    float2 vp_xy = u_camera.m_viewport.xy;
    float2 vp_wh = u_camera.m_viewport.zw;
    float2 frag_px = in.gl_FragCoord.xy;

    float2 src_px = frag_px;
    bool in_event_horizon = false;
    float distortion_strength = 0.0;
    float2 bh_chroma_dir = float2(0.0);
    float bh_chroma_amt = 0.0;

    // ---- Gravitational lensing from the active black holes ----
    for (int bh_i = 0; bh_i < 4; bh_i++)
    {
        if (in_event_horizon || u_camera.m_black_holes[bh_i].w <= 0.001)
            continue;
        float4 bh_clip = u_camera.m_projection_view_matrix *
            float4(u_camera.m_black_holes[bh_i].xyz, 1.0);
        if (bh_clip.w > 0.001 && bh_clip.z > 0.0)
        {
            float2 bh_ndc = bh_clip.xy / bh_clip.w;
            float2 bh_screen = vp_xy + (bh_ndc * 0.5 + 0.5) * vp_wh;

            float3 cam_right = float3(u_camera.m_view_matrix[0][0],
                                  u_camera.m_view_matrix[1][0],
                                  u_camera.m_view_matrix[2][0]);
            float4 rim_clip = u_camera.m_projection_view_matrix *
                float4(u_camera.m_black_holes[bh_i].xyz +
                cam_right * u_camera.m_black_holes[bh_i].w, 1.0);
            float2 rim_ndc = rim_clip.xy / max(rim_clip.w, 0.001);
            float2 rim_screen = vp_xy + (rim_ndc * 0.5 + 0.5) * vp_wh;
            float R_E = max(length(rim_screen - bh_screen), 2.0);
            const float BH_LENS_STRENGTH = 1.55;
            float R_L = R_E * BH_LENS_STRENGTH;

            float2 delta = src_px - bh_screen;
            float r = length(delta);

            if (r > 0.5 && r < R_L * 6.0)
            {
                float bh_vz = (u_camera.m_view_matrix *
                    float4(u_camera.m_black_holes[bh_i].xyz, 1.0)).z;
                if (viewPosAt(u_camera, u_depth, u_depth_s, frag_px).z >=
                    bh_vz - u_camera.m_black_holes[bh_i].w * 10.0)
                {
                    float drag = min(0.9 * (R_L * R_L) / (r * r), 0.9);
                    float cd = cos(drag), sd = sin(drag);
                    float2 dragged = float2(cd * delta.x - sd * delta.y,
                                        sd * delta.x + cd * delta.y);
                    if (r < R_E)
                    {
                        in_event_horizon = true;
                    }
                    else
                    {
                        float r_src = max(r - (R_L * R_L) / r, R_E * 0.05);
                        float2 deflected = bh_screen + (dragged / r) * r_src;
                        float fade = 1.0 - smoothstep(R_L * 2.5, R_L * 6.0, r);
                        src_px = mix(src_px, deflected, fade);
                        distortion_strength = max(distortion_strength,
                            clamp(1.0 - (r_src / (R_L * 2.0)), 0.0, 1.0) *
                            fade);
                        float ring_dist = abs(r - R_E) / R_E;
                        float ca = min(2.0 / max(ring_dist + 0.15, 0.15),
                            12.0) * fade;
                        if (ca > bh_chroma_amt)
                        {
                            bh_chroma_amt = ca;
                            bh_chroma_dir = dragged / r;
                        }
                    }
                    src_px = clamp(src_px, vp_xy, vp_xy + vp_wh);
                }
            }
        }
    }

    if (in_event_horizon)
    {
        out.o_color = float4(kerrAccretion(u_camera, u_depth, u_depth_s, frag_px), 1.0);
        return out;
    }

    // ---- Compactification: Calabi-Yau screen warp (banana debuff) ----
    float compact_strength = u_camera.m_compactification.x;
    if (compact_strength > 0.001)
    {
        const float strip_lo = 31.0 / 64.0;
        const float strip_hi = 33.0 / 64.0;
        float uv_y = (src_px.y - vp_xy.y) / vp_wh.y;
        float y_compacted = mix(strip_lo, strip_hi, uv_y);
        float y_sample = mix(uv_y, y_compacted, compact_strength);
        src_px.y = vp_xy.y + y_sample * vp_wh.y;
    }

    // ---- Displace (heat shimmer etc. from displace materials) ----
    if (u_push_constants.m_has_displace != 0)
    {
        float2 mask = u_displace_mask.read(uint2(int2(frag_px)), 0).xy;
        if (!(mask.x == 0.0 && mask.y == 0.0))
        {
            float2 shift = 2.0 * mask - 1.0;
            int2 displaced = getDisplaceUV(shift, u_camera.m_viewport, frag_px,
                u_displace_mask, u_displace_mask_s);
            src_px += float2(displaced) - frag_px;
        }
    }

    // Base scene sample with AA / SSAO / bloom, evaluated at the lens-warped
    // source position.
    float3 col;
    if (bh_chroma_amt > 0.01)
    {
        float2 ca = bh_chroma_dir * bh_chroma_amt;
        col.r = sampleScene(u_camera, u_displace_color, u_displace_color_s, clamp(src_px + ca, vp_xy, vp_xy + vp_wh)).r;
        col.g = sampleScene(u_camera, u_displace_color, u_displace_color_s, src_px).g;
        col.b = sampleScene(u_camera, u_displace_color, u_displace_color_s, clamp(src_px - ca, vp_xy, vp_xy + vp_wh)).b;
    }
    else
    {
        col = u_camera.m_postfx_flags.w > 0.5 ?
            antialiasScene(u_camera, u_displace_color, u_displace_color_s, src_px) :
            sampleScene(u_camera, u_displace_color, u_displace_color_s, src_px);
        if (u_camera.m_postfx_flags.w > 0.5)
            col = casSharpen(u_camera, u_displace_color, u_displace_color_s, src_px, col);
    }
    if (u_camera.m_postfx_flags.x > 0.5)
        col += bloomGather(u_camera, u_displace_color, u_displace_color_s, src_px);

    // ---- Track distance fog ----
    if (u_global_light.m_fog_density > 0.0001)
    {
        float fog_z = u_depth.sample(u_depth_s, src_px / u_camera.m_screensize).x;
        if (fog_z < 1.0)
        {
            float fog_dist = length(viewPosAt(u_camera, u_depth, u_depth_s, src_px));
            float fog_f = clamp(1.0 - exp(-u_global_light.m_fog_density *
                fog_dist), 0.0, 1.0);
            col = mix(col, u_global_light.m_fog_color.rgb, fog_f);
        }
    }

    col += godRays(u_camera, u_depth, u_depth_s, src_px);
    col += lensFlare(u_camera, u_depth, u_depth_s, src_px);
    if (u_camera.m_postfx_flags2.x > 0.5)
        col = glowOutline(u_camera, u_glow, u_glow_s, col, src_px);
    float3 scatter = lightScatter(u_camera, u_global_light, u_depth, u_depth_s, src_px);
    col += scatter / (1.0 + sceneLuma(scatter));

    // Darken the distorted region near the black hole.
    if (distortion_strength > 0.01)
        col *= (1.0 - distortion_strength * 0.4);

    col += kerrAccretion(u_camera, u_depth, u_depth_s, frag_px);

    // ---- Wormhole: Interstellar-style lensing mouth ----
    if (u_camera.m_wormhole.w > 0.01)
    {
        float4 wh_clip = u_camera.m_projection_view_matrix *
            float4(u_camera.m_wormhole.xyz, 1.0);
        if (wh_clip.w > 0.001 && wh_clip.z > 0.0)
        {
            float2 wh_ndc = wh_clip.xy / wh_clip.w;
            float2 wh_screen = vp_xy + (wh_ndc * 0.5 + 0.5) * vp_wh;
            float wh_depth01 = wh_clip.z / wh_clip.w;

            float3 cam_right = float3(u_camera.m_view_matrix[0][0],
                                  u_camera.m_view_matrix[1][0],
                                  u_camera.m_view_matrix[2][0]);
            float4 rim_clip = u_camera.m_projection_view_matrix *
                float4(u_camera.m_wormhole.xyz +
                cam_right * u_camera.m_wormhole.w, 1.0);
            float2 rim_ndc = rim_clip.xy / max(rim_clip.w, 0.001);
            float2 rim_screen = vp_xy + (rim_ndc * 0.5 + 0.5) * vp_wh;
            float R_S = max(length(rim_screen - wh_screen), 8.0);

            float2 delta = frag_px - wh_screen;
            float r = length(delta);
            float R_LENS_OUTER = R_S * 4.0;

            if (r > 0.1 && r < R_LENS_OUTER)
            {
                float wh_vz = (u_camera.m_view_matrix *
                    float4(u_camera.m_wormhole.xyz, 1.0)).z;
                bool wh_sky =
                    u_depth.sample(u_depth_s, frag_px / u_camera.m_screensize).x >= 1.0;
                if (wh_sky || viewPosAt(u_camera, u_depth, u_depth_s, frag_px).z >=
                    wh_vz - u_camera.m_wormhole.w * 4.0)
                {
                    float2 dir = delta / max(r, 0.001);
                    float swirl =
                        0.18 * exp(-pow((r - R_S) / (R_S * 1.2), 2.0));
                    float cs = cos(swirl), sn = sin(swirl);
                    float2 swirl_dir = float2(cs * dir.x - sn * dir.y,
                                          sn * dir.x + cs * dir.y);

                    if (r < R_S)
                    {
                        float rn = r / R_S;
                        float rn_fisheye = sin(rn * 1.5707963) * 0.98;
                        float2 inside_px =
                            wh_screen + swirl_dir * (rn_fisheye * R_S);
                        float ca = 0.012 * (rn * rn);
                        float2 px_r = wh_screen + swirl_dir *
                            (rn_fisheye * R_S * (1.0 + ca));
                        float2 px_b = wh_screen + swirl_dir *
                            (rn_fisheye * R_S * (1.0 - ca));
                        float3 portal_col = float3(
                            sampleScene(u_camera, u_displace_color, u_displace_color_s, clamp(px_r, vp_xy, vp_xy + vp_wh)).r,
                            sampleScene(u_camera, u_displace_color, u_displace_color_s, clamp(inside_px, vp_xy,
                                              vp_xy + vp_wh)).g,
                            sampleScene(u_camera, u_displace_color, u_displace_color_s, clamp(px_b, vp_xy,
                                              vp_xy + vp_wh)).b);
                        float rim_darken =
                            mix(1.0, 0.65, smoothstep(0.55, 1.0, rn));
                        col = portal_col * rim_darken;
                    }
                    else
                    {
                        float R_E = R_S * 1.05;
                        float r_src = max(r - (R_E * R_E) / r, R_S * 0.02);

                        float ring_dist = abs(r - R_S) / R_S;
                        float ca_out = 2.0 / max(ring_dist + 0.15, 0.15);
                        float2 base_pos = wh_screen + swirl_dir * r_src;
                        float3 lens_col = float3(
                            sampleScene(u_camera, u_displace_color, u_displace_color_s, clamp(base_pos + dir * ca_out,
                                vp_xy, vp_xy + vp_wh)).r,
                            sampleScene(u_camera, u_displace_color, u_displace_color_s, clamp(base_pos,
                                vp_xy, vp_xy + vp_wh)).g,
                            sampleScene(u_camera, u_displace_color, u_displace_color_s, clamp(base_pos - dir * ca_out,
                                vp_xy, vp_xy + vp_wh)).b);
                        float ring_blend =
                            1.0 - smoothstep(R_S, R_LENS_OUTER, r);
                        col = mix(col, lens_col, ring_blend);
                    }

                    float ring_sigma = R_S * 0.12;
                    float ring_n = (r - R_S) / ring_sigma;
                    float ring = exp(-ring_n * ring_n);
                    float theta = atan2(dir.y, dir.x);
                    float mod_az = 0.85 + 0.15 * cos(theta * 2.0);
                    float rim_boost = ring * mod_az * 0.45;
                    col = mix(col, col * 1.35, rim_boost);
                }
            }
        }
    }

    // ---- Depth of field (dof.frag port) ----
    if (u_camera.m_postfx_flags.z > 0.5)
        col = applyDOF(u_camera, u_displace_color, u_displace_color_s, u_depth, u_depth_s, col, frag_px);

    // ---- Boost motion blur (reprojection-based, motion_blur.frag) ----
    float boost_amount = u_camera.m_motion_blur.x;
    if (boost_amount > 0.001)
    {
        const int NB_SAMPLES = 8;
        float2 texcoords = (frag_px - vp_xy) / vp_wh;

        float z = u_depth.sample(u_depth_s, frag_px / u_camera.m_screensize).x;
        float2 ndc = texcoords * 2.0 - 1.0;
        float4 clip = float4(ndc, z, 1.0);
        float4 view_pos = u_camera.m_inverse_projection_matrix * clip;
        view_pos /= view_pos.w;
        float4 world_pos = u_camera.m_inverse_view_matrix * view_pos;
        float4 old_clip = u_camera.m_previous_pv_matrix * world_pos;
        old_clip /= old_clip.w;
        float2 old_texcoords = old_clip.xy * 0.5 + 0.5;

        float2 blur_dir = texcoords - old_texcoords;

        float2 center = u_camera.m_motion_blur.yz;
        float mask_radius = u_camera.m_motion_blur.w;
        float blur_factor =
            max(0.0, length(texcoords - center) - mask_radius);
        blur_factor *= boost_amount;
        blur_dir *= blur_factor;

        float2 inc = blur_dir / float(NB_SAMPLES);
        float2 blur_texcoords =
            texcoords - inc * float(NB_SAMPLES) / 2.0;
        for (int i = 1; i < NB_SAMPLES; i++)
        {
            float2 tap = clamp(blur_texcoords, float2(0.0), float2(1.0));
            col += sampleScene(u_camera, u_displace_color, u_displace_color_s, vp_xy + tap * vp_wh);
            blur_texcoords += inc;
        }
        col /= float(NB_SAMPLES);
    }

    // ---- Time-dilation gravitational wave (expanding ring) ----
    if (u_camera.m_grav_wave.w > 0.0)
    {
        float wave_r = u_camera.m_grav_wave.w;
        float zc = u_depth.sample(u_depth_s, frag_px / u_camera.m_screensize).x;
        if (zc < 1.0) // ripples live on the ground, not the sky
        {
            float2 tc = (frag_px - vp_xy) / vp_wh;
            float4 clip = float4(tc * 2.0 - 1.0, zc, 1.0);
            float4 vpos = u_camera.m_inverse_projection_matrix * clip;
            vpos /= vpos.w;
            float3 wpos = (u_camera.m_inverse_view_matrix * vpos).xyz;
            float d = distance(wpos, u_camera.m_grav_wave.xyz);

            if (d < wave_r + 3.0) // only inside the expanding front
            {
                const float K = 6.2831853 / 6.0;        // 6 m wavelength
                float phase = (d - wave_r) * K;          // crests trail the front
                float trail     = exp(-max(0.0, wave_r - d) / 22.0);
                float lead      = smoothstep(wave_r + 3.0, wave_r - 1.0, d);
                float edge_fade = clamp(1.0 - wave_r / 75.0, 0.0, 1.0);
                float in_fade   = smoothstep(0.0, 4.0, wave_r);
                float amp = trail * lead * edge_fade * in_fade;
                if (amp > 0.003)
                {
                    float slope = cos(phase) * amp;
                    float4 oc = u_camera.m_projection_view_matrix *
                              float4(u_camera.m_grav_wave.xyz, 1.0);
                    float2 rdir = float2(0.0, 1.0);
                    if (oc.w > 0.001)
                    {
                        float2 ondc = oc.xy / oc.w;
                        float2 osc = vp_xy + (ondc * 0.5 + 0.5) * vp_wh;
                        float2 v = frag_px - osc;
                        if (dot(v, v) > 1.0) rdir = normalize(v);
                    }
                    float disp = slope * 60.0;            // refraction (pixels)
                    float ca   = abs(slope) * 28.0;       // chromatic split
                    float2 base = frag_px + rdir * disp;
                    float3 refr = float3(
                        sampleScene(u_camera, u_displace_color, u_displace_color_s, clamp(base + rdir * ca,
                            vp_xy, vp_xy + vp_wh)).r,
                        sampleScene(u_camera, u_displace_color, u_displace_color_s, clamp(base,
                            vp_xy, vp_xy + vp_wh)).g,
                        sampleScene(u_camera, u_displace_color, u_displace_color_s, clamp(base - rdir * ca,
                            vp_xy, vp_xy + vp_wh)).b);
                    col = mix(col, refr, clamp(amp * 2.0, 0.0, 1.0));
                    float crest = max(0.0, sin(phase)) * amp;
                    col += float3(0.16, 0.28, 0.55) * (crest * 0.9);
                }
            }
        }
    }

    // ---- Vignette ----
    {
        float2 q = (frag_px - vp_xy) / vp_wh * 2.0 - 1.0;
        q.x *= 0.85;
        float r = length(q) * 0.7071;
        col *= 1.0 - u_camera.m_beauty_params.z * smoothstep(0.55, 1.0, r);
    }

    out.o_color = float4(col, 1.0);
    return out;
}

#endif // PBR_ENABLED (displace_color)

// ###########################################################################
// #  displace_mask.frag                                                     #
// ###########################################################################
//
// Writes the displacement mask (o_displace_mask) and, when u_ssr is set, the
// screen-space-reflection colour (o_displace_ssr). The SSR ray-march is guarded
// by GE_DISABLE_DISPLACE_SSR exactly like the GLSL (the depth + Hi-Z samplers
// exceed Metal's 16-sampler limit, so the CPU may drop them and fall back to the
// skybox reflection).

#ifdef PBR_ENABLED

#ifndef GE_DISABLE_DISPLACE_SSR
namespace displace_mask_ns
{
// Start tracing in this level.
constant int HIZ_START_LEVEL = 0;
// Stop tracing if current level is higher than this. (higher level = lower val)
constant int HIZ_STOP_LEVEL  = 0;
constant int HIZ_MAX_LEVEL   = 6;

// Set to 1 to disable HiZ and perform naive linear search.
#define DEBUG_LINEAR_SEARCH  0
constant float MAX_THICKNESS = 0.001;

inline float3 intersectDepthPlane(float3 o, float3 d, float z)
{
    return o + d * z;
}

// Index of the cell that contains the given 2D position.
inline int2 getCell(float2 screenUV, int2 cellCount)
{
    return int2(screenUV * float2(cellCount));
}

// Number of cells in the quad tree at the given level.
inline int2 getCellCount(texture2d<float> u_hiz_depth, int level)
{
    return int2(u_hiz_depth.get_width(uint(level)),
                u_hiz_depth.get_height(uint(level)));
}

// Screen-space position of the intersection between o + d*t and the closest
// cell boundary at the current HiZ level.
inline float3 intersectCellBoundary(
    float3 pos, float3 dir,
    int2 cell, int2 cellCount,
    float2 crossStep, float2 crossOffset)
{
    float3 intersection = float3(0.0);

    float2 index = float2(cell) + crossStep;
    float2 boundary = index / float2(cellCount);
    boundary += crossOffset;

    float2 delta = boundary - pos.xy;
    delta /= dir.xy;
    float t = min(delta.x, delta.y);

    intersection = intersectDepthPlane(pos, dir, t);
    return intersection;
}

inline bool crossedCellBoundary(int2 oldCellIx, int2 newCellIx)
{
    return any(oldCellIx != newCellIx);
}

// Minimum depth of the current cell in the current HiZ level.
inline float getMinDepthPlane(texture2d<float> u_hiz_depth, int2 cellIx, int level)
{
    return u_hiz_depth.read(uint2(cellIx), uint(level)).x;
}

inline float getMaxTraceDistance(float3 p, float3 v)
{
    float3 traceDistances;
    if (v.x < 0.0)
        traceDistances.x = p.x / (-v.x);
    else
        traceDistances.x = (1.0 - p.x) / v.x;

    if (v.y < 0.0)
        traceDistances.y = p.y / (-v.y);
    else
        traceDistances.y = (1.0 - p.y) / v.y;

    if (v.z < 0.0)
        traceDistances.z = p.z / (-v.z);
    else
        traceDistances.z = (1.0 - p.z) / v.z;

    return min(traceDistances.x, min(traceDistances.y, traceDistances.z));
}

// p          : Screen space position
// v          : Screen space reflection direction
// hitPointSS : Returns screen space hit point
// Returns    : Whether RT actually hit a surface
inline bool traceHiZ(thread const CameraBuffer& u_camera,
                     texture2d<float> u_hiz_depth,
                     float3 p, float3 v, thread float2& hitPointSS)
{
    const int maxLevel = min(HIZ_MAX_LEVEL, int(u_hiz_depth.get_num_mip_levels()) - 1);
    float maxTraceDistance = getMaxTraceDistance(p, v);

    float2 crossStep = float2(v.x >= 0 ? 1 : -1, v.y >= 0 ? 1 : -1);
    float2 crossOffset = crossStep / u_camera.m_viewport.zw / 128.0;
    crossStep = clamp(crossStep, 0.0, 1.0);

    float3 ray = p;
    float minZ = ray.z;
    float maxZ = ray.z + v.z * maxTraceDistance;
    float deltaZ = maxZ - minZ;

    float3 o = ray;
    float3 d = v * maxTraceDistance;

    int level = HIZ_START_LEVEL;
    int deepestLevel = level;
#if DEBUG_LINEAR_SEARCH
    level = 0;
#endif
    uint iterations = 0;
    bool isBackwardRay = v.z < 0;
    float rayDir = isBackwardRay ? -1.0 : 1.0;

    int2 startCellCount = getCellCount(u_hiz_depth, level);
    int2 rayCell = getCell(ray.xy, startCellCount);
    ray = intersectCellBoundary(o, d, rayCell, startCellCount, crossStep, crossOffset * 64.0);

    while (level >= HIZ_STOP_LEVEL && ray.z * rayDir <= maxZ * rayDir &&
        iterations < u_hiz_iterations)
    {
        int2 cellCount = getCellCount(u_hiz_depth, level);
        int2 oldCellIx = getCell(ray.xy, cellCount);

        float cellMinZ = getMinDepthPlane(u_hiz_depth, oldCellIx, level);

        float3 tempRay;
        if (cellMinZ > ray.z && !isBackwardRay)
            tempRay = intersectDepthPlane(o, d, (cellMinZ - minZ) / deltaZ);
        else
            tempRay = ray;

        int2 newCellIx = getCell(tempRay.xy, cellCount);
        float thickness = level == 0 ? (ray.z - cellMinZ) : 0;

        bool crossed = (isBackwardRay && (cellMinZ > ray.z))
                    || (thickness > MAX_THICKNESS) || crossedCellBoundary(oldCellIx, newCellIx);

        if (crossed)
        {
            ray = intersectCellBoundary(o, d, oldCellIx, cellCount, crossStep, crossOffset);
            level = min(maxLevel, level + 1);
            deepestLevel = max(deepestLevel, level);
#if DEBUG_LINEAR_SEARCH
            level = 0;
#endif
        }
        else
        {
            ray = tempRay;
            level = level - 1;
        }

        iterations += 1;
    }

    hitPointSS = ray.xy;
    return level < HIZ_STOP_LEVEL && iterations < u_hiz_iterations;
}
} // namespace displace_mask_ns
#endif // !GE_DISABLE_DISPLACE_SSR

// ---------------------------------------------------------------------------
// displace_mask_main  (GLSL displace_mask.frag main())
// ---------------------------------------------------------------------------
// Per-pass resources:
//   mesh textures (sampleMeshTexture0/2) at the low texture slots
//   set=2 binding=2 u_skybox_texture (cube) -> texturecube
//   set=3 binding=0 u_displace_color        -> texture2d
//   set=3 binding=1 u_depth (sampler2DShadow)-> depth2d + compare sampler  (SSR only)
//   set=3 binding=2 u_hiz_depth              -> texture2d                  (SSR only)
fragment MaskOut displace_mask_main(
    DisplaceMaskIn in                       [[stage_in]],
    constant CameraBuffer&      u_camera         [[buffer(GE_MTL_BUF_CAMERA)]],
    constant DisplacePush&      u_push_constants [[buffer(GE_MTL_BUF_PUSH_CONSTANT)]]
#ifdef BIND_MESH_TEXTURES_AT_ONCE
    ,
    constant GEMeshTextures&    mesh_textures    [[buffer(GE_MTL_BUF_MESH_TEXTURES)]],
    sampler                     mesh_sampler     [[sampler(GE_MTL_TEX_MESH0)]]
#else
    ,
    texture2d<float> f_mesh_texture_0 [[texture(GE_MTL_TEX_MESH0 + 0)]],
    sampler          f_mesh_sampler_0 [[sampler(GE_MTL_TEX_MESH0 + 0)]],
    texture2d<float> f_mesh_texture_2 [[texture(GE_MTL_TEX_MESH0 + 2)]],
    sampler          f_mesh_sampler_2 [[sampler(GE_MTL_TEX_MESH0 + 2)]]
#endif
    ,
    texturecube<float> u_skybox_texture  [[texture(13)]],
    sampler            u_skybox_sampler  [[sampler(13)]],
    texture2d<float>   u_displace_color  [[texture(15)]],
    sampler            u_displace_color_s [[sampler(15)]]
#ifndef GE_DISABLE_DISPLACE_SSR
    ,
    depth2d<float>   u_depth      [[texture(16)]],
    sampler          u_depth_s    [[sampler(16)]],
    texture2d<float> u_hiz_depth  [[texture(17)]]
#endif
    )
{
    MaskOut out;

    // horiz/vert use mesh texture layer 2 with the same UV math as GLSL.
#ifdef BIND_MESH_TEXTURES_AT_ONCE
    float horiz = sampleMeshTexture2(mesh_textures, mesh_sampler, in.f_material_id,
        in.f_uv + u_push_constants.m_displace_direction.xy * 150.0).x;
    float vert = sampleMeshTexture2(mesh_textures, mesh_sampler, in.f_material_id,
        (in.f_uv.yx + u_push_constants.m_displace_direction.zw * 150.0) * float2(0.9)).x;
#else
    float horiz = sampleMeshTexture2(f_mesh_texture_2, f_mesh_sampler_2, in.f_material_id,
        in.f_uv + u_push_constants.m_displace_direction.xy * 150.0).x;
    float vert = sampleMeshTexture2(f_mesh_texture_2, f_mesh_sampler_2, in.f_material_id,
        (in.f_uv.yx + u_push_constants.m_displace_direction.zw * 150.0) * float2(0.9)).x;
#endif
    float2 mask = getDisplaceShift(horiz, vert);
    mask = (mask + 1.0) * 0.5;
    out.o_displace_mask = mask;

    if (u_ssr)
    {
#ifdef BIND_MESH_TEXTURES_AT_ONCE
        float alpha = sampleMeshTexture0(mesh_textures, mesh_sampler, in.f_material_id, in.f_uv).a;
#else
        float alpha = sampleMeshTexture0(f_mesh_texture_0, f_mesh_sampler_0, in.f_material_id, in.f_uv).a;
#endif
        if (alpha == 0.0)
        {
            out.o_displace_ssr = float4(0.0);
            return out;
        }
        // eye-space position
        float3 xpos = (u_camera.m_view_matrix * in.f_world_position).xyz;
        // eye-space view direction (surface -> eye at origin)
        float3 eyedir = -normalize(xpos);
        // eye-space normal
        float3 normal = (u_camera.m_view_matrix * float4(normalize(in.f_normal), 0.0)).xyz;

        float NdotV = dot(normal, eyedir);
        if (NdotV <= 0.0)
        {
            out.o_displace_ssr = float4(0.0);
            return out;
        }

        float3 reflected = reflect(-eyedir, normal);
        float3 world_reflection = (u_camera.m_inverse_view_matrix *
            float4(reflected, 0.0)).xyz;

        // fallback to skybox
        float4 fallback = u_skybox_texture.sample(u_skybox_sampler, world_reflection);

        if (normal.z < -0.75)
        {
            out.o_displace_ssr = fallback;
            return out;
        }

#ifdef GE_DISABLE_DISPLACE_SSR
        // No depth / Hi-Z samplers available on Metal: skybox reflection only.
        out.o_displace_ssr = fallback;
        return out;
#else
        using namespace displace_mask_ns;

        float4 result;
        float2 viewport_scale = u_camera.m_viewport.zw / u_camera.m_screensize;
        float2 viewport_offset = u_camera.m_viewport.xy / u_camera.m_screensize;
        bool hit = true;
        float2 coords;
        if (u_hiz_iterations == 0)
        {
            coords = RayCast(reflected, xpos, u_camera.m_projection_matrix,
                viewport_scale, viewport_offset, u_depth, u_depth_s);
        }
        else
        {
            float3 positionSS = CalcCoordFromPosition(xpos,
                u_camera.m_projection_matrix, float2(1.0), float2(0.0));
            float3 positionCS = positionSS;
            positionCS.xy = 2.0 * positionCS.xy - 1.0;
            float3 position2VS = xpos + 1000.0 * reflected;
            float4 position2CS = u_camera.m_projection_matrix * float4(position2VS, 1.0);
            position2CS /= position2CS.w;
            float3 position2SS = position2CS.xyz;
            position2SS.xy = float2(0.5) + 0.5 * position2SS.xy;
            float3 reflectionDirSS = normalize(position2SS - positionSS);
            hit = traceHiZ(u_camera, u_hiz_depth, positionSS, reflectionDirSS, coords);
            coords = coords * viewport_scale + viewport_offset;
        }
        float2 viewport_coords = (coords - viewport_offset) / viewport_scale;
        if (!hit || viewport_coords.x < 0.0 || viewport_coords.x > 1.0 ||
            viewport_coords.y < 0.0 || viewport_coords.y > 1.0)
        {
            result = fallback;
        }
        else
        {
            result = u_displace_color.sample(u_displace_color_s, coords);
            float edge = GetEdgeFade(coords, viewport_scale, viewport_offset);
            float fresnel = (1.0 - NdotV) * (1.0 - NdotV);
            float blend_weight = edge * fresnel;
            result = mix(fallback, result, blend_weight);
        }
        out.o_displace_ssr = result;
#endif // GE_DISABLE_DISPLACE_SSR
    }

    return out;
}

#endif // PBR_ENABLED (displace_mask)

// ###########################################################################
// #  displace_transparent.frag                                              #
// ###########################################################################
//
// Displaced-material colour output with an optional screen-space-reflection
// blend (u_ssr). o_displace_ssr from the mask pass is sampled here via
// texelFetch at the displaced UV; the SSR sampler is dropped on the Metal
// 16-sampler workaround (GE_DISABLE_DISPLACE_SSR).

#ifdef PBR_ENABLED

fragment ColorOut displace_transparent_main(
    DisplaceTransparentIn in                 [[stage_in]],
    constant CameraBuffer&  u_camera         [[buffer(GE_MTL_BUF_CAMERA)]],
    constant DisplacePush&  u_push_constants [[buffer(GE_MTL_BUF_PUSH_CONSTANT)]]
#ifdef BIND_MESH_TEXTURES_AT_ONCE
    ,
    constant GEMeshTextures& mesh_textures   [[buffer(GE_MTL_BUF_MESH_TEXTURES)]],
    sampler                  mesh_sampler    [[sampler(GE_MTL_TEX_MESH0)]]
#else
    ,
    texture2d<float> f_mesh_texture_0 [[texture(GE_MTL_TEX_MESH0 + 0)]],
    sampler          f_mesh_sampler_0 [[sampler(GE_MTL_TEX_MESH0 + 0)]],
    texture2d<float> f_mesh_texture_2 [[texture(GE_MTL_TEX_MESH0 + 2)]],
    sampler          f_mesh_sampler_2 [[sampler(GE_MTL_TEX_MESH0 + 2)]]
#endif
    ,
    // set=3 binding=0 u_displace_mask (sampler2D; texelFetch only)
    texture2d<float> u_displace_mask  [[texture(15)]],
    sampler          u_displace_mask_s [[sampler(15)]]
#if !defined(GE_DISABLE_DISPLACE_SSR)
    ,
    // set=3 binding=1 u_displace_ssr (sampler2D; texelFetch only)
    texture2d<float> u_displace_ssr   [[texture(16)]],
    sampler          u_displace_ssr_s [[sampler(16)]]
#endif
    )
{
    ColorOut out;

#ifdef BIND_MESH_TEXTURES_AT_ONCE
    float4 color = sampleMeshTexture0(mesh_textures, mesh_sampler, in.f_material_id, in.f_uv) *
        in.f_vertex_color;
#else
    float4 color = sampleMeshTexture0(f_mesh_texture_0, f_mesh_sampler_0, in.f_material_id, in.f_uv) *
        in.f_vertex_color;
#endif
    float3 mixed_color = color.xyz;
    float alpha = color.w;
    mixed_color = ge_convertColor(mixed_color);
    if (u_ssr)
    {
#ifdef BIND_MESH_TEXTURES_AT_ONCE
        float alpha2 = sampleMeshTexture0(mesh_textures, mesh_sampler, in.f_material_id, in.f_uv).a;
#else
        float alpha2 = sampleMeshTexture0(f_mesh_texture_0, f_mesh_sampler_0, in.f_material_id, in.f_uv).a;
#endif
        if (alpha2 == 0.0)
        {
            out.o_color = float4(mixed_color * alpha2, alpha2);
            return out;
        }
#ifdef BIND_MESH_TEXTURES_AT_ONCE
        float horiz = sampleMeshTexture2(mesh_textures, mesh_sampler, in.f_material_id,
            in.f_uv + u_push_constants.m_displace_direction.xy * 150.0).x;
        float vert = sampleMeshTexture2(mesh_textures, mesh_sampler, in.f_material_id,
            (in.f_uv.yx + u_push_constants.m_displace_direction.zw * 150.0) * float2(0.9)).x;
#else
        float horiz = sampleMeshTexture2(f_mesh_texture_2, f_mesh_sampler_2, in.f_material_id,
            in.f_uv + u_push_constants.m_displace_direction.xy * 150.0).x;
        float vert = sampleMeshTexture2(f_mesh_texture_2, f_mesh_sampler_2, in.f_material_id,
            (in.f_uv.yx + u_push_constants.m_displace_direction.zw * 150.0) * float2(0.9)).x;
#endif
        float2 shift = getDisplaceShift(horiz, vert);
        int2 uv = getDisplaceUV(shift, u_camera.m_viewport, in.gl_FragCoord.xy,
            u_displace_mask, u_displace_mask_s);
        // NOTE: inside the u_ssr block the GLSL shadows the outer `alpha`
        // (color.w) with the inner `alpha2` (sampleMeshTexture0(...).a); every
        // alpha used below is the inner one, matching the GLSL exactly.
#if defined(GE_DISABLE_DISPLACE_SSR)
        // No SSR sampler (Metal sampler-limit workaround): displaced colour only.
        out.o_color = float4(mixed_color * alpha2, alpha2);
#else
        float3 reflection = u_displace_ssr.read(uint2(uv), 0).xyz;
        out.o_color = float4(mixed_color * alpha2 * 0.5 + reflection * alpha2 * 0.5,
            alpha2);
#endif
    }
    else
    {
        out.o_color = float4(mixed_color * alpha, alpha);
    }

    return out;
}

#endif // PBR_ENABLED (displace_transparent)
