layout(binding = 0) uniform sampler2D u_displace_mask;
layout(binding = 2) uniform sampler2D u_displace_color;
layout(binding = 3) uniform sampler2D u_depth;
layout(binding = 4) uniform sampler2D u_glow;
// Half-res blurred ambient occlusion (GEVulkanAOPass, data descriptor)
layout(set = 1, binding = 7) uniform sampler2D u_ao;

layout(location = 0) in vec2 f_uv;

layout(location = 0) out vec4 o_color;

layout(push_constant) uniform Constants
{
    bool m_has_displace;
} u_push_constants;

#include "utils/camera.glsl"
#include "utils/global_light_data.glsl"
#include "../utils/displace_utils.frag"

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
    for (int i = 0; i < 8; i++)
    {
        accum += brightPass(sampleScene(px + DIRS[i] * (4.0 * s))) * 0.25;
        accum += brightPass(sampleScene(px + DIRS[i] * (9.0 * s))) * 0.125;
        accum += brightPass(sampleScene(px + DIRS[i] * (16.0 * s))) * 0.0625;
    }
    // Total weight 0.5 + 8*(0.4375) = 4.0
    return accum * (0.25 / 4.0);
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
    float focus = clamp(max(1.1666 - (depth / 240.0), depth - 2000.0),
                        0.0, 1.0);
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

    vec4 sun_clip = u_camera.m_projection_view_matrix *
        vec4(u_camera.m_godrays_pos.xyz, 1.0);
    if (sun_clip.w <= 0.001 || sun_clip.z <= 0.0)
        return vec3(0.0);

    vec2 vp_xy = u_camera.m_viewport.xy;
    vec2 vp_wh = u_camera.m_viewport.zw;
    vec2 sun_ndc = sun_clip.xy / sun_clip.w;
    vec2 sun_screen = vp_xy + (sun_ndc * 0.5 + 0.5) * vp_wh;
    // Occlusion is tested in view space: raw depth01 is so non-linear that
    // any fixed epsilon lets distant walls pass (shafts leaked through
    // geometry). The interposer world radius doubles as the margin.
    float sun_vz = (u_camera.m_view_matrix *
        vec4(u_camera.m_godrays_pos.xyz, 1.0)).z;
    float sun_margin = u_camera.m_godrays_color.w;

    // Project the interposer's world radius to pixels (robust to FOV/aspect).
    vec3 cam_right = vec3(u_camera.m_view_matrix[0][0],
                          u_camera.m_view_matrix[1][0],
                          u_camera.m_view_matrix[2][0]);
    vec4 rim_clip = u_camera.m_projection_view_matrix *
        vec4(u_camera.m_godrays_pos.xyz +
             cam_right * u_camera.m_godrays_color.w, 1.0);
    vec2 rim_ndc = rim_clip.xy / max(rim_clip.w, 0.001);
    vec2 rim_screen = vp_xy + (rim_ndc * 0.5 + 0.5) * vp_wh;
    float R_px = max(length(rim_screen - sun_screen), 4.0);

    // Skip pixels far outside the shaft range to keep the pass cheap.
    float px_dist = length(px - sun_screen);
    if (px_dist > R_px * 14.0)
        return vec3(0.0);

    const int N = 24;
    const float DECAY = 0.90;
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
    return u_camera.m_godrays_color.rgb * (accum * 0.30 * opacity);
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

    vec4 sun_clip = u_camera.m_projection_view_matrix *
        vec4(u_camera.m_godrays_pos.xyz, 1.0);
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
    vec3 tint = normalize(u_camera.m_godrays_color.rgb + 0.2) * 1.2;
    vec3 flare = vec3(0.0);

    // Ghost sprites mirrored along the axis
    const vec4 GHOSTS[5] = vec4[](
        // [position along axis, size in viewport heights, weight, hue mix]
        vec4(0.45, 0.045, 0.30, 0.0),
        vec4(0.85, 0.025, 0.25, 0.5),
        vec4(1.25, 0.070, 0.20, 0.2),
        vec4(1.65, 0.035, 0.18, 0.8),
        vec4(2.05, 0.100, 0.12, 0.4));
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
    flare += tint * (streak * 0.35);

    // Fade ghosts as the sun leaves the screen edge; overall strength comes
    // from the settings knob (m_postfx_flags2.z).
    float edge_fade = (1.0 - smoothstep(0.85, 1.1, abs(sun_ndc.x))) *
        (1.0 - smoothstep(0.85, 1.1, abs(sun_ndc.y)));
    flare *= edge_fade * opacity * u_camera.m_postfx_flags2.z;

    // Only pay for the occlusion taps where flare would actually show.
    if (dot(flare, vec3(1.0)) < 0.002)
        return vec3(0.0);

    // Soft visibility: fraction of taps around the sun centre that see
    // past-the-sun depth (same view-space test as the god rays).
    float sun_vz = (u_camera.m_view_matrix *
        vec4(u_camera.m_godrays_pos.xyz, 1.0)).z;
    float sun_margin = u_camera.m_godrays_color.w;
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
    for (int i = 0; i < 8; i++)
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

        const int STEPS = 8;
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

