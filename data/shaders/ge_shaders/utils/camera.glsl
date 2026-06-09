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
    vec4 m_black_hole;              // [wx, wy, wz, scale]  (scale=0 = inactive)
    vec4 m_wormhole;                // [wx, wy, wz, radius] (radius=0 = inactive)
} u_camera;
