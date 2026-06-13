layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include "gtao_common.glsl"

float gtaoEstimate(ivec2 gid, vec2 center_px, vec3 center_pos,
                   vec3 center_normal, float noise)
{
    const int SLICE_COUNT = 3;
    const int SAMPLES_PER_SLICE = 6;
    const float PI = 3.14159265359;

    float view_depth = max(abs(center_pos.z), 0.05);
    float radius = u_pc.m_params0.x;
    float projection_scale = 0.5 * u_pc.m_viewport.w;
    float pixel_radius = clamp(radius * projection_scale / view_depth,
        2.0, 64.0);
    float occlusion = 0.0;
    float weight_sum = 0.0;

    for (int slice = 0; slice < SLICE_COUNT; slice++)
    {
        float angle = (float(slice) + noise) * PI / float(SLICE_COUNT);
        vec2 dir = vec2(cos(angle), sin(angle));
        for (int side = -1; side <= 1; side += 2)
        {
            vec2 ray_dir = dir * float(side);
            float horizon = 0.0;
            for (int sample_id = 0; sample_id < SAMPLES_PER_SLICE; sample_id++)
            {
                float step_t = (float(sample_id) + 0.65) /
                    float(SAMPLES_PER_SLICE);
                float jitter = fract(noise + float(sample_id) * 0.61803398875);
                float sample_radius = pixel_radius *
                    mix(step_t, step_t * step_t, 0.45 + 0.1 * jitter);
                ivec2 sample_px = ivec2(round(center_px +
                    ray_dir * sample_radius));
                sample_px = clampScreenPixel(sample_px);
                float raw_depth = texelFetch(u_depth, sample_px, 0).r;
                if (raw_depth >= 1.0)
                    continue;

                vec3 sample_pos = viewPosFromScreen(sample_px);
                vec3 v = sample_pos - center_pos;
                float dist2 = dot(v, v);
                float inv_len = inversesqrt(max(dist2, 1e-5));
                float dist = dist2 * inv_len;
                float range = clamp(1.0 - dist / radius, 0.0, 1.0);
                range *= range;

                // Thin-object compensation: close blockers should survive
                // the half-res path, but broad depth jumps should not haze.
                float thickness = mix(0.08, 0.55, step_t);
                float thin = exp(-max(abs(v.z) - thickness, 0.0) * 2.0);
                float ndot = dot(center_normal, v * inv_len);
                horizon = max(horizon, max(ndot - 0.08, 0.0) * range * thin);
            }
            occlusion += horizon;
            weight_sum += 1.0;
        }
    }

    float ao = 1.0 - (occlusion / max(weight_sum, 1.0)) * u_pc.m_params0.y;
    return clamp(pow(ao, 1.15), 0.0, 1.0);
}

void main()
{
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    ivec2 out_size = imageSize(u_out0);
    if (gid.x >= out_size.x || gid.y >= out_size.y)
        return;

    vec2 center_px = u_pc.m_viewport.xy + (vec2(gid) + vec2(0.5)) * 2.0;
    ivec2 screen_px = clampScreenPixel(ivec2(center_px));
    float raw_depth = texelFetch(u_depth, screen_px, 0).r;
    if (raw_depth >= 1.0)
    {
        imageStore(u_out0, gid, vec4(1.0));
        return;
    }

    vec3 center_pos = viewPosFromScreen(screen_px);
    vec3 center_normal = viewNormalFromScreen(screen_px);
    float noise = r2Noise(center_px, u_pc.m_params0.w);
    float ao = gtaoEstimate(gid, center_px, center_pos, center_normal, noise);
    imageStore(u_out0, gid, vec4(ao));
}
