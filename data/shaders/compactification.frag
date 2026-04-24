uniform sampler2D tex;
uniform float compactification_strength;

out vec4 FragColor;

void main()
{
    vec2 uv = gl_FragCoord.xy / u_screen;

    // Strip spans 1/32 on each side of centre — the middle 1/16 of the screen
    // centred on the track tangent plane (approximated as screen centre).
    const float strip_lo = 14.0 / 32.0;  // 0.4375
    const float strip_hi = 16.0 / 32.0;  // 0.5

    // Full compactification maps every output row to a source row inside the strip.
    float y_compacted = mix(strip_lo, strip_hi, uv.y);

    // Smoothly blend between identity (strength=0) and compacted (strength=1).
    float y_sample = mix(uv.y, y_compacted, compactification_strength);

    FragColor = texture(tex, vec2(uv.x, y_sample));
}
