layout (input_attachment_index = 0, binding = 0) uniform subpassInput u_hdr;

layout(location = 0) out vec4 o_color;

#include "utils/camera.glsl"
#include "utils/relativity_bridge.glsl"
#include "utils/constants_utils.glsl"
#include "../utils/relativity_color.frag"

void main()
{
    vec3 hdr = convertColor(subpassLoad(u_hdr).xyz);

    // Apply Doppler colour shift when relativistic visuals are active.
    // Compute the world-space view direction for this pixel from NDC.
    vec2 ndc = (gl_FragCoord.xy / u_screen) * 2.0 - vec2(1.0);
    vec4 world_far = u_camera.m_inverse_projection_view_matrix *
                     vec4(ndc, 1.0, 1.0);
    vec3 view_dir = normalize(world_far.xyz / world_far.w
                              - u_relativity_observer_pos.xyz);

    o_color = vec4(applyDopplerShift(hdr, view_dir), 1.0);
}
