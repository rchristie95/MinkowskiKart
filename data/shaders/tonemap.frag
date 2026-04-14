uniform sampler2D tex;
uniform float vignette_weight;

out vec4 FragColor;

#stk_include "utils/getCIEXYZ.frag"
#stk_include "utils/getRGBfromCIEXxy.frag"
#stk_include "utils/relativity_color.frag"

void main()
{
    vec2 uv = gl_FragCoord.xy / u_screen;

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

            // Einstein ring radius (pixels). Governs how wide the lens halo is.
            const float R_E = 55.0;

            if (r > 0.5 && r < R_E * 6.0)
            {
                // Schwarzschild lens equation (point mass, small angle):
                //   r_source = r - R_E^2 / r
                // r_source < 0  → inside shadow / event-horizon → black
                float r_src = r - (R_E * R_E) / r;

                if (r_src <= 0.0)
                {
                    // Event horizon: all light absorbed.
                    uv = vec2(-1.0); // will clamp to black below
                }
                else
                {
                    // Remap: sample scene from the direction the photon actually came from.
                    vec2 sample_pos = bh_screen + normalize(delta) * r_src;
                    uv = sample_pos / u_screen;
                }
                uv = clamp(uv, vec2(0.0), vec2(1.0));
            }
        }
    }
    // -------------------------------------------------------

    vec4 col = texture(tex, uv);

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
