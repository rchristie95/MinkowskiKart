layout(binding = 0) uniform sampler2D u_displace_mask;
layout(binding = 2) uniform sampler2D u_displace_color;
layout(binding = 3) uniform sampler2D u_depth;
layout(binding = 4) uniform sampler2D u_glow;

layout(location = 0) in vec2 f_uv;

layout(location = 0) out vec4 o_color;

layout(push_constant) uniform Constants
{
    bool m_has_displace;
} u_push_constants;

#include "utils/camera.glsl"
#include "utils/global_light_data.glsl"
#include "utils/relativity_bridge.glsl"
#include "../utils/relativity_visual.vert"
#include "../utils/displace_utils.frag"

// The skybox is aberrated by the observer's relativistic motion
// (skybox.frag bends each view ray with transformObserverRayToWorldDirection),
// so the sun baked into the sky swings as the observer's beta changes. The
// god-ray / lens-flare sun, however, is projected straight from its world
// position - without that aberration it ignores the warp and slides around
// like a foreground object. Aberrate the sun position the exact same way the
// rest of the scene geometry is (same observer-frame mapping, same distance)
// so the glow stays locked onto the sky sun. A no-op when relativity visuals
// are disabled, so non-relativistic behaviour is unchanged.
vec3 relativisticSunWorldPos()
{
    return applyRelativisticVisualPosition(
        vec4(u_camera.m_godrays_pos.xyz, 1.0)).xyz;
}

// Screen-space post effects, ported from the SP/OpenGL pipeline so both
// renderers look identical:
// - black hole gravitational lensing  (tonemap.frag)
// - wormhole lensing + Einstein ring  (tonemap.frag)
// - compactification screen warp      (compactification.frag)
// - boost motion blur                 (motion_blur.frag)

vec3 sampleScene(vec2 px)
{
    return texture(u_displace_color, px / u_camera.m_screensize).rgb;
}

float sceneLuma(vec3 c)
{
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// View-space position reconstruction from the depth buffer (irrlicht
// convention: +z forward, like the SP/OpenGL post shaders).
vec3 viewPosAt(vec2 px)
{
    vec2 vp_xy = u_camera.m_viewport.xy;
    vec2 vp_wh = u_camera.m_viewport.zw;
    vec2 ndc = ((px - vp_xy) / vp_wh) * 2.0 - 1.0;
    float z = texture(u_depth, px / u_camera.m_screensize).x;
    vec4 clip = vec4(ndc, z, 1.0);
    vec4 view_pos = u_camera.m_inverse_projection_matrix * clip;
    return view_pos.xyz / view_pos.w;
}

// ---- Anti-aliasing (FXAA, standing in for the SP/OpenGL MLAA option) ----
vec3 antialiasScene(vec2 px)
{
    vec3 rgbM  = sampleScene(px);
    vec3 rgbNW = sampleScene(px + vec2(-1.0, -1.0));
    vec3 rgbNE = sampleScene(px + vec2( 1.0, -1.0));
    vec3 rgbSW = sampleScene(px + vec2(-1.0,  1.0));
    vec3 rgbSE = sampleScene(px + vec2( 1.0,  1.0));

    float lumaM  = sceneLuma(rgbM);
    float lumaNW = sceneLuma(rgbNW);
    float lumaNE = sceneLuma(rgbNE);
    float lumaSW = sceneLuma(rgbSW);
    float lumaSE = sceneLuma(rgbSE);

    float luma_min = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float luma_max = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
    if (luma_max - luma_min < max(0.0312, luma_max * 0.125))
        return rgbM;

    vec2 dir = vec2(-((lumaNW + lumaNE) - (lumaSW + lumaSE)),
                    ((lumaNW + lumaSW) - (lumaNE + lumaSE)));
    float dir_reduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.03125,
                           1.0 / 128.0);
    float rcp_dir_min = 1.0 / (min(abs(dir.x), abs(dir.y)) + dir_reduce);
    dir = clamp(dir * rcp_dir_min, vec2(-8.0), vec2(8.0));

    vec3 rgbA = 0.5 * (sampleScene(px + dir * (1.0 / 3.0 - 0.5)) +
                       sampleScene(px + dir * (2.0 / 3.0 - 0.5)));
    vec3 rgbB = rgbA * 0.5 + 0.25 * (sampleScene(px + dir * -0.5) +
                                     sampleScene(px + dir * 0.5));
    float lumaB = sceneLuma(rgbB);
    if (lumaB < luma_min || lumaB > luma_max)
        return rgbA;
    return rgbB;
}

// ---- Contrast-adaptive sharpening (AMD CAS style) ----
// Recovers the crispness FXAA blurs away; the adaptive weight backs off in
// already-contrasty regions so edges don't ring.
vec3 casSharpen(vec2 px, vec3 center)
{
    vec3 up    = sampleScene(px + vec2( 0.0, -1.0));
    vec3 down  = sampleScene(px + vec2( 0.0,  1.0));
    vec3 left  = sampleScene(px + vec2(-1.0,  0.0));
    vec3 right = sampleScene(px + vec2( 1.0,  0.0));
    // Scalar (luma-driven) weight: per-channel weights tint high-contrast
    // edges with rainbow speckles.
    float l_up = sceneLuma(up), l_down = sceneLuma(down);
    float l_left = sceneLuma(left), l_right = sceneLuma(right);
    float l_c = sceneLuma(center);
    float mn = min(min(l_up, l_down), min(min(l_left, l_right), l_c));
    float mx = max(max(l_up, l_down), max(max(l_left, l_right), l_c));
    float amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, 1e-3), 0.0, 1.0));
    // Sharpness knob from the settings (x0.01)
    float w = amp * (-u_camera.m_beauty_params.w);
    vec3 sharpened = (center + (up + down + left + right) * w) /
        (1.0 + 4.0 * w);
    // Anti-ringing: never exceed the local neighbourhood's range, so
    // aliased high-contrast edges don't sparkle.
    vec3 lo = min(min(up, down), min(min(left, right), center));
    vec3 hi = max(max(up, down), max(max(left, right), center));
    return clamp(sharpened, lo, hi);
}

// ---- Bloom ----
// Single-pass approximation of the SP/OpenGL bloom chain (bright-pass with a
// smoothstep threshold, gaussian pyramid at 512/256/128, weighted re-blend).
// Operates on the tonemapped output, so the threshold targets near-white
// pixels rather than the >1 HDR range the GL bright-pass uses.
vec3 brightPass(vec3 c)
{
    // The GL chain bright-passes HDR values > 1; on this tonemapped LDR
    // input only near-clipped pixels count as highlights. (An
    // inverse-tonemap bright-pass was tried and rejected: it explodes on
    // near-white texture detail and the sparse gather turns it into
    // sparkle.)
    return c * smoothstep(0.90, 1.0, sceneLuma(c));
}

vec3 bloomGather(vec2 px)
{
    // Pixel radii scaled to the viewport so the halo size tracks resolution
    // like the GL fixed-size 512/256/128 pyramid does.
    float s = u_camera.m_viewport.w / 540.0;
    vec3 accum = brightPass(sampleScene(px)) * 0.5;
    const vec2 DIRS[8] = vec2[](
        vec2(1.0, 0.0), vec2(0.7071, 0.7071), vec2(0.0, 1.0),
        vec2(-0.7071, 0.7071), vec2(-1.0, 0.0), vec2(-0.7071, -0.7071),
        vec2(0.0, -1.0), vec2(0.7071, -0.7071));
#ifdef TILED_GPU
    // Mobile TBDRs pay heavily for wide, full-resolution gathers. Four
    // evenly-spaced directions at two radii retain a soft halo while cutting
    // this effect from 25 scene reads to 9.
    for (int i = 0; i < 8; i += 2)
    {
        accum += brightPass(sampleScene(px + DIRS[i] * (5.0 * s))) * 0.25;
        accum += brightPass(sampleScene(px + DIRS[i] * (13.0 * s))) * 0.125;
    }
    // Total weight 0.5 + 4*(0.375) = 2.0
    return accum * (0.25 / 2.0);
#else
    for (int i = 0; i < 8; i++)
    {
        accum += brightPass(sampleScene(px + DIRS[i] * (4.0 * s))) * 0.25;
        accum += brightPass(sampleScene(px + DIRS[i] * (9.0 * s))) * 0.125;
        accum += brightPass(sampleScene(px + DIRS[i] * (16.0 * s))) * 0.0625;
    }
    // Total weight 0.5 + 8*(0.4375) = 4.0
    return accum * (0.25 / 4.0);
#endif
}

