layout (input_attachment_index = 0, binding = 0) uniform subpassInput u_hdr;

layout(location = 0) out vec4 o_color;

#include "utils/camera.glsl"
#include "utils/relativity_bridge.glsl"
#include "utils/constants_utils.glsl"
#include "../utils/relativity_color.frag"

// ACES-fitted filmic tonemap (Narkowicz approximation). The exposure knob
// comes from the settings (default 2.2 puts mid grey where the previous
// rational curve had it); the filmic shoulder rolls highlights off instead
// of clipping them.
vec3 filmicToneMap(vec3 x)
{
    x *= u_camera.m_beauty_params.x;
    return clamp((x * (2.51 * x + 0.03)) /
        (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

// Gentle grade: a touch of S-curve contrast plus the saturation knob.
vec3 gradeColor(vec3 c)
{
    c = mix(c, c * c * (3.0 - 2.0 * c), 0.15);
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    return clamp(mix(vec3(luma), c, u_camera.m_beauty_params.y), 0.0, 1.0);
}

void main()
{
    vec3 hdr = gradeColor(filmicToneMap(subpassLoad(u_hdr).xyz));

    // Apply Doppler colour shift when relativistic visuals are active.
    // Compute the world-space view direction for this pixel from NDC.
    vec2 ndc = (gl_FragCoord.xy / u_screen) * 2.0 - vec2(1.0);
    vec4 world_far = u_camera.m_inverse_projection_view_matrix *
                     vec4(ndc, 1.0, 1.0);
    vec3 view_dir = normalize(world_far.xyz / world_far.w
                              - u_relativity_observer_pos.xyz);

    o_color = vec4(applyDopplerShift(hdr, view_dir), 1.0);
}