void main()
{
#ifdef PBR_ENABLED
    vec2 vp_xy = u_camera.m_viewport.xy;
    vec2 vp_wh = u_camera.m_viewport.zw;
    vec2 frag_px = gl_FragCoord.xy;

    // Source pixel in the scene colour texture for this output pixel.
    vec2 src_px = frag_px;
    bool in_event_horizon = false;
    float distortion_strength = 0.0;

    // ---- Gravitational lensing from the active black holes ----
    // u_black_holes[i].w = world-space sphere radius (0 = slot inactive).
    // Several black holes can be live at once; apply each in turn on the
    // already-warped source position so the lenses compose.
    for (int bh_i = 0; bh_i < 4; bh_i++)
    {
        if (in_event_horizon || u_camera.m_black_holes[bh_i].w <= 0.001)
            continue;
        vec4 bh_clip = u_camera.m_projection_view_matrix *
            vec4(u_camera.m_black_holes[bh_i].xyz, 1.0);
        if (bh_clip.w > 0.001 && bh_clip.z > 0.0)
        {
            vec2 bh_ndc = bh_clip.xy / bh_clip.w;
            vec2 bh_screen = vp_xy + (bh_ndc * 0.5 + 0.5) * vp_wh;

            // Project the world-space sphere radius to pixels for R_E.
            vec3 cam_right = vec3(u_camera.m_view_matrix[0][0],
                                  u_camera.m_view_matrix[1][0],
                                  u_camera.m_view_matrix[2][0]);
            vec4 rim_clip = u_camera.m_projection_view_matrix *
                vec4(u_camera.m_black_holes[bh_i].xyz +
                cam_right * u_camera.m_black_holes[bh_i].w, 1.0);
            vec2 rim_ndc = rim_clip.xy / max(rim_clip.w, 0.001);
            vec2 rim_screen = vp_xy + (rim_ndc * 0.5 + 0.5) * vp_wh;
            float R_E = max(length(rim_screen - bh_screen), 2.0);

            float bh_depth01 = bh_clip.z / bh_clip.w;

            vec2 delta = src_px - bh_screen;
            float r = length(delta);

            if (r > 0.5 && r < R_E * 6.0)
            {
                // Skip lensing where scene is in front of the black hole.
                float this_depth =
                    texture(u_depth, frag_px / u_camera.m_screensize).x;
                if (this_depth >= bh_depth01 - 0.02)
                {
                    float r_src = r - (R_E * R_E) / r;
                    if (r_src <= 0.0)
                    {
                        in_event_horizon = true;
                    }
                    else
                    {
                        src_px = bh_screen + normalize(delta) * r_src;
                        distortion_strength = max(distortion_strength,
                            clamp(1.0 - (r_src / (R_E * 3.0)), 0.0, 1.0));
                    }
                    src_px = clamp(src_px, vp_xy, vp_xy + vp_wh);
                }
            }
        }
    }

    if (in_event_horizon)
    {
        o_color = vec4(0.0, 0.0, 0.0, 1.0);
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
    vec3 col = u_camera.m_postfx_flags.w > 0.5 ?
        antialiasScene(src_px) : sampleScene(src_px);
    // Contrast-adaptive sharpening recovers the crispness FXAA removes
    // (tied to the anti-aliasing toggle).
    if (u_camera.m_postfx_flags.w > 0.5)
        col = casSharpen(src_px, col);
    // Ambient occlusion: bilinear upsample of the half-res blurred AO
    // computed by the GEVulkanAOPass dispatches.
    if (u_camera.m_postfx_flags.y > 0.5)
    {
        vec2 ao_uv = (src_px - vp_xy) / vp_wh;
        col *= texture(u_ao, ao_uv).x;
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
                // Skip lensing where scene is in front of the wormhole.
                float this_depth =
                    texture(u_depth, frag_px / u_camera.m_screensize).x;
                if (this_depth >= wh_depth01 - 0.02)
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
        const int NB_SAMPLES = 8;
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