// ---- Depth of field ----
// Direct port of the SP/OpenGL dof.frag (41-tap bokeh disc, focal depth 10,
// range 100, blended back by the same depth ramp).
vec3 applyDOF(vec3 col_in, vec2 px)
{
    const float FOCAL_DEPTH = 10.0;
    const float MAX_BLUR = 1.0;
    const float RANGE = 100.0;

    float depth = viewPosAt(px).z;
    float blur = clamp(abs(depth - FOCAL_DEPTH) / RANGE, -MAX_BLUR, MAX_BLUR);
    float focus = clamp(max(1.1666 - (depth / 240.0), depth - 2000.0),
                        0.0, 1.0);

    // Most road and kart pixels are close enough to the focal region that
    // the expensive bokeh gather would be blended away. Avoid paying for it.
    if (focus >= 0.995 || abs(blur) < 0.002)
        return col_in;

    // GL taps at uv + dir * (10 / screen) * blur, i.e. dir * 10 * blur px.
    float o = 10.0 * blur;
    vec3 col = col_in;

    const vec2 TAPS[16] = vec2[](
        vec2(0.0, 0.4), vec2(0.15, 0.37), vec2(0.29, 0.29),
        vec2(-0.37, 0.15), vec2(0.4, 0.0), vec2(0.37, -0.15),
        vec2(0.29, -0.29), vec2(-0.15, -0.37), vec2(0.0, -0.4),
        vec2(-0.15, 0.37), vec2(-0.29, 0.29), vec2(0.37, 0.15),
        vec2(-0.4, 0.0), vec2(-0.37, -0.15), vec2(-0.29, -0.29),
        vec2(0.15, -0.37));
#ifdef TILED_GPU
    // An 8-tap disc is a good compromise at the reduced mobile render scale.
    // It preserves visible defocus without the desktop path's 40 extra reads.
    for (int i = 0; i < 16; i += 2)
        col += sampleScene(px + TAPS[i] * o);
    col /= 9.0;
#else
    for (int i = 0; i < 16; i++)
        col += sampleScene(px + TAPS[i] * o);

    const vec2 TAPS9[8] = vec2[](
        vec2(0.15, 0.37), vec2(-0.37, 0.15), vec2(0.37, -0.15),
        vec2(-0.15, -0.37), vec2(-0.15, 0.37), vec2(0.37, 0.15),
        vec2(-0.37, -0.15), vec2(0.15, -0.37));
    for (int i = 0; i < 8; i++)
        col += sampleScene(px + TAPS9[i] * (o * 0.9));

    const vec2 TAPS7[8] = vec2[](
        vec2(0.29, 0.29), vec2(0.4, 0.0), vec2(0.29, -0.29),
        vec2(0.0, -0.4), vec2(-0.29, 0.29), vec2(-0.4, 0.0),
        vec2(-0.29, -0.29), vec2(0.0, 0.4));
    for (int i = 0; i < 8; i++)
    {
        col += sampleScene(px + TAPS7[i] * (o * 0.7));
        col += sampleScene(px + TAPS7[i] * (o * 0.4));
    }

    col /= 41.0;
#endif
    return col_in * focus + col * (1.0 - focus);
}

// ---- Track god rays / light shafts ----
// Single-pass approximation of the SP/OpenGL PostProcessing::renderGodRays
// chain (sun interposer sphere -> godfade mask -> radial blur toward the sun
// -> additive blend at the track opacity). The source term is an analytic
// glow disc at the projected lightshaft position, depth-masked so scenery
// occludes the sun, marched radially with the same decay style as
// godray.frag. Computed at the (possibly black-hole-warped) sample position
// so gravitational lensing smears the sun glow exactly like it does on GL.
vec3 godRays(vec2 px)
{
    float opacity = u_camera.m_godrays_pos.w;
    if (opacity <= 0.001)
        return vec3(0.0);

    // Aberrate the sun like the sky so the shaft origin tracks the warp.
    vec3 sun_world = relativisticSunWorldPos();
    vec4 sun_clip = u_camera.m_projection_view_matrix *
        vec4(sun_world, 1.0);
    if (sun_clip.w <= 0.001 || sun_clip.z <= 0.0)
        return vec3(0.0);

    vec2 vp_xy = u_camera.m_viewport.xy;
    vec2 vp_wh = u_camera.m_viewport.zw;
    vec2 sun_ndc = sun_clip.xy / sun_clip.w;
    vec2 sun_screen = vp_xy + (sun_ndc * 0.5 + 0.5) * vp_wh;
    // Occlusion is tested in view space: raw depth01 is so non-linear that
    // any fixed epsilon lets distant walls pass (shafts leaked through
    // geometry). A tight linear-depth margin keeps the test sharp; the old
    // ~20-unit interposer radius was so generous that track sitting between
    // the camera and the central sun failed to occlude, so the shafts bled
    // straight through it.
    float sun_vz = (u_camera.m_view_matrix *
        vec4(sun_world, 1.0)).z;
    float sun_margin = 3.0;

    // Project the interposer's world radius to pixels (robust to FOV/aspect).
    vec3 cam_right = vec3(u_camera.m_view_matrix[0][0],
                          u_camera.m_view_matrix[1][0],
                          u_camera.m_view_matrix[2][0]);
    vec4 rim_clip = u_camera.m_projection_view_matrix *
        vec4(sun_world +
             cam_right * u_camera.m_godrays_color.w, 1.0);
    vec2 rim_ndc = rim_clip.xy / max(rim_clip.w, 0.001);
    vec2 rim_screen = vp_xy + (rim_ndc * 0.5 + 0.5) * vp_wh;
    float R_px = max(length(rim_screen - sun_screen), 4.0);

    // Hard occlusion gate: probe a small disc at the sun's (aberrated) screen
    // centre. When scene geometry sits in front of it - e.g. the track band
    // between the camera and the central sun - the sun is hidden, so suppress
    // the shafts entirely instead of letting the screen-space march leak them
    // around the occluder and bleed through the track.
    float sun_vis = 0.0;
    float r_occ = 0.01 * vp_wh.y;
#ifdef TILED_GPU
    const int OCCLUSION_TAPS = 4;
#else
    const int OCCLUSION_TAPS = 8;
#endif
    for (int k = 0; k < OCCLUSION_TAPS; k++)
    {
        float a = float(k) * (6.283185307 / float(OCCLUSION_TAPS));
        vec2 t = clamp(sun_screen + vec2(cos(a), sin(a)) * r_occ,
                       vp_xy, vp_xy + vp_wh);
        if (viewPosAt(t).z >= sun_vz - sun_margin)
            sun_vis += 1.0 / float(OCCLUSION_TAPS);
    }
    if (sun_vis <= 0.001)
        return vec3(0.0);

    // Skip pixels far outside the shaft range to keep the pass cheap.
    float px_dist = length(px - sun_screen);
    if (px_dist > R_px * 14.0)
        return vec3(0.0);

#ifdef TILED_GPU
    const int N = 12;
    // Preserve the desktop path's attenuation over the same ray distance when
    // taking half as many, twice-as-long steps: sqrt(0.90) per mobile step.
    const float DECAY = 0.9486832981;
#else
    const int N = 24;
    const float DECAY = 0.90;
#endif
    // Like godray.frag, march most of the way toward the sun.
    vec2 step_px = (sun_screen - px) / (float(N) * 1.12);
    vec2 cur = px;
    float decay = 1.0;
    float accum = 0.0;
    for (int i = 0; i < N; i++)
    {
        cur += step_px;
        vec2 sample_px = clamp(cur, vp_xy, vp_xy + vp_wh);
        // Scene in front of the sun (view space) blocks the shaft; sky
        // pixels unproject to the far plane and always pass.
        if (viewPosAt(sample_px).z >= sun_vz - sun_margin)
        {
            float r = length(sample_px - sun_screen) / R_px;
            accum += exp(-r * r * 2.0) * decay;
        }
        decay *= DECAY;
    }

    // Normalised march sum peaks around ~9 at the disc centre; the gain maps
    // that to roughly the additive brightness the GL chain produces.
    return u_camera.m_godrays_color.rgb * (accum * 0.30 * opacity * sun_vis);
}

