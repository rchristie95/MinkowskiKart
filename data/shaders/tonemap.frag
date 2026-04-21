uniform sampler2D tex;
uniform float vignette_weight;

out vec4 FragColor;

#stk_include "utils/getCIEXYZ.frag"
#stk_include "utils/getRGBfromCIEXxy.frag"
#stk_include "utils/relativity_color.frag"

void main()
{
    vec2 uv = gl_FragCoord.xy / u_screen;
    bool in_event_horizon = false;  // Track if we're inside the black hole shadow
    float distortion_strength = 0.0; // Track how strong the lensing is at this pixel

    // ---- Gravitational lensing from active black hole ----
    if (u_black_hole.w > 0.5)
    {
        // Project world-space black hole position into clip space
        vec4 bh_clip = u_projection_view_matrix * vec4(u_black_hole.xyz, 1.0);
        if (bh_clip.w > 0.001 && bh_clip.z > 0.0)
        {
            // NDC → pixel coordinates
            vec2 bh_ndc    = bh_clip.xy / bh_clip.w;
            vec2 bh_screen = (bh_ndc * 0.5 + 0.5) * u_screen;

            vec2  delta = gl_FragCoord.xy - bh_screen;
            float r     = length(delta);

            // Einstein ring radius (pixels). Increased for more dramatic effect.
            const float R_E = 75.0;

            if (r > 0.5 && r < R_E * 6.0)
            {
                // Schwarzschild lens equation (point mass, small angle):
                //   r_source = r - R_E^2 / r
                // r_source < 0  → inside shadow / event-horizon → black
                float r_src = r - (R_E * R_E) / r;

                if (r_src <= 0.0)
                {
                    // Event horizon: all light absorbed → pure black
                    // Mark this pixel as inside the event horizon so tone-mapping
                    // doesn't add unwanted color/brightness back in.
                    in_event_horizon = true;
                    uv = vec2(-1.0); // will clamp to black below
                }
                else
                {
                    // Remap: sample scene from the direction the photon actually came from.
                    vec2 sample_pos = bh_screen + normalize(delta) * r_src;
                    uv = sample_pos / u_screen;

                    // Track distortion strength for darkening effect
                    // Closer to event horizon = stronger darkening
                    distortion_strength = 1.0 - (r_src / (R_E * 3.0));
                    distortion_strength = clamp(distortion_strength, 0.0, 1.0);
                }
                uv = clamp(uv, vec2(0.0), vec2(1.0));
            }
        }
    }
    // -------------------------------------------------------

    vec4 col = texture(tex, uv);

    // Darken the distorted region near the black hole to emphasize gravitational pull
    if (distortion_strength > 0.01)
    {
        col.rgb *= (1.0 - distortion_strength * 0.4);  // Darken by up to 40% near event horizon
    }

    // If inside event horizon, enforce pure black (no tone-mapping brightness leakage)
    if (in_event_horizon)
    {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // Add accretion disk glow around black hole for dramatic effect
    if (u_black_hole.w > 0.5)
    {
        vec4 bh_clip = u_projection_view_matrix * vec4(u_black_hole.xyz, 1.0);
        if (bh_clip.w > 0.001 && bh_clip.z > 0.0)
        {
            vec2 bh_ndc    = bh_clip.xy / bh_clip.w;
            vec2 bh_screen = (bh_ndc * 0.5 + 0.5) * u_screen;
            vec2 delta = gl_FragCoord.xy - bh_screen;
            float r = length(delta);

            const float R_E = 55.0;
            const float EVENT_HORIZON = R_E * 0.4;  // Inner black region
            const float ACCRETION_INNER = R_E * 0.5;
            const float ACCRETION_OUTER = R_E * 1.5;

            // Accretion disk: bright orange/red glow just outside event horizon
            if (r > EVENT_HORIZON && r < ACCRETION_OUTER)
            {
                // Radial falloff from inner to outer edge
                float accretion_t = (r - ACCRETION_INNER) / (ACCRETION_OUTER - ACCRETION_INNER);
                accretion_t = clamp(accretion_t, 0.0, 1.0);

                // Strongest glow just outside event horizon, fades outward
                float glow_strength = (1.0 - accretion_t) * (1.0 - accretion_t);

                // Hot accretion disk color: orange to red (simulating superheated matter)
                vec3 accretion_color = mix(
                    vec3(1.0, 0.5, 0.1),  // Orange at inner edge
                    vec3(0.8, 0.1, 0.05), // Red at outer edge
                    accretion_t
                );

                // Blend accretion disk glow with scene color
                col.rgb = mix(col.rgb, accretion_color * 2.0, glow_strength * 0.6);
            }
        }
    }

    // ---- Wormhole: Interstellar-style gravitational lensing sphere ----
    //
    // u_wormhole.xyz = world-space mouth centre.
    // u_wormhole.w   = world-space mouth radius (>0 iff active).
    //
    // The mouth sphere is also rendered as geometry with a render-to-texture
    // of the far end already composited into `tex`, so inside the silhouette
    // we only need to radially compress those pixels to give the "fisheye
    // into another patch of space" look from the Double Negative / Kip Thorne
    // Interstellar wormhole paper. Outside the silhouette we apply the same
    // inverse-square Schwarzschild deflection used by the black hole so
    // stars and scenery smear into Einstein arcs around the rim.
    if (u_wormhole.w > 0.01)
    {
        vec4 wh_clip = u_projection_view_matrix * vec4(u_wormhole.xyz, 1.0);
        if (wh_clip.w > 0.001 && wh_clip.z > 0.0)
        {
            // Project the mouth centre to pixel coordinates.
            vec2 wh_ndc    = wh_clip.xy / wh_clip.w;
            vec2 wh_screen = (wh_ndc * 0.5 + 0.5) * u_screen;

            // Project a point offset along camera-right by the world radius
            // so we get the actual on-screen silhouette radius in pixels
            // (robust to FOV, distance, and aspect).
            vec3 cam_right = vec3(u_view_matrix[0][0],
                                  u_view_matrix[1][0],
                                  u_view_matrix[2][0]);
            vec4 rim_clip = u_projection_view_matrix
                          * vec4(u_wormhole.xyz + cam_right * u_wormhole.w, 1.0);
            vec2 rim_ndc    = rim_clip.xy / max(rim_clip.w, 0.001);
            vec2 rim_screen = (rim_ndc * 0.5 + 0.5) * u_screen;
            float R_S = max(length(rim_screen - wh_screen), 8.0); // silhouette radius (px)

            vec2  delta = gl_FragCoord.xy - wh_screen;
            float r     = length(delta);
            float R_LENS_OUTER = R_S * 4.0;

            if (r > 0.1 && r < R_LENS_OUTER)
            {
                vec2  dir  = delta / max(r, 0.001);
                // Gentle static swirl — rotate the sample direction slightly
                // so light appears to flow around the mouth (frame-dragging
                // style). No time uniform available in this pass, so we
                // keep it position-independent.
                float swirl = 0.18 * exp(-pow((r - R_S) / (R_S * 1.2), 2.0));
                float cs = cos(swirl), sn = sin(swirl);
                vec2 swirl_dir = vec2(cs * dir.x - sn * dir.y,
                                      sn * dir.x + cs * dir.y);

                if (r < R_S)
                {
                    // --- Inside the mouth silhouette ---
                    // The RTT of the far end is already composited here via
                    // the rendered sphere mesh. Apply a stereographic-ish
                    // radial compression: rho = sin(0.5*pi*rho_norm).
                    // That pushes peripheral pixels toward the rim and gives
                    // the "porthole into another universe" fisheye, while
                    // leaving the centre near-linear.
                    float rn        = r / R_S;              // 0..1
                    float rn_fisheye = sin(rn * 1.5707963) * 0.98;
                    vec2  inside_pos = wh_screen + swirl_dir * (rn_fisheye * R_S);
                    vec2  inside_uv  = clamp(inside_pos / u_screen,
                                             vec2(0.0), vec2(1.0));

                    // Chromatic aberration: sample R/G/B at slightly offset
                    // radii so the rim picks up a wavelength-dependent smear.
                    float ca = 0.012 * (rn * rn);
                    vec2 uv_r = clamp((wh_screen + swirl_dir * (rn_fisheye * R_S * (1.0 + ca))) / u_screen, vec2(0.0), vec2(1.0));
                    vec2 uv_b = clamp((wh_screen + swirl_dir * (rn_fisheye * R_S * (1.0 - ca))) / u_screen, vec2(0.0), vec2(1.0));
                    vec3 portal_col = vec3(
                        texture(tex, uv_r).r,
                        texture(tex, inside_uv).g,
                        texture(tex, uv_b).b);

                    // Rim darkening — the further out, the more light has to
                    // climb out of the throat. Boosts the Einstein ring pop.
                    float rim_darken = mix(1.0, 0.65, smoothstep(0.55, 1.0, rn));
                    col.rgb = portal_col * rim_darken;
                }
                else
                {
                    // --- Outside the mouth: gravitational lensing of scene ---
                    // Schwarzschild small-angle deflection: the apparent
                    // position of a background pixel is its source position
                    // plus R_E^2 / r along the radial outward direction.
                    // Equivalently, sampling the scene at (r - R_E^2/r) in
                    // the direction from the mouth centre gives the lensed
                    // view. We clamp so rays that would pass through the
                    // throat land on the rim instead of black.
                    float R_E  = R_S * 1.05;                // Einstein ring ~at silhouette
                    float r_src = r - (R_E * R_E) / r;
                    r_src = max(r_src, R_S * 0.02);

                    // Chromatic aberration increasing toward the rim.
                    float ring_dist = abs(r - R_S) / R_S;
                    float ca_out    = 2.0 / max(ring_dist + 0.15, 0.15);
                    vec2  base_pos  = wh_screen + swirl_dir * r_src;
                    vec2  lens_uv   = clamp(base_pos / u_screen,
                                            vec2(0.0), vec2(1.0));
                    vec2  uv_r      = clamp((base_pos + dir * ca_out) / u_screen,
                                            vec2(0.0), vec2(1.0));
                    vec2  uv_b      = clamp((base_pos - dir * ca_out) / u_screen,
                                            vec2(0.0), vec2(1.0));
                    vec3 lens_col = vec3(
                        texture(tex, uv_r).r,
                        texture(tex, lens_uv).g,
                        texture(tex, uv_b).b);

                    // Fade back to the direct scene at the outer working
                    // radius so the effect has a graceful edge.
                    float ring_blend = 1.0 - smoothstep(R_S * 1.0, R_LENS_OUTER, r);
                    col.rgb = mix(col.rgb, lens_col, ring_blend);
                }

                // --- Einstein ring ---
                // Peak brightness exactly at the silhouette, with a soft
                // Gaussian on each side. Slight cyan-to-white gradient to
                // cue "space folded" without looking like a neon hoop.
                float ring_sigma = R_S * 0.12;
                float ring_n     = (r - R_S) / ring_sigma;
                float ring       = exp(-ring_n * ring_n);
                // Subtle azimuthal modulation so the ring isn't perfectly
                // uniform (mimics the non-uniform energy density of the
                // Interstellar renders).
                float theta  = atan(dir.y, dir.x);
                float mod_az = 0.85 + 0.15 * cos(theta * 2.0);
                vec3 ring_col = mix(vec3(0.55, 0.85, 1.00),
                                    vec3(1.00, 0.95, 1.00),
                                    0.5);
                col.rgb += ring_col * (ring * mod_az * 1.6);
            }
        }
    }
    // -----------------------------------------------------------------

    vec3 eyedir = vec3(uv * 2.0 - 1.0, 1.0);
    vec4 tmp = (u_inverse_projection_matrix * vec4(eyedir, 1.0));
    tmp /= tmp.w;
    eyedir = normalize((u_inverse_view_matrix * vec4(tmp.xyz, 0.0)).xyz);

    col.xyz = applyDopplerShift(col.xyz, eyedir);

    // Uncharted2 tonemap with Auria's custom coefficients
    vec4 perChannel = (col * (6.9 * col + .5)) / (col * (5.2 * col + 1.7) + 0.06);
    vec2 inside = uv - 0.5;
    float vignette = 1. - dot(inside, inside) * vignette_weight;
    vignette = clamp(pow(vignette, 0.8), 0., 1.);

    FragColor = vec4(perChannel.xyz * vignette, col.a);
}
