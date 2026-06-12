// Sun shadow atlas sampling for the deferred lighting pass. Port of the
// SP/OpenGL sunlightshadow.frag (PCF) and sunlightshadowpcss.frag (PCSS)
// with two cascades (near + far halves of a 2x1 atlas) fit by
// GEVulkanDrawCall::updateSunShadowCamera.
// Requires camera.glsl and global_light_data.glsl to be included first.

layout(set = 1, binding = 5) uniform sampler2DShadow u_sun_shadow_pcf;
layout(set = 1, binding = 6) uniform sampler2D u_sun_shadow_raw;

// PCF / blocker search offsets (Vogel disk, from sunlightshadowpcss.frag)
const vec2 SUN_SHADOW_VOGEL16[16] = vec2[](
    vec2(0.18993645671348536, 0.027087114076591513),
    vec2(-0.21261242652069953, 0.23391293246949066),
    vec2(0.04771781344140756, -0.3666840644525993),
    vec2(0.297730981239584, 0.398259878229082),
    vec2(-0.509063425827436, -0.06528681462854097),
    vec2(0.507855152944665, -0.2875976005206389),
    vec2(-0.15230616564632418, 0.6426121151781916),
    vec2(-0.30240170651828074, -0.5805072900736001),
    vec2(0.6978019230005561, 0.2771173334141519),
    vec2(-0.6990963248129052, 0.3210960724922725),
    vec2(0.3565142601623699, -0.7066415061851589),
    vec2(0.266890002328106, 0.8360191043249159),
    vec2(-0.7515861305520581, -0.41609876195815027),
    vec2(0.9102937449894895, -0.17014527555321657),
    vec2(-0.5343471434373126, 0.8058593459499529),
    vec2(-0.1133270115046468, -0.9490025827627441)
);

const vec2 SUN_SHADOW_SEARCH8[8] = vec2[](
    vec2( 0.125, -0.375),
    vec2(-0.125,  0.375),
    vec2( 0.625,  0.125),
    vec2(-0.375, -0.625),
    vec2(-0.625,  0.625),
    vec2(-0.875, -0.125),
    vec2( 0.375,  0.875),
    vec2( 0.875, -0.875)
);

float sunShadowNoise(vec2 w)
{
    const vec3 m = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(m.z * fract(dot(w, m.xy)));
}

// Samples one cascade of the shadow atlas. u_base = 0.0 (near) or 0.5 (far);
// the u axis covers half the atlas, so horizontal offsets are scaled by 0.5.
float sampleSunShadowCascade(vec3 world_pos, vec3 geo_normal, float slope,
                             bool pcss, mat4 sample_matrix, vec4 cparams,
                             float u_base)
{
    float depth_range = cparams.x;
    float texel = cparams.z;
    float penumbra_per_metre = cparams.w;
    // Normal-offset bias: push the receiver out along the geometric normal
    // by 1..11 shadow texels (world units) depending on slope.
    float texel_world = 0.02 * texel / penumbra_per_metre;
    vec4 sp = sample_matrix * vec4(world_pos +
        geo_normal * (texel_world * (1.0 + slope) * 1.5), 1.0);
    vec3 coord = sp.xyz / sp.w;
    float u_min = u_base + texel;
    float u_max = u_base + 0.5 - texel;
    if (coord.x <= u_min || coord.x >= u_max ||
        coord.y <= 0.002 || coord.y >= 0.998 ||
        coord.z <= 0.0 || coord.z >= 1.0)
        return 1.0;

    // Receiver depth bias along the sun axis, slope-scaled.
    float ref_z = coord.z -
        max((0.06 + texel_world * slope * 2.0) / depth_range, 0.0006);
    const vec2 AXIS = vec2(0.5, 1.0); // u axis covers half the atlas

    if (pcss)
    {
        // ---- PCSS (contact hardening) ----
        float angle = sunShadowNoise(gl_FragCoord.xy) * 6.2831853;
        vec2 base = vec2(cos(angle), sin(angle));
        mat2 R = mat2(base.x, base.y, -base.y, base.x);

        float search_radius = 5.0 * texel;
        float z_sum = 0.0;
        float blockers = 0.0;
        for (int i = 0; i < 8; i++)
        {
            vec2 duv = R * (SUN_SHADOW_SEARCH8[i] * search_radius) * AXIS;
            vec2 tc = vec2(clamp(coord.x + duv.x, u_min, u_max),
                coord.y + duv.y);
            float z_occ = texture(u_sun_shadow_raw, tc).x;
            if (z_occ < ref_z)
            {
                z_sum += z_occ;
                blockers += 1.0;
            }
        }
        if (blockers < 0.5)
            return 1.0;
        float separation =
            max(ref_z - z_sum / blockers, 0.0) * depth_range;
        float radius = clamp(separation * penumbra_per_metre,
            0.5 * texel, 8.0 * texel);
        float sum = 0.0;
        for (int i = 0; i < 16; i++)
        {
            vec2 duv = R * (SUN_SHADOW_VOGEL16[i] * radius) * AXIS;
            vec2 tc = vec2(clamp(coord.x + duv.x, u_min, u_max),
                coord.y + duv.y);
            sum += texture(u_sun_shadow_pcf, vec3(tc, ref_z));
        }
        return sum * (1.0 / 16.0);
    }
    else
    {
        // ---- Fixed kernel PCF (3x3 with hardware 2x2 per tap) ----
        float sum = 0.0;
        float r = 1.5 * texel;
        for (int x = -1; x <= 1; x++)
        {
            for (int y = -1; y <= 1; y++)
            {
                vec2 tc = coord.xy +
                    vec2(float(x) * 0.5, float(y)) * r;
                tc.x = clamp(tc.x, u_min, u_max);
                sum += texture(u_sun_shadow_pcf, vec3(tc, ref_z));
            }
        }
        return sum * (1.0 / 9.0);
    }
}