// ---- Sun lens flare ----
// Ghost sprites along the sun -> screen-centre axis plus a faint anamorphic
// streak, faded by how much of the sun disc is actually visible (same
// view-space occlusion test as the god rays). Active under the same gate as
// the god rays (track lightshaft + Light shaft setting).
vec3 lensFlare(vec2 px)
{
    float opacity = u_camera.m_godrays_pos.w;
    if (opacity <= 0.001)
        return vec3(0.0);

    // Aberrate the sun like the sky so the flare tracks the warp (see godRays).
    vec3 sun_world = relativisticSunWorldPos();
    vec4 sun_clip = u_camera.m_projection_view_matrix *
        vec4(sun_world, 1.0);
    if (sun_clip.w <= 0.001 || sun_clip.z <= 0.0)
        return vec3(0.0);

    vec2 vp_xy = u_camera.m_viewport.xy;
    vec2 vp_wh = u_camera.m_viewport.zw;
    vec2 sun_ndc = sun_clip.xy / sun_clip.w;
    if (abs(sun_ndc.x) > 1.1 || abs(sun_ndc.y) > 1.1)
        return vec3(0.0);
    vec2 sun_screen = vp_xy + (sun_ndc * 0.5 + 0.5) * vp_wh;

    vec2 center = vp_xy + vp_wh * 0.5;
    vec2 axis = center - sun_screen;
    vec3 tint = normalize(u_camera.m_godrays_color.rgb + 0.24) * 1.45;
    vec3 flare = vec3(0.0);

    // Ghost sprites mirrored along the axis
    const vec4 GHOSTS[5] = vec4[](
        // [position along axis, size in viewport heights, weight, hue mix]
        vec4(0.45, 0.055, 0.42, 0.0),
        vec4(0.85, 0.032, 0.34, 0.5),
        vec4(1.25, 0.085, 0.28, 0.2),
        vec4(1.65, 0.044, 0.24, 0.8),
        vec4(2.05, 0.120, 0.18, 0.4));
    for (int i = 0; i < 5; i++)
    {
        vec2 ghost_pos = sun_screen + axis * GHOSTS[i].x;
        float size = GHOSTS[i].y * vp_wh.y;
        float d = length(px - ghost_pos) / size;
        float shape = exp(-d * d * 1.8);
        // Alternate between the sun tint and a cooler complementary
        vec3 ghost_col = mix(tint, tint.bgr, GHOSTS[i].w);
        flare += ghost_col * (shape * GHOSTS[i].z);
    }

    // Faint anamorphic streak across the sun
    vec2 dp = px - sun_screen;
    float streak = exp(-pow(dp.y / (0.006 * vp_wh.y), 2.0)) *
        exp(-pow(dp.x / (0.22 * vp_wh.x), 2.0));
    flare += tint * (streak * 0.58);

    // Fade ghosts as the sun leaves the screen edge; overall strength comes
    // from the settings knob (m_postfx_flags2.z).
    float edge_fade = (1.0 - smoothstep(0.85, 1.1, abs(sun_ndc.x))) *
        (1.0 - smoothstep(0.85, 1.1, abs(sun_ndc.y)));
    flare *= edge_fade * opacity * u_camera.m_postfx_flags2.z;

    // Only pay for the occlusion taps where flare would actually show.
    if (dot(flare, vec3(1.0)) < 0.002)
        return vec3(0.0);

    // Soft visibility: fraction of taps around the sun centre that see
    // past-the-sun depth (same tight view-space margin as the god rays, so
    // the flare is killed when the track hides the sun).
    float sun_vz = (u_camera.m_view_matrix *
        vec4(sun_world, 1.0)).z;
    float sun_margin = 3.0;
    float vis = 0.0;
    float r_vis = 0.012 * vp_wh.y;
    const vec2 VIS_TAPS[5] = vec2[](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(-1.0, 0.0),
        vec2(0.0, 1.0), vec2(0.0, -1.0));
    for (int i = 0; i < 5; i++)
    {
        vec2 tap = clamp(sun_screen + VIS_TAPS[i] * r_vis,
            vp_xy, vp_xy + vp_wh);
        if (viewPosAt(tap).z >= sun_vz - sun_margin)
            vis += 0.2;
    }

    return flare * vis;
}

// ---- Per-object glow outlines ----
// Port of the SP/OpenGL glow chain: glowing objects were drawn flat-coloured
// into the glow attachment (mask pass); here the silhouettes are blurred and
// added as a halo outside the object (the GL pass excluded the interior via
// a stencil mask).
vec3 glowOutline(vec3 col_in, vec2 px)
{
    vec2 guv = px / u_camera.m_screensize;
    vec4 center = texture(u_glow, guv);
    // Pixel radii scaled to the viewport, like the GL half/quarter pyramid.
    float s = u_camera.m_viewport.w / 540.0;
    const vec2 DIRS[8] = vec2[](
        vec2(1.0, 0.0), vec2(0.7071, 0.7071), vec2(0.0, 1.0),
        vec2(-0.7071, 0.7071), vec2(-1.0, 0.0), vec2(-0.7071, -0.7071),
        vec2(0.0, -1.0), vec2(0.7071, -0.7071));
    vec4 blur = center * 0.25;
    float weight = 0.25;
#ifdef TILED_GPU
    // Nine reads instead of seventeen; the alternating directions still
    // cover the full circle and the two radii keep the outline broad.
    for (int i = 0; i < 8; i += 2)
#else
    for (int i = 0; i < 8; i++)
#endif
    {
        blur += texture(u_glow,
            (px + DIRS[i] * (4.0 * s)) / u_camera.m_screensize) * 0.125;
        blur += texture(u_glow,
            (px + DIRS[i] * (9.0 * s)) / u_camera.m_screensize) * 0.0625;
        weight += 0.1875;
    }
    blur /= weight;
    if (blur.a < 0.004)
        return col_in;
    vec3 glow_col = blur.rgb / max(blur.a, 0.001);
    // glow.frag boosted the colour (x4) and blended with alpha 0.9 * a;
    // keep the halo outside the silhouette like the GL stencil mask did.
    float a = clamp(blur.a * 1.5, 0.0, 1.0) * 0.6 * (1.0 - center.a);
    return mix(col_in, min(glow_col * 2.0, vec3(1.0)), a);
}

