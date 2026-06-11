layout(binding = 0) uniform sampler2D u_displace_mask;
layout(binding = 2) uniform sampler2D u_displace_color;
layout(binding = 3) uniform sampler2D u_depth;

layout(location = 0) in vec2 f_uv;

layout(location = 0) out vec4 o_color;

layout(push_constant) uniform Constants
{
    bool m_has_displace;
} u_push_constants;

#include "utils/camera.glsl"
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

    vec3 col = sampleScene(src_px);

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

    o_color = vec4(col, 1.0);
#endif
}