// world_pos / shading normal from the G-buffer; geo_normal is the geometric
// (derivative-based) normal used for slope-scaled bias, like the SP/OpenGL
// sunlightshadow nbias; view_z selects the cascade.
float getSunShadowFactor(vec3 world_pos, vec3 world_normal, vec3 geo_normal,
                         float view_z)
{
    if (u_camera.m_shadow_params.x <= 0.0)
        return 1.0;
    // The shadow atlas is rendered from unwarped world positions while
    // relativity visuals warp the visible receiver geometry. The SP/OpenGL
    // shaders disable shadows entirely whenever relativity is active, but in
    // this game that means shadows would never be seen; instead fade them
    // out with the observer's beta, so they are correct when slow and vanish
    // before the warp makes them visibly misplaced.
    float rel_fade = 0.0;
    if (u_camera.m_relativity_params.x > 0.5)
    {
        float beta = length(u_camera.m_relativity_beta.xyz);
        rel_fade = clamp((beta - 0.25) * 4.0, 0.0, 1.0);
        if (rel_fade >= 1.0)
            return 1.0;
    }

    vec3 sun_dir = u_global_light.m_sun_direction;
    // Surfaces facing away from the sun receive no direct light; skip the
    // (noisy at grazing angles) shadow test entirely.
    float ndl = dot(world_normal, sun_dir);
    if (ndl <= 0.02)
        return 1.0;
    // Slope of the geometric surface relative to the sun: tan(acos(N.L)),
    // used to scale both biases so grazing surfaces don't self-shadow.
    float geo_ndl = clamp(dot(geo_normal, sun_dir), 0.05, 1.0);
    float slope = clamp(sqrt(max(1.0 - geo_ndl * geo_ndl, 0.0)) / geo_ndl,
        0.0, 10.0);

    bool pcss = u_camera.m_shadow_params.y > 0.5;
    float split = u_camera.m_shadow_params_far.y;
    float blend_start = split * 0.8;

    float factor;
    if (view_z < blend_start)
    {
        factor = sampleSunShadowCascade(world_pos, geo_normal, slope, pcss,
            u_camera.m_sun_shadow_matrix, u_camera.m_shadow_params, 0.0);
    }
    else if (view_z < split)
    {
        float near_f = sampleSunShadowCascade(world_pos, geo_normal, slope,
            pcss, u_camera.m_sun_shadow_matrix, u_camera.m_shadow_params,
            0.0);
        float far_f = sampleSunShadowCascade(world_pos, geo_normal, slope,
            pcss, u_camera.m_sun_shadow_matrix_far,
            u_camera.m_shadow_params_far, 0.5);
        factor = mix(near_f, far_f,
            (view_z - blend_start) / max(split - blend_start, 0.001));
    }
    else
    {
        factor = sampleSunShadowCascade(world_pos, geo_normal, slope, pcss,
            u_camera.m_sun_shadow_matrix_far, u_camera.m_shadow_params_far,
            0.5);
    }
    return mix(factor, 1.0, rel_fade);
}
