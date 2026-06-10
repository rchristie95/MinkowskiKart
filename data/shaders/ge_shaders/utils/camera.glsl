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
} u_camera;
