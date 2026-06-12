layout(std140, set = 1, binding = 0) uniform CameraBuffer
{
    mat4 m_view_matrix;
    mat4 m_projection_matrix;
    mat4 m_inverse_view_matrix;
    mat4 m_inverse_projection_matrix;
    mat4 m_projection_view_matrix;
    mat4 m_inverse_projection_view_matrix;
    vec4 m_viewport;
    vec2 m_screensize;
    vec2 m_padding;
    // Relativistic visual parameters (match GEVulkanCameraUBO / buildRelativityUBOTail)
    vec4 m_relativity_params;       // [item_active, doppler_active, gamma, inv_gamma]
    vec4 m_relativity_beta;         // [bx, by, bz, c_light]
    vec4 m_relativity_observer_pos; // [ox, oy, oz, scanner_active]
    vec4 m_relativity_bubble;       // [bubble.xyz, warp_radius]
    vec4 m_black_holes[4];          // [wx, wy, wz, radius] (radius=0 = slot inactive)
    vec4 m_wormhole;                // [wx, wy, wz, radius] (radius=0 = inactive)
    // Screen-space post effect parameters (displace_color.frag)
    mat4 m_previous_pv_matrix;      // previous frame projection*view
    vec4 m_motion_blur;             // [boost_amount, center_x, center_y, mask_radius]
    vec4 m_compactification;        // [strength, 0, 0, 0]
    // Track god rays / light shafts (matches the SP/OpenGL renderGodRays sun)
    vec4 m_godrays_pos;             // [x, y, z, opacity] (opacity=0 = inactive)
    vec4 m_godrays_color;           // [r, g, b, world_radius]
    // Post effect toggles mirroring the SP/OpenGL advanced pipeline options
    vec4 m_postfx_flags;            // [bloom, ssao, dof, antialias]
    // Sun shadow mapping: world position -> shadow atlas [uv.xy, depth01],
    // near cascade then far cascade
    mat4 m_sun_shadow_matrix;
    vec4 m_shadow_params;           // [depth range (0=off), pcss, texel, penumbra]
    // Second post effect toggle block: [glow, scatter_density, 0, 0]
    vec4 m_postfx_flags2;
    mat4 m_sun_shadow_matrix_far;
    vec4 m_shadow_params_far;       // [depth range, split distance, texel, penumbra]
    // Post-processing style knobs: [exposure, saturation, vignette, sharpness]
    vec4 m_beauty_params;
} u_camera;
