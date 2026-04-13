uniform sampler2D tex;
uniform float vignette_weight;

out vec4 FragColor;

#stk_include "utils/getCIEXYZ.frag"
#stk_include "utils/getRGBfromCIEXxy.frag"
#stk_include "utils/relativity_color.frag"

void main()
{
    vec2 uv = gl_FragCoord.xy / u_screen;
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
