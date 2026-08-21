#include "utils/camera.glsl"
#include "utils/global_light_data.glsl"
#include "utils/spm_data.glsl"
#include "../utils/get_world_location.vert"
#include "utils/relativity_bridge.glsl"
#include "../utils/relativity_visual.vert"

layout(push_constant) uniform Constants
{
    vec4 m_billboard_rotation;
    int m_fullscreen_light;
} u_push_constants;

layout(location = 0) flat out int light_idx;

const vec3 g_vertices[4] =
    vec3[]
    (
        vec3( 1.0,  1.0, 0.0),
        vec3( 1.0, -1.0, 0.0),
        vec3(-1.0,  1.0, 0.0),
        vec3(-1.0, -1.0, 0.0)
    );

void main()
{
    // Get the light index from the instance ID
    light_idx = gl_InstanceIndex + u_push_constants.m_fullscreen_light;
    LightData light = u_global_light.m_lights[light_idx];
    vec4 pos_radius = light.m_position_radius;

    // Lighting is computed at the warped light position (see pbr_light.glsl),
    // so the coverage billboard must be placed there too. Aberration
    // magnifies image space by up to 1/(1 + beta.d) behind the observer;
    // scale the quad radius by the same conservative amplification factor
    // the tessellator uses (ge_tess.tesc getWarpAmplification) so the
    // billboard still covers the whole warped influence region.
    vec3 light_pos = applyRelativisticVisualPosition(
        vec4(pos_radius.xyz, 1.0)).xyz;
    float radius = pos_radius.w;
    vec3 beta = getRelativityBetaVector();
    if (dot(beta, beta) >= 1e-6)
    {
        vec3 rel = light_pos - u_relativity_observer_pos.xyz;
        float len2 = dot(rel, rel);
        if (len2 >= 1e-6)
        {
            vec3 dir = rel * inversesqrt(len2);
            radius *= clamp(1.0 / max(1.0 + dot(beta, dir), 0.125), 1.0, 8.0);
        }
    }

    // Get camera position from inverse view matrix
    vec3 camera_pos = vec3(u_camera.m_inverse_view_matrix[3]);

    // Calculate vector from light to camera
    vec3 light_to_camera = normalize(camera_pos - light_pos);

    /* The lights which cover the whole screen have been rendered already
    // Calculate distance from light to camera
    float dist_to_camera = distance(camera_pos, pos_radius.xyz);

    // If camera is within light radius, move the billboard quad towards the
    // near plane
    if (dist_to_camera < pos_radius.w)
    {
        gl_Position = vec4(g_vertices[gl_VertexIndex], 1.0);
        return;
    }
    */

    // Move the billboard towards camera by one radius unit
    vec4 world_pos = getWorldPosition(
        light_pos + light_to_camera * radius,
        u_push_constants.m_billboard_rotation,
        vec3(radius), g_vertices[gl_VertexIndex]);
    vec4 pv = u_camera.m_projection_view_matrix * world_pos;
    if (pv.z < 0.0)
        gl_Position = vec4(pv.xy, 0.0, 1.0);
    else
        gl_Position = pv;
}
