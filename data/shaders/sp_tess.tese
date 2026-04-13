layout(triangles, equal_spacing, ccw) in;

in vec3 tc_tangent[];
in vec3 tc_bitangent[];
in vec3 tc_normal[];
in vec2 tc_uv[];
in vec2 tc_uv_two[];
in vec4 tc_color[];
in vec4 tc_world_position[];
in vec3 tc_world_normal[];
in float tc_hue_change[];
in vec3 tc_velocity[];

out vec3 tangent;
out vec3 bitangent;
out vec3 normal;
out vec2 uv;
out vec2 uv_two;
out vec4 color;
out vec4 world_position;
out vec3 world_normal;
out float camdist;
out float hue_change;

#stk_include "utils/relativity_visual.vert"

void main()
{
    vec3 p0 = gl_TessCoord.x * tc_world_position[0].xyz;
    vec3 p1 = gl_TessCoord.y * tc_world_position[1].xyz;
    vec3 p2 = gl_TessCoord.z * tc_world_position[2].xyz;
    vec3 p = p0 + p1 + p2;

    vec3 n0 = gl_TessCoord.x * tc_world_normal[0];
    vec3 n1 = gl_TessCoord.y * tc_world_normal[1];
    vec3 n2 = gl_TessCoord.z * tc_world_normal[2];
    vec3 n = normalize(n0 + n1 + n2);

    vec2 u0 = gl_TessCoord.x * tc_uv[0];
    vec2 u1 = gl_TessCoord.y * tc_uv[1];
    vec2 u2 = gl_TessCoord.z * tc_uv[2];
    vec2 u = u0 + u1 + u2;

    vec2 u20 = gl_TessCoord.x * tc_uv_two[0];
    vec2 u21 = gl_TessCoord.y * tc_uv_two[1];
    vec2 u22 = gl_TessCoord.z * tc_uv_two[2];
    vec2 u2_final = u20 + u21 + u22;

    vec4 c0 = gl_TessCoord.x * tc_color[0];
    vec4 c1 = gl_TessCoord.y * tc_color[1];
    vec4 c2 = gl_TessCoord.z * tc_color[2];
    vec4 c = c0 + c1 + c2;

    vec3 t0 = gl_TessCoord.x * tc_tangent[0];
    vec3 t1 = gl_TessCoord.y * tc_tangent[1];
    vec3 t2 = gl_TessCoord.z * tc_tangent[2];
    vec3 t = normalize(t0 + t1 + t2);

    vec3 b0 = gl_TessCoord.x * tc_bitangent[0];
    vec3 b1 = gl_TessCoord.y * tc_bitangent[1];
    vec3 b2 = gl_TessCoord.z * tc_bitangent[2];
    vec3 b = normalize(b0 + b1 + b2);

    float h0 = gl_TessCoord.x * tc_hue_change[0];
    float h1 = gl_TessCoord.y * tc_hue_change[1];
    float h2 = gl_TessCoord.z * tc_hue_change[2];
    float h = h0 + h1 + h2;

    vec3 vel = tc_velocity[0]; // Velocity should be the same for all vertices of an instance

    // Apply warping here!
    float relativity_fade = getRelativisticVisualFade(p, vel);
    vec4 v_world_position = vec4(p, 1.0);
    vec3 v_world_normal = applyRelativisticNormalTransform(n, relativity_fade);
    vec3 world_tangent = applyRelativisticDisplacement(t, relativity_fade);
    v_world_position = applyRelativisticVisualPosition(v_world_position, vel, relativity_fade);

    tangent = world_tangent;
    bitangent = b; // Or re-derive from normal and tangent
    normal = v_world_normal;
    uv = u;
    uv_two = u2_final;
    color = c;
    world_position = v_world_position;
    world_normal = v_world_normal;
    camdist = length(u_view_matrix * v_world_position);
    hue_change = h;
    gl_Position = u_projection_view_matrix * v_world_position;
}
