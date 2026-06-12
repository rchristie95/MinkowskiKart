// ge_glow.frag
//
// Writes flat per-object glow silhouettes into the glow attachment of the
// displace mask pass. displace_color.frag blurs them and composites the
// outline, mirroring the SP/OpenGL glow chain (renderGlow + glow.frag).
//
// The glow attachment is the last colour attachment of the pass and its
// index depends on whether the SSR attachment exists, so the value is
// written to every location; the pipeline's per-attachment write masks make
// sure only the glow attachment is stored.

layout(location = 0) flat in vec4 f_glow_color;

layout(location = 0) out vec4 o_color0;
layout(location = 1) out vec4 o_color1;
layout(location = 2) out vec4 o_color2;

void main()
{
    if (f_glow_color.w < 0.5)
        discard;
    vec4 glow = vec4(f_glow_color.rgb, 1.0);
    o_color0 = glow;
    o_color1 = glow;
    o_color2 = glow;
}