// ---- Volumetric light scattering (pointlightscatter.frag port) ----
// For every rendered point light, march the view ray through the light's
// sphere of influence accumulating fog in-scatter, with the same attenuation
// terms as the SP/OpenGL shader. density = 1 / (40 * track fog start); the
// GL pass used a white fog colour and additive blending at half resolution
// followed by a gaussian blur (the march is smooth enough unblurred).
vec3 lightScatter(vec2 px)
{
    float density = u_camera.m_postfx_flags2.y;
    if (density <= 0.0001)
        return vec3(0.0);

    vec3 pixelpos = viewPosAt(px);
    float pixel_len = length(pixelpos);
    if (pixel_len < 0.01)
        return vec3(0.0);
    vec3 eyedir = -normalize(pixelpos);

    vec3 fog = vec3(0.0);
    for (int i = 0; i < u_global_light.m_light_count; i++)
    {
        vec3 light_pos = (u_camera.m_view_matrix *
            vec4(u_global_light.m_lights[i].m_position_radius.xyz, 1.0)).xyz;
        // The GL chain blurs its half-res scatter buffer (~20 full-res px),
        // spreading small bright lamp cores into soft orbs. Equivalent here:
        // widen the scattering radius and conserve the in-scattered energy.
        float light_radius = u_global_light.m_lights[i].m_position_radius.w;
        float radius = max(light_radius * 2.0, light_radius + 4.0);
        float energy_scale = (light_radius * light_radius) /
            (radius * radius);
        float t_center = dot(-eyedir, light_pos);
        float t_far = min(t_center + radius, pixel_len);
        float t_near = t_center - radius;
        if (t_far <= max(t_near, 0.0))
            continue;
        vec3 farthestpoint = -eyedir * t_far;
        vec3 closestpoint = -eyedir * t_near;
        if (closestpoint.z < 1.0)
            closestpoint = vec3(0.0);

#ifdef TILED_GPU
        const int STEPS = 4;
#else
        const int STEPS = 8;
#endif
        float stepsize = length(farthestpoint - closestpoint) / float(STEPS);
        vec3 light_col =
            u_global_light.m_lights[i].m_color_inverse_square_range.xyz;
        // The GL energy uniform is folded into the GE light colour. The
        // extra gain compensates for compositing post-tonemap (the GL chain
        // accumulates in HDR where the bright cores survive the blur).
        vec3 fog_factor = light_col * density * stepsize * 20.0 *
            energy_scale * 56.0;
        vec3 xpos = farthestpoint;
        vec3 xpos_step = eyedir * stepsize;

        // Spotlight direction (same encoding as deferred_pointlight)
        float sscale =
            u_global_light.m_lights[i].m_direction_scale_offset.z;
        vec3 sdir = vec3(0.0);
        if (sscale != 0.0)
        {
            sdir = vec3(
                u_global_light.m_lights[i].m_direction_scale_offset.xy, 0.0);
            sdir.z = sqrt(max(1.0 - dot(sdir, sdir), 0.0)) * sign(sscale);
            sdir = (u_camera.m_view_matrix * vec4(sdir, 0.0)).xyz;
        }

        for (int j = 0; j < STEPS; j++)
        {
            vec3 light_to_pos = light_pos - xpos;
            float d = length(light_to_pos);
            float l = float(STEPS - j) * stepsize;
            vec3 base_att = fog_factor / (1.0 + d * d) *
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

// ---- Kerr black hole accretion ----
// Visuals for the thrown black hole powerup: an inclined accretion disk
// (the iconic Interstellar look) built in a screen-space frame aligned to
// the disk's real orientation, with a gravitationally lensed top arc,
// relativistic Doppler beaming and a thin photon ring. The gameplay
// position/radius are untouched; everything here is additive on top of the
// existing screen-space lensing.
//
// Cheap value noise + fbm for the procedural accretion plasma.
float bhHash(vec2 p)
{
    return fract(sin(dot(p, vec2(41.31, 289.17))) * 43758.5453);
}
float bhNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(bhHash(i), bhHash(i + vec2(1.0, 0.0)), f.x),
               mix(bhHash(i + vec2(0.0, 1.0)), bhHash(i + vec2(1.0, 1.0)),
               f.x), f.y);
}
float bhFbm(vec2 p)
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

// bhBlackbody(): cheap blackbody-locus colour from a normalised temperature
// t01 in [0,1]. 0 = cool deep-red outer rim, ~0.6 = orange/white-hot,
// 1 = blue-white inner edge. Brighter than 1.0 in the hot range so the inner
// disk blooms on the tonemapped target. Piecewise fit avoids the cost of a
// full Planck integral while keeping the physical hot->cool hue ordering.
vec3 bhBlackbody(float t01)
{
    t01 = clamp(t01, 0.0, 1.0);
    vec3 c0 = vec3(0.75, 0.10, 0.02);   // coolest rim (deep red)
    vec3 c1 = vec3(1.20, 0.45, 0.10);   // orange
    vec3 c2 = vec3(1.35, 1.18, 0.88);   // white-hot
    vec3 c3 = vec3(1.05, 1.20, 1.55);   // blue-white inner edge
    if (t01 < 0.40)
        return mix(c0, c1, t01 / 0.40);
    if (t01 < 0.75)
        return mix(c1, c2, (t01 - 0.40) / 0.35);
    return mix(c2, c3, (t01 - 0.75) / 0.25);
}

// diskSample(): brightness/colour of the equatorial disk annulus sampled in
// the disk's local screen frame. a = along the disk (major axis), b =
// across it (minor axis, already un-squashed by the inclination). doppler
// is +1 on the approaching (boosted) side, -1 on the receding side.
//
// Physical model (thin relativistic accretion disk, evaluated analytically so
// it runs in the post-process budget — no per-pixel geodesic march):
//  - Shakura-Sunyaev effective temperature  T(r) ~ r^-3/4 (1-sqrt(r_in/r))^1/4
//    mapped through bhBlackbody() for the hue (hot blue-white in, red rim).
//  - Keplerian orbital speed beta(r) ~ r^-1/2, capped for stability.
//  - Relativistic Doppler factor D = 1/(gamma(1 - beta*cos_los)) plus a
//    gravitational redshift g = sqrt(1 - r_s/r); the combined shift blue/red
//    shifts the temperature and drives bolometric beaming I_obs ~ shift^4
//    (the "headlight effect" — approaching side bright/blue, receding dim/red).
// The churning plasma is procedural: domain-warped fbm scrolled by the same
// Keplerian flow (Omega ~ r^-3/2) so inner annuli visibly shear faster.
vec3 diskSample(float a, float b, float doppler, out float bright)
{
    const float DISK_IN = 1.45;   // inner edge (~ISCO) in R_E units
    const float DISK_OUT = 3.6;   // nominal outer radius
    float rho = sqrt(a * a + b * b);
    bright = 0.0;
    // Wide guard: the outer envelope is a long exponential tail, so cut off
    // only far out where it has already faded to nothing (no hard ring).
    if (rho < DISK_IN * 0.6 || rho > DISK_OUT * 2.4)
        return vec3(0.0);
    float t = u_camera.m_postfx_flags2.w;
    float phi = atan(b, a);

    // Procedural plasma scrolled by the Keplerian angular rate Omega ~ r^-3/2
    // (so v ~ r^-1/2). Same handedness so it never reverses; low-frequency so
    // it boils smoothly instead of strobing. Slow inward drift on top.
    float r_phys  = max(rho, DISK_IN);
    float omega   = 1.6 * pow(r_phys / DISK_IN, -1.5);
    float u = phi + t * omega;               // along-orbit coordinate
    float v = rho * 0.8 - t * 0.16;          // slow inward drift
    // Sample the noise on a slowly orbiting ring (seamless around the disk).
    vec2 tc = vec2(cos(u), sin(u)) * (1.2 + rho * 0.5) + vec2(0.0, v);
    float warp = bhFbm(tc * 1.1);
    float turb = bhFbm(tc * 2.0 + warp * 1.0);
    // Keep a base level so the brightness modulation never drops to zero
    // (which read as flicker); the noise rides on top.
    turb = mix(0.55, turb, 0.8);

    // Radial envelope: soft turbulent inner ramp, then a long exponential
    // outer tail so the disk fades out smoothly with no fixed-size hard
    // boundary.
    float inner = smoothstep(DISK_IN * 0.6, DISK_IN * 1.08, rho + turb * 0.45);
    float outer = exp(-pow(max(rho - DISK_IN, 0.0) / (DISK_OUT - DISK_IN),
                  1.25) * 1.35);
    float env = inner * outer;
    // Strong plasma but with a solid base so it doesn't flicker dark.
    float prof = env * (0.7 + 1.0 * turb);

    // --- Relativistic shading ---------------------------------------------
    // Shakura-Sunyaev effective temperature, remapped to a 0..1 ramp.
    float t_ss   = pow(r_phys / DISK_IN, -0.75) *
                   pow(max(1.0 - sqrt(DISK_IN / r_phys), 0.0), 0.25);
    float temp01 = clamp(t_ss * 1.35, 0.0, 1.0);

    // Keplerian orbital speed (fraction of c), capped for numerical stability.
    float beta   = clamp(0.55 * sqrt(DISK_IN / r_phys), 0.0, 0.85);
    float gamma  = 1.0 / sqrt(max(1.0 - beta * beta, 1e-3));
    // Relativistic Doppler factor along the projected line of sight, and the
    // gravitational redshift climbing out of the well near the inner edge.
    float D      = 1.0 / (gamma * (1.0 - beta * doppler));
    float g_grav = sqrt(max(1.0 - DISK_IN / (r_phys + DISK_IN * 0.5), 0.05));
    float shift  = D * g_grav;               // >1 boosted/blue, <1 dim/red

    // Blue/red-shift the temperature, then colour it on the blackbody locus.
    float temp_obs = clamp(temp01 * (0.6 + 0.4 * shift), 0.0, 1.0);
    vec3  col = bhBlackbody(temp_obs);
    // Hotter cores in the bright turbulent knots.
    col = mix(col, vec3(1.4, 1.34, 1.25), clamp((turb - 0.6) * 1.5, 0.0, 0.6));

    // Bolometric beaming (headlight effect), with a dim floor so the receding
    // side still reads all the way round rather than vanishing.
    float beam = clamp(pow(shift, 4.0), 0.3, 4.0);
    bright = max(prof, 0.0) * beam;
    return col;
}

struct BlackHoleProjection
{
    bool valid;
    vec3 apparent_pos;
    vec2 screen;
    vec2 ndc;
    float radius_px;
    float view_z;
    vec2 minor_axis;
    vec2 major_axis;
    vec2 bright_dir;
    float inclination;
};

// Built once per output fragment in main and reused by the lens, emission,
// flare and trail paths.  Besides guaranteeing identical coordinates, this
// avoids repeating the observer transform for every one of the trail taps.
BlackHoleProjection g_black_hole_projection[4];

bool bhFinite(vec2 v)
{
    return !any(isnan(v)) && !any(isinf(v));
}

bool projectBlackHolePoint(vec3 world_pos, out vec3 apparent_pos,
                           out vec4 clip, out vec2 screen)
{
    // Black-hole post effects use a stable observer-frame anchor.  Deliberately
    // omit projectile-velocity retardation: fired black holes can transiently
    // exceed the configurable c_light, whereas observer aberration is smooth
    // over the complete options-menu beta range.
    apparent_pos = applyRelativisticVisualPosition(
        vec4(world_pos, 1.0), vec3(0.0), 1.0).xyz;
    clip = u_camera.m_projection_view_matrix * vec4(apparent_pos, 1.0);
    if (clip.w <= 0.001 || clip.z <= 0.0 || any(isnan(clip)) || any(isinf(clip)))
        return false;

    vec2 ndc = clip.xy / clip.w;
    screen = u_camera.m_viewport.xy +
        (ndc * 0.5 + 0.5) * u_camera.m_viewport.zw;
    return bhFinite(screen);
}

BlackHoleProjection projectBlackHole(int index)
{
    BlackHoleProjection bh;
    bh.valid = false;
    bh.apparent_pos = vec3(0.0);
    bh.screen = vec2(0.0);
    bh.ndc = vec2(0.0);
    bh.radius_px = 2.0;
    bh.view_z = 0.0;
    bh.minor_axis = vec2(0.0, 1.0);
    bh.major_axis = vec2(-1.0, 0.0);
    bh.bright_dir = vec2(1.0, 0.0);
    bh.inclination = 0.42;

    float radius = u_camera.m_black_holes[index].w;
    if (radius <= 0.001)
        return bh;

    vec3 raw_pos = u_camera.m_black_holes[index].xyz;
    vec4 center_clip;
    if (!projectBlackHolePoint(raw_pos, bh.apparent_pos, center_clip, bh.screen))
        return bh;

    bh.ndc = center_clip.xy / center_clip.w;
    vec4 center_view = u_camera.m_view_matrix * vec4(bh.apparent_pos, 1.0);
    bh.view_z = center_view.z;

    vec3 cam_right = vec3(u_camera.m_view_matrix[0][0],
                          u_camera.m_view_matrix[1][0],
                          u_camera.m_view_matrix[2][0]);
    vec3 rim_pos;
    vec4 rim_clip;
    vec2 rim_screen;
    bool rim_valid = projectBlackHolePoint(raw_pos + cam_right * radius,
        rim_pos, rim_clip, rim_screen);
    float projected_radius = 0.0;
    if (rim_valid)
        projected_radius = length(rim_screen - bh.screen);

    // Near the camera plane the transformed rim can cross behind the camera
    // even though the centre is visible.  A camera-space offset gives a finite
    // perspective fallback and keeps the effect alive rather than producing a
    // NaN, a one-frame flash, or a disappearing ring.
    if (!rim_valid || isnan(projected_radius) || isinf(projected_radius) ||
        projected_radius < 0.001)
    {
        vec4 fallback_clip = u_camera.m_projection_matrix *
            (center_view + vec4(radius, 0.0, 0.0, 0.0));
        if (fallback_clip.w > 0.001 && !any(isnan(fallback_clip)) &&
            !any(isinf(fallback_clip)))
        {
            vec2 fallback_screen = u_camera.m_viewport.xy +
                (fallback_clip.xy / fallback_clip.w * 0.5 + 0.5) *
                u_camera.m_viewport.zw;
            projected_radius = length(fallback_screen - bh.screen);
        }
    }
    float radius_limit = 2.0 * max(u_camera.m_viewport.z,
                                   u_camera.m_viewport.w);
    bh.radius_px = clamp(projected_radius, 2.0, radius_limit);

    vec3 up_pos;
    vec4 up_clip;
    vec2 up_screen;
    if (projectBlackHolePoint(raw_pos + vec3(0.0, radius, 0.0),
        up_pos, up_clip, up_screen))
    {
        vec2 minor = up_screen - bh.screen;
        if (dot(minor, minor) > 1e-8)
            bh.minor_axis = normalize(minor);
    }
    bh.major_axis = vec2(-bh.minor_axis.y, bh.minor_axis.x);

    vec3 cam_pos = u_camera.m_inverse_view_matrix[3].xyz;
    vec3 to_bh = bh.apparent_pos - cam_pos;
    float to_bh_len2 = dot(to_bh, to_bh);
    if (to_bh_len2 > 1e-8)
        to_bh *= inversesqrt(to_bh_len2);
    else
        to_bh = vec3(0.0, 0.0, 1.0);
    bh.inclination = clamp(abs(to_bh.y), 0.42, 0.85);

    vec3 bright_world = cross(vec3(0.0, 1.0, 0.0), to_bh);
    if (dot(bright_world, bright_world) > 1e-8)
    {
        bright_world = normalize(bright_world);
        vec3 bright_pos;
        vec4 bright_clip;
        vec2 bright_screen;
        if (projectBlackHolePoint(raw_pos + bright_world * radius,
            bright_pos, bright_clip, bright_screen))
        {
            vec2 bright_delta = bright_screen - bh.screen;
            if (dot(bright_delta, bright_delta) > 1e-8)
                bh.bright_dir = normalize(bright_delta);
        }
    }

    bh.valid = true;
    return bh;
}

// bhEmissionAt(): raw additive emission (disk + lensed arcs + photon ring)
// of every active black hole, evaluated at an arbitrary screen pixel. Kept
// separate from the compression/flare so the motion-trail can resample it
// along the hole's screen-space velocity.
vec3 bhEmissionAt(vec2 px)
{
    vec3 emission = vec3(0.0);
    vec2 vp_xy = u_camera.m_viewport.xy;
    vec2 vp_wh = u_camera.m_viewport.zw;
    for (int i = 0; i < 4; i++)
    {
        float bh_r = u_camera.m_black_holes[i].w;
        BlackHoleProjection bh = g_black_hole_projection[i];
        if (!bh.valid)
            continue;
        vec2 bh_screen = bh.screen;
        float R_E = bh.radius_px;

        vec2 d = px - bh_screen;
        float rr = length(d) / R_E;
        // Reach past the disk's long outer tail (see diskSample) so it isn't
        // clipped to a hard circle.
        if (rr > 9.0)
            continue;
        // Fade the disk when the hole is almost on top of the camera: the
        // shadow then fills the screen and the pure lensing should take
        // over (otherwise the disk math degenerates into a bright ring).
        float close_fade = 1.0 - smoothstep(0.32, 0.5, R_E / vp_wh.y);
        if (close_fade <= 0.0)
            continue;

        // Occlusion: hide the disk only where scene geometry is clearly in
        // front of the whole hole (walls, karts). The generous margin (the
        // disk spans ~3.6 radii and floats above the ball) lets the disk draw
        // over the surrounding dunes instead of being clipped - matching the
        // lenient hovering wormhole.
        bool is_sky = texture(u_depth, px / u_camera.m_screensize).x >= 1.0;
        if (!is_sky && viewPosAt(px).z < bh.view_z - bh_r * 10.0)
            continue;

        // Disk orientation on screen: project the disk's world normal
        // (equatorial plane -> world up) to get the minor (across) axis.
        vec2 minor = bh.minor_axis;
        vec2 major = bh.major_axis;

        // Inclination: 0 edge-on, 1 face-on. Clamped so the disk always
        // reads as a clear ellipse (never a degenerate edge-on line).
        float incl = bh.inclination;

        // Local disk coordinates (un-squash the across axis by inclination).
        float a = dot(d, major) / R_E;
        float b = dot(d, minor) / R_E;
        // Constant spin: the disk always orbits the same way about world up,
        // so the approaching (Doppler-boosted) side is a stable world
        // direction projected to screen - it tracks smoothly as the camera
        // moves instead of flipping.
        float doppler = dot(d / max(length(d), 1e-3), bh.bright_dir);
        float dwarm = clamp(doppler * 0.5 + 0.5, 0.0, 1.0);

        vec3 bh_em = vec3(0.0);

        // Primary (direct) disk image: the inclined annulus ellipse.
        float bd;
        vec3 dcol = diskSample(a, b / incl, doppler, bd);
        bh_em += dcol * bd;

        // Lensed arcs: the far side of the disk is bent up and over the top
        // of the shadow, the near underside under the bottom. Warm bands
        // hugging the shadow edge along the minor (vertical) axis.
        float vert = b / max(rr, 1e-3);            // +1 top, -1 bottom
        float arc_band = exp(-pow((rr - 1.22) * 3.0, 2.0));
        float top_arc = arc_band * smoothstep(0.0, 0.55, vert) * 1.35;
        float bot_arc = arc_band * smoothstep(0.1, 0.7, -vert) * 0.8;
        vec3 arc_warm = mix(vec3(1.15, 0.55, 0.18), vec3(1.4, 1.3, 1.15),
            dwarm);
        bh_em += arc_warm * (top_arc + bot_arc) * mix(0.6, 1.7, dwarm);

        // Photon ring / Einstein ring: thin bright circle at the shadow edge,
        // Doppler-tinted. Widened, boosted and azimuthally modulated (same
        // 0.85 + 0.15*cos(2*theta) profile as the wormhole) so the rim reads
        // as a luminous lensed ring rather than a flat hoop.
        float theta_r = atan(d.y, d.x);
        float mod_az  = 0.85 + 0.15 * cos(theta_r * 2.0);
        float ring = exp(-pow((rr - 1.04) * 9.0, 2.0));
        vec3 ring_col = mix(vec3(1.1, 0.8, 0.5), vec3(1.45, 1.35, 1.15), dwarm);
        bh_em += ring_col * ring * mod_az * mix(0.8, 2.1, dwarm);

        emission += bh_em * close_fade;
    }
    return emission;
}

// Screen-space velocity of the black hole nearest to px, from camera
// reprojection (previous projection*view). Drives the disk's ghost trail:
// as the kart sweeps the hole across the screen the luminous disk smears
// behind its motion.
vec2 bhScreenVelocity(vec2 px, out float trail_radius)
{
    vec2 vp_xy = u_camera.m_viewport.xy;
    vec2 vp_wh = u_camera.m_viewport.zw;
    float best = 1e9;
    vec2 vel = vec2(0.0);
    trail_radius = 0.0;
    for (int i = 0; i < 4; i++)
    {
        BlackHoleProjection bh = g_black_hole_projection[i];
        if (!bh.valid)
            continue;
        vec2 cur = bh.screen;
        vec4 prev_clip = u_camera.m_previous_pv_matrix *
            vec4(bh.apparent_pos, 1.0);
        if (prev_clip.w <= 0.001 || prev_clip.z <= 0.0 ||
            any(isnan(prev_clip)) || any(isinf(prev_clip)))
            continue;
        vec2 prev = vp_xy +
            (prev_clip.xy / prev_clip.w * 0.5 + 0.5) * vp_wh;
        if (!bhFinite(prev))
            continue;
        float dsc = length(px - cur) / bh.radius_px;
        if (dsc < best)
        {
            best = dsc;
            vel = cur - prev;
            trail_radius = bh.radius_px;
        }
    }
    return vel;
}

// Warm anamorphic lens flare emitted by the accretion disk, so the hole
// reads as a genuine light source. Ghost sprites stride along the line from
// the hole through the screen centre, tinted by the disk and faded by depth
// occlusion; strength shares the lens-flare settings knob.
vec3 bhLensFlare(vec2 px)
{
    float knob = u_camera.m_postfx_flags2.z;
    if (knob <= 0.001)
        return vec3(0.0);
    vec2 vp_xy = u_camera.m_viewport.xy;
    vec2 vp_wh = u_camera.m_viewport.zw;
    vec2 center = vp_xy + vp_wh * 0.5;
    vec3 flare = vec3(0.0);

    for (int i = 0; i < 4; i++)
    {
        float bh_r = u_camera.m_black_holes[i].w;
        BlackHoleProjection bh = g_black_hole_projection[i];
        if (!bh.valid)
            continue;
        vec2 bh_ndc = bh.ndc;
        if (abs(bh_ndc.x) > 1.2 || abs(bh_ndc.y) > 1.2)
            continue;
        vec2 bh_screen = bh.screen;

        // Depth occlusion: no flare when scene geometry hides the hole.
        bool is_sky = texture(u_depth,
            bh_screen / u_camera.m_screensize).x >= 1.0;
        if (!is_sky && viewPosAt(bh_screen).z < bh.view_z - bh_r * 10.0)
            continue;

        // Fade the flare out as the hole fills the screen (the disk fades
        // too at point-blank range) and as it nears the screen edge.
        float R_E = bh.radius_px;
        float close_fade = 1.0 - smoothstep(0.30, 0.5, R_E / vp_wh.y);
        float edge = (1.0 - smoothstep(0.9, 1.2, abs(bh_ndc.x))) *
            (1.0 - smoothstep(0.9, 1.2, abs(bh_ndc.y)));
        float vis = close_fade * edge;
        if (vis <= 0.0)
            continue;

        vec2 axis = center - bh_screen;
        vec3 warm = vec3(1.25, 0.7, 0.3);
        vec3 f = vec3(0.0);
        // A few ghosts mirrored along the flare axis.
        const vec4 G[4] = vec4[](
            vec4(0.30, 0.05, 0.18, 0.0),
            vec4(0.62, 0.028, 0.14, 0.6),
            vec4(1.15, 0.07, 0.10, 0.2),
            vec4(1.55, 0.04, 0.07, 0.8));
        for (int g = 0; g < 4; g++)
        {
            vec2 gp = bh_screen + axis * G[g].x;
            float sz = G[g].y * vp_wh.y;
            float d = length(px - gp) / sz;
            float shape = exp(-d * d * 1.8);
            vec3 gc = mix(warm, warm.bgr, G[g].w);
            f += gc * (shape * G[g].z);
        }
        // Soft halo right at the hole + faint anamorphic streak.
        float hd = length(px - bh_screen);
        f += warm * exp(-pow(hd / (0.045 * vp_wh.y), 2.0)) * 0.10;
        vec2 dp = px - bh_screen;
        float streak = exp(-pow(dp.y / (0.004 * vp_wh.y), 2.0)) *
            exp(-pow(dp.x / (0.16 * vp_wh.x), 2.0));
        f += vec3(1.3, 1.0, 0.7) * streak * 0.14;

        flare += f * vis;
    }
    return flare * knob;
}

vec3 kerrAccretion(vec2 px)
{
    // Ghost trail: smear the disk emission along the hole's screen velocity
    // (a crisp head plus a decaying after-image), so a moving hole drags a
    // luminous tail instead of looking stapled in place.
    vec3 em = bhEmissionAt(px);
    float trail_radius;
    vec2 vel = bhScreenVelocity(px, trail_radius);
    if (dot(vel, vel) > 0.25)
    {
        const int N = 5;
        const float TRAIL = 4.0;   // frames of smear
        float trail_length = length(vel) * TRAIL;
        if (trail_length > trail_radius && trail_length > 1e-4)
            vel *= trail_radius / trail_length;
        for (int k = 1; k <= N; k++)
        {
            float t = float(k) / float(N);
            em += bhEmissionAt(px + vel * (t * TRAIL)) * (0.3 * (1.0 - t));
        }
    }

    // The disk is a light source: emit a warm lens flare.
    em += bhLensFlare(px);

    // Compress so the additive plasma keeps detail on this tonemapped
    // input instead of clipping to white.
    return em / (1.0 + sceneLuma(em) * 0.4);
}

void main()
{
#ifdef PBR_ENABLED
    vec2 vp_xy = u_camera.m_viewport.xy;
    vec2 vp_wh = u_camera.m_viewport.zw;
    vec2 frag_px = gl_FragCoord.xy;

    for (int bh_i = 0; bh_i < 4; bh_i++)
        g_black_hole_projection[bh_i] = projectBlackHole(bh_i);

    // Source pixel in the scene colour texture for this output pixel.
    vec2 src_px = frag_px;
    bool in_event_horizon = false;
    float distortion_strength = 0.0;
    // Chromatic aberration of the lensed background (wormhole-style): radial
    // direction and fringe magnitude of the strongest lensing at this pixel.
    vec2  bh_chroma_dir = vec2(0.0);
    float bh_chroma_amt = 0.0;

    // ---- Gravitational lensing from the active black holes ----
    // u_black_holes[i].w = world-space sphere radius (0 = slot inactive).
    // Several black holes can be live at once; apply each in turn on the
    // already-warped source position so the lenses compose.
    for (int bh_i = 0; bh_i < 4; bh_i++)
    {
        if (in_event_horizon)
            continue;
        BlackHoleProjection bh = g_black_hole_projection[bh_i];
        if (bh.valid)
        {
            vec2 bh_screen = bh.screen;
            float R_E = bh.radius_px;
            // Effective lensing reach, pushed well past the shadow so the
            // deflection is as dramatic as the wormhole's. The black shadow
            // itself stays at R_E (the ball); only the warp strengthens.
            // Tunable: raise for stronger bending (recompiles on launch).
            const float BH_LENS_STRENGTH = 1.55;
            float R_L = R_E * BH_LENS_STRENGTH;

            // The visible silhouette belongs to output space and must never
            // inherit a warp accumulated by an earlier lens.  Source space is
            // used only to compose the background lookup below.
            vec2 visible_delta = frag_px - bh_screen;
            float visible_r = length(visible_delta);

            // Outer bound scales with the boosted reach so the wider warp
            // isn't clipped; the deflection eases off gradually toward it.
            if (visible_r > 0.5 && visible_r < R_L * 6.0)
            {
                // Skip lensing where scene is in front of the black hole
                // (view-space compare: raw depth01 epsilons let distant
                // walls pass).
                if (viewPosAt(frag_px).z >=
                    bh.view_z - u_camera.m_black_holes[bh_i].w * 10.0)
                {
                    // Frame dragging (Kerr): the deflection direction is
                    // swirled around the spin axis, strongest near the
                    // horizon.
                    float drag = min(0.9 * (R_L * R_L) /
                        (visible_r * visible_r), 0.9);
                    float cd = cos(drag), sd = sin(drag);
                    vec2 visible_dragged = vec2(
                        cd * visible_delta.x - sd * visible_delta.y,
                        sd * visible_delta.x + cd * visible_delta.y);
                    if (visible_r < R_E)
                    {
                        // True shadow (ball-sized) stays pure black.
                        in_event_horizon = true;
                    }
                    else
                    {
                        // Strong Schwarzschild deflection: scene wraps tightly
                        // around the shadow rim (clamped positive like the
                        // wormhole) so a pronounced ring of lensed scenery
                        // smears around the hole.
                        vec2 source_delta = src_px - bh_screen;
                        float source_r = max(length(source_delta), 0.5);
                        vec2 source_dragged = vec2(
                            cd * source_delta.x - sd * source_delta.y,
                            sd * source_delta.x + cd * source_delta.y);
                        float r_src = max(source_r - (R_L * R_L) / source_r,
                            R_E * 0.05);
                        vec2 deflected = bh_screen +
                            (source_dragged / source_r) * r_src;
                        float fade = 1.0 - smoothstep(R_L * 2.5,
                            R_L * 6.0, visible_r);
                        src_px = mix(src_px, deflected, fade);
                        distortion_strength = max(distortion_strength,
                            clamp(1.0 - (r_src / (R_L * 2.0)), 0.0, 1.0) *
                            fade);
                        // Chromatic fringe near the rim, at wormhole strength.
                        float ring_dist = abs(visible_r - R_E) / R_E;
                        float ca = min(2.0 / max(ring_dist + 0.15, 0.15),
                            12.0) * fade;
                        ca = min(ca, max(1.0, R_E * 0.25));
                        if (ca > bh_chroma_amt)
                        {
                            bh_chroma_amt = ca;
                            bh_chroma_dir = visible_dragged / visible_r;
                        }
                    }
                    src_px = clamp(src_px, vp_xy, vp_xy + vp_wh);
                }
            }
        }
    }

    if (in_event_horizon)
    {
        // Inside the shadow: pure black, but the front rim of the accretion
        // disk (matter between camera and hole) still occludes it.
        o_color = vec4(kerrAccretion(frag_px), 1.0);
        return;
    }

    // ---- Compactification: Calabi-Yau screen warp (banana debuff) ----
    // Maps every output row toward a thin strip around the horizon.
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
    if (u_push_constants.m_has_displace)
    {
        vec2 mask = texelFetch(u_displace_mask, ivec2(frag_px), 0).xy;
        if (!(mask.x == 0.0 && mask.y == 0.0))
        {
            vec2 shift = 2.0 * mask - 1.0;
            ivec2 displaced = getDisplaceUV(shift, u_camera.m_viewport,
                u_displace_mask);
            src_px += vec2(displaced) - frag_px;
        }
    }

    // Base scene sample with the SP/OpenGL advanced-pipeline post effects:
    // anti-aliasing, SSAO and bloom, all evaluated at the lens-warped source
    // position so black holes bend them with the rest of the scene.
    vec3 col;
    if (bh_chroma_amt > 0.01)
    {
        // Per-channel radial split of the lensed background: it fringes into
        // colour near the ring, matching the wormhole's chromatic lensing.
        // (AA/CAS skipped for these few near-hole pixels.)
        vec2 ca = bh_chroma_dir * bh_chroma_amt;
        col.r = sampleScene(clamp(src_px + ca, vp_xy, vp_xy + vp_wh)).r;
        col.g = sampleScene(src_px).g;
        col.b = sampleScene(clamp(src_px - ca, vp_xy, vp_xy + vp_wh)).b;
    }
    else
    {
        col = u_camera.m_postfx_flags.w > 0.5 ?
            antialiasScene(src_px) : sampleScene(src_px);
        // Contrast-adaptive sharpening recovers the crispness FXAA removes
        // (tied to the anti-aliasing toggle).
        if (u_camera.m_postfx_flags.w > 0.5)
            col = casSharpen(src_px, col);
    }
    if (u_camera.m_postfx_flags.x > 0.5)
        col += bloomGather(src_px);
    // ---- Track distance fog ----
    // Mirrors combine_diffuse_color.frag: haze toward the track fog colour
    // with density 1 / (40 * fog_start), skipping the skybox.
    if (u_global_light.m_fog_density > 0.0001)
    {
        float fog_z = texture(u_depth, src_px / u_camera.m_screensize).x;
        if (fog_z < 1.0)
        {
            float fog_dist = length(viewPosAt(src_px));
            float fog_f = clamp(1.0 - exp(-u_global_light.m_fog_density *
                fog_dist), 0.0, 1.0);
            col = mix(col, u_global_light.m_fog_color.rgb, fog_f);
        }
    }

    // God rays are evaluated at the lens-warped source position so black
    // holes bend and smear the sun glow along with the rest of the scene.
    col += godRays(src_px);
    col += lensFlare(src_px);
    // Per-object glow outlines and volumetric light scattering, also at the
    // lens-warped position.
    if (u_camera.m_postfx_flags2.x > 0.5)
        col = glowOutline(col, src_px);
    // The GL chain accumulates the fog in-scatter in HDR before the
    // tonemapper compresses it; here the input is already tonemapped, so
    // soft-clamp the contribution to avoid blown-out fog blobs.
    // The GL chain accumulates the fog in-scatter in HDR before the
    // tonemapper compresses it; here the input is already tonemapped, so
    // soft-clamp the contribution to avoid blown-out fog blobs.
    vec3 scatter = lightScatter(src_px);
    col += scatter / (1.0 + sceneLuma(scatter));

    // Darken the distorted region near the black hole.
    if (distortion_strength > 0.01)
        col *= (1.0 - distortion_strength * 0.4);

    // Kerr accretion disk, photon ring and lensed arcs (additive, computed
    // at the true pixel direction rather than the lens-warped lookup).
    col += kerrAccretion(frag_px);

    // ---- Wormhole: Interstellar-style lensing mouth ----
    // u_wormhole.xyz = world-space mouth centre, .w = radius (>0 iff active).
    if (u_camera.m_wormhole.w > 0.01)
    {
        vec4 wh_clip = u_camera.m_projection_view_matrix *
            vec4(u_camera.m_wormhole.xyz, 1.0);
        if (wh_clip.w > 0.001 && wh_clip.z > 0.0)
        {
            vec2 wh_ndc = wh_clip.xy / wh_clip.w;
            vec2 wh_screen = vp_xy + (wh_ndc * 0.5 + 0.5) * vp_wh;
            float wh_depth01 = wh_clip.z / wh_clip.w;

            vec3 cam_right = vec3(u_camera.m_view_matrix[0][0],
                                  u_camera.m_view_matrix[1][0],
                                  u_camera.m_view_matrix[2][0]);
            vec4 rim_clip = u_camera.m_projection_view_matrix *
                vec4(u_camera.m_wormhole.xyz +
                cam_right * u_camera.m_wormhole.w, 1.0);
            vec2 rim_ndc = rim_clip.xy / max(rim_clip.w, 0.001);
            vec2 rim_screen = vp_xy + (rim_ndc * 0.5 + 0.5) * vp_wh;
            float R_S = max(length(rim_screen - wh_screen), 8.0);

            vec2 delta = frag_px - wh_screen;
            float r = length(delta);
            float R_LENS_OUTER = R_S * 4.0;

            if (r > 0.1 && r < R_LENS_OUTER)
            {
                // Skip lensing only where scene geometry is clearly in front
                // of the whole mouth (view-space compare with a margin that
                // spans the mouth radius), so the road the wormhole sits on
                // does not clip it - matching the black hole.
                float wh_vz = (u_camera.m_view_matrix *
                    vec4(u_camera.m_wormhole.xyz, 1.0)).z;
                bool wh_sky =
                    texture(u_depth, frag_px / u_camera.m_screensize).x >= 1.0;
                if (wh_sky || viewPosAt(frag_px).z >=
                    wh_vz - u_camera.m_wormhole.w * 4.0)
                {
                    vec2 dir = delta / max(r, 0.001);
                    // Gentle static swirl around the mouth.
                    float swirl =
                        0.18 * exp(-pow((r - R_S) / (R_S * 1.2), 2.0));
                    float cs = cos(swirl), sn = sin(swirl);
                    vec2 swirl_dir = vec2(cs * dir.x - sn * dir.y,
                                          sn * dir.x + cs * dir.y);

                    if (r < R_S)
                    {
                        // Inside the mouth: stereographic-ish compression
                        // with chromatic aberration toward the rim.
                        float rn = r / R_S;
                        float rn_fisheye = sin(rn * 1.5707963) * 0.98;
                        vec2 inside_px =
                            wh_screen + swirl_dir * (rn_fisheye * R_S);
                        float ca = 0.012 * (rn * rn);
                        vec2 px_r = wh_screen + swirl_dir *
                            (rn_fisheye * R_S * (1.0 + ca));
                        vec2 px_b = wh_screen + swirl_dir *
                            (rn_fisheye * R_S * (1.0 - ca));
                        vec3 portal_col = vec3(
                            sampleScene(clamp(px_r, vp_xy, vp_xy + vp_wh)).r,
                            sampleScene(clamp(inside_px, vp_xy,
                                              vp_xy + vp_wh)).g,
                            sampleScene(clamp(px_b, vp_xy,
                                              vp_xy + vp_wh)).b);
                        float rim_darken =
                            mix(1.0, 0.65, smoothstep(0.55, 1.0, rn));
                        col = portal_col * rim_darken;
                    }
                    else
                    {
                        // Outside the mouth: Schwarzschild deflection.
                        float R_E = R_S * 1.05;
                        float r_src = max(r - (R_E * R_E) / r, R_S * 0.02);

                        float ring_dist = abs(r - R_S) / R_S;
                        float ca_out = 2.0 / max(ring_dist + 0.15, 0.15);
                        vec2 base_pos = wh_screen + swirl_dir * r_src;
                        vec3 lens_col = vec3(
                            sampleScene(clamp(base_pos + dir * ca_out,
                                vp_xy, vp_xy + vp_wh)).r,
                            sampleScene(clamp(base_pos,
                                vp_xy, vp_xy + vp_wh)).g,
                            sampleScene(clamp(base_pos - dir * ca_out,
                                vp_xy, vp_xy + vp_wh)).b);
                        float ring_blend =
                            1.0 - smoothstep(R_S, R_LENS_OUTER, r);
                        col = mix(col, lens_col, ring_blend);
                    }

                    // Einstein ring: peak brightness at the silhouette.
                    float ring_sigma = R_S * 0.12;
                    float ring_n = (r - R_S) / ring_sigma;
                    float ring = exp(-ring_n * ring_n);
                    float theta = atan(dir.y, dir.x);
                    float mod_az = 0.85 + 0.15 * cos(theta * 2.0);
                    float rim_boost = ring * mod_az * 0.45;
                    col = mix(col, col * 1.35, rim_boost);
                }
            }
        }
    }

    // ---- Depth of field (dof.frag port) ----
    if (u_camera.m_postfx_flags.z > 0.5)
        col = applyDOF(col, frag_px);

    // ---- Boost motion blur (reprojection-based, motion_blur.frag) ----
    float boost_amount = u_camera.m_motion_blur.x;
    if (boost_amount > 0.001)
    {
#ifdef TILED_GPU
        const int NB_SAMPLES = 4;
#else
        const int NB_SAMPLES = 8;
#endif
        vec2 texcoords = (frag_px - vp_xy) / vp_wh;

        // Reconstruct the world position of this pixel and reproject it
        // with last frame's projection*view to get the blur direction.
        float z = texture(u_depth, frag_px / u_camera.m_screensize).x;
        vec2 ndc = texcoords * 2.0 - 1.0;
        vec4 clip = vec4(ndc, z, 1.0);
        vec4 view_pos = u_camera.m_inverse_projection_matrix * clip;
        view_pos /= view_pos.w;
        vec4 world_pos = u_camera.m_inverse_view_matrix * view_pos;
        vec4 old_clip = u_camera.m_previous_pv_matrix * world_pos;
        old_clip /= old_clip.w;
        vec2 old_texcoords = old_clip.xy * 0.5 + 0.5;

        // Not normalized: avoids a glitch around the centre and scales
        // the blur naturally (see motion_blur.frag).
        vec2 blur_dir = texcoords - old_texcoords;

        vec2 center = u_camera.m_motion_blur.yz;
        float mask_radius = u_camera.m_motion_blur.w;
        float blur_factor =
            max(0.0, length(texcoords - center) - mask_radius);
        blur_factor *= boost_amount;
        blur_dir *= blur_factor;

        vec2 inc = blur_dir / float(NB_SAMPLES);
        vec2 blur_texcoords =
            texcoords - inc * float(NB_SAMPLES) / 2.0;
        for (int i = 1; i < NB_SAMPLES; i++)
        {
            vec2 tap = clamp(blur_texcoords, vec2(0.0), vec2(1.0));
            col += sampleScene(vp_xy + tap * vp_wh);
            blur_texcoords += inc;
        }
        col /= float(NB_SAMPLES);
    }

    // ---- Time-dilation gravitational wave (expanding ring) ----
    // u_grav_wave.xyz = world-space origin, .w = current radius (<=0 inactive).
    // A spacetime shockwave that rolls outward to ~50 m and fades to nothing
    // there; the c-light drop on each kart lands as this ring passes them.
    // Pond-ripple shockwave: concentric sinusoidal waves trailing an expanding
    // front (radius m_grav_wave.w, 0 -> 75 m). The wave is evaluated in world
    // space on the track surface (reconstructed from depth) so it looks like
    // ripples spreading across a pond; each ripple slope refracts the scene
    // (gravitational lensing) with radial chromatic aberration. Because it
    // fills the whole disturbed disc - not just a thin rim - it is clearly
    // visible even from the centre, where the firing kart sits.
    if (u_camera.m_grav_wave.w > 0.0)
    {
        float wave_r = u_camera.m_grav_wave.w;
        float zc = texture(u_depth, frag_px / u_camera.m_screensize).x;
        if (zc < 1.0) // ripples live on the ground, not the sky
        {
            vec2 tc = (frag_px - vp_xy) / vp_wh;
            vec4 clip = vec4(tc * 2.0 - 1.0, zc, 1.0);
            vec4 vpos = u_camera.m_inverse_projection_matrix * clip;
            vpos /= vpos.w;
            vec3 wpos = (u_camera.m_inverse_view_matrix * vpos).xyz;
            float d = distance(wpos, u_camera.m_grav_wave.xyz);

            if (d < wave_r + 3.0) // only inside the expanding front
            {
                const float K = 6.2831853 / 6.0;        // 6 m wavelength
                float phase = (d - wave_r) * K;          // crests trail the front
                // Amplitude: peaks at the front, ripples decay toward the calm
                // centre, and the whole disturbance dissipates by 75 m.
                float trail     = exp(-max(0.0, wave_r - d) / 22.0);
                float lead      = smoothstep(wave_r + 3.0, wave_r - 1.0, d);
                float edge_fade = clamp(1.0 - wave_r / 75.0, 0.0, 1.0);
                float in_fade   = smoothstep(0.0, 4.0, wave_r);
                float amp = trail * lead * edge_fade * in_fade;
                if (amp > 0.003)
                {
                    // Pond-surface slope drives the refraction; max bend on the
                    // wave flanks, none at crest/trough (like real water).
                    float slope = cos(phase) * amp;
                    // Screen-space radial direction from the wave centre.
                    vec4 oc = u_camera.m_projection_view_matrix *
                              vec4(u_camera.m_grav_wave.xyz, 1.0);
                    vec2 rdir = vec2(0.0, 1.0);
                    if (oc.w > 0.001)
                    {
                        vec2 ondc = oc.xy / oc.w;
                        vec2 osc = vp_xy + (ondc * 0.5 + 0.5) * vp_wh;
                        vec2 v = frag_px - osc;
                        if (dot(v, v) > 1.0) rdir = normalize(v);
                    }
                    float disp = slope * 60.0;            // refraction (pixels)
                    float ca   = abs(slope) * 28.0;       // chromatic split
                    vec2 base = frag_px + rdir * disp;
                    vec3 refr = vec3(
                        sampleScene(clamp(base + rdir * ca,
                            vp_xy, vp_xy + vp_wh)).r,
                        sampleScene(clamp(base,
                            vp_xy, vp_xy + vp_wh)).g,
                        sampleScene(clamp(base - rdir * ca,
                            vp_xy, vp_xy + vp_wh)).b);
                    col = mix(col, refr, clamp(amp * 2.0, 0.0, 1.0));
                    // Bluish sheen riding the crests for readability.
                    float crest = max(0.0, sin(phase)) * amp;
                    col += vec3(0.16, 0.28, 0.55) * (crest * 0.9);
                }
            }
        }
    }

    // ---- Vignette ----
    // Subtle corner darkening; aspect-softened so it reads as a gentle
    // photographic falloff rather than an oval frame. Strength from the
    // settings knob.
    {
        vec2 q = (frag_px - vp_xy) / vp_wh * 2.0 - 1.0;
        q.x *= 0.85;
        float r = length(q) * 0.7071;
        col *= 1.0 - u_camera.m_beauty_params.z * smoothstep(0.55, 1.0, r);
    }

    o_color = vec4(col, 1.0);
#endif
}
