// relativity_bridge.glsl
//
// Maps the SP-pipeline standalone uniform names used by the shared relativity
// utility files (data/shaders/utils/relativity_visual.vert and
// data/shaders/utils/relativity_color.frag) to the corresponding fields of the
// GE CameraBuffer uniform block.
//
// Include camera.glsl BEFORE this file, then include the shared relativity
// files after it, e.g.:
//
//   #include "utils/camera.glsl"
//   #include "utils/relativity_bridge.glsl"
//   #include "../utils/relativity_visual.vert"

#define u_relativity_params       u_camera.m_relativity_params
#define u_relativity_beta         u_camera.m_relativity_beta
#define u_relativity_observer_pos u_camera.m_relativity_observer_pos
#define u_relativity_bubble       u_camera.m_relativity_bubble
#define u_black_holes             u_camera.m_black_holes
#define u_wormhole                u_camera.m_wormhole

// Matrix aliases used by tonemap/post-process relativity shaders
#define u_projection_view_matrix  u_camera.m_projection_view_matrix
#define u_view_matrix             u_camera.m_view_matrix

// Screen-size alias used by relativity_color.frag (dopplerScannerRadius)
#define u_screen                  u_camera.m_screensize
