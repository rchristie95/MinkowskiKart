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

    // ---- Wormhole: portal-style lensing (cyan/violet swirl ring) ----
    // Reuses the same screen-space Schwarzschild lens math as the black hole
    // but renders as a translucent blue/purple ring to read as a tunnel
    // rather than a crushing singularity, and it does not fully absorb light
    // at the core (stars/scene sample from the opposite side of the ring).
    if (u_wormhole.w > 0.5)
    {
        vec4 wh_clip = u_projection_view_matrix * vec4(u_wormhole.xyz, 1.0);
        if (wh_clip.w > 0.001 && wh_clip.z > 0.0)
        {
            vec2 wh_ndc    = wh_clip.xy / wh_clip.w;
            vec2 wh_screen = (wh_ndc * 0.5 + 0.5) * u_screen;

            vec2  delta = gl_FragCoord.xy - wh_screen;
            float r     = length(delta);

            const float R_E = 60.0;

            if (r > 0.5 && r < R_E * 6.0)
            {
                // Same lens equation but never a "swallowed" region: when the
                // remapped radius would go negative we mirror it so the other
                // side of the wormhole mouth is visible through the portal.
                float r_src = r - (R_E * R_E) / r;
                r_src = abs(r_src);
                vec2 sample_pos = wh_screen + normalize(delta) * r_src;
                vec2 wh_uv = clamp(sample_pos / u_screen, vec2(0.0), vec2(1.0));
                vec4 portal_col = texture(tex, wh_uv);

                // Blend portal image over the direct scene — stronger near
                // the Einstein ring, fading at the outer limit.
                float blend = 1.0 - clamp(r / (R_E * 2.5), 0.0, 1.0);
                col.rgb = mix(col.rgb, portal_col.rgb, blend * 0.75);

                // Glowing ring highlight (violet → cyan) to distinguish the
                // wormhole from the black hole's hot accretion disk.
                const float RING_INNER = R_E * 0.85;
                const float RING_OUTER = R_E * 1.35;
                if (r > RING_INNER && r < RING_OUTER)
                {
                    float ring_t = (r - RING_INNER) / (RING_OUTER - RING_INNER);
                    float ring_strength = sin(ring_t * 3.14159);
                    vec3 ring_col = mix(
                        vec3(0.35, 0.15, 0.95),  // violet inner
                        vec3(0.15, 0.85, 1.00),  // cyan outer
                        ring_t);
                    col.rgb = mix(col.rgb, ring_col * 1.8,
                                  ring_strength * 0.6);
                }
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
