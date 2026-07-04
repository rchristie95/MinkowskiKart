// skybox.metal - native-Metal port of data/shaders/ge_shaders/skybox.frag
// (paired with data/shaders/ge_shaders/fullscreen_quad.vert for the vertex stage).
//
// Cubemap skybox. The full-screen pass reconstructs a *view-space* view ray from
// the fullscreen-quad UV, rotates it to world space with w = 0 (so the camera
// world position drops out entirely -> stable at large world coordinates), then
// applies the inverse relativistic aberration (transformObserverRayToWorldDirection)
// so the sky swings with the observer's beta exactly like the scene geometry.
// Finally it samples the sRGB or linear skybox cubemap depending on u_deferred.
//
// Ported FAITHFULLY / numerically identically from the GLSL: same ray
// reconstruction (inverse-projection then inverse-view with w = 0), same
// aberration call, same deferred branch. No math was changed.
//
// GLSL -> MSL mapping
// ---------------------------------------------------------------------------
//   set=1 b0  CameraBuffer (u_camera)      -> constant CameraBuffer&  [[buffer(GE_MTL_BUF_CAMERA)]]
//   binding 2 samplerCube f_skybox_texture -> texturecube<float> + sampler [[texture/sampler(2)]]
//   binding 3 samplerCube f_skybox_texture_srgb -> texturecube<float> + sampler [[texture/sampler(3)]]
//   layout(constant_id=2) u_deferred       -> [[function_constant(2)]] (from ge_metal_bindings.h)
//   gl_VertexIndex                          -> [[vertex_id]]
//
// The relativity helpers (transformObserverRayToWorldDirection) live in
// common/relativity_visual.h and take the camera UBO explicitly as
// `constant CameraBuffer& u_camera`; relativity_bridge.h defines that
// CameraBuffer struct (field-for-field identical to GECameraBuffer, incl.
// m_inverse_projection_matrix + m_inverse_view_matrix that this pass reads) and
// the u_* field aliases. So we bind the same camera UBO and pass it through.

#include <metal_stdlib>
using namespace metal;

// shared/ge_metal_bindings.h supplies GE_MTL_BUF_CAMERA and the
// u_deferred [[function_constant(2)]] specialization constant. Included first so
// its include guard / constants win, matching the sibling *.metal convention.
#include "shared/ge_metal_bindings.h"
// relativity_bridge.h defines the CameraBuffer struct + u_* aliases used by the
// aberration helper; relativity_visual.h defines
// transformObserverRayToWorldDirection itself.
#include "shared/relativity_bridge.h"
#include "common/relativity_visual.h"

// The GLSL skybox binds cubemaps at binding 2 (linear) and binding 3 (sRGB).
// This is a self-contained skybox pass (not the set=2 IBL cubemaps), so keep the
// GLSL binding numbers verbatim for the texture/sampler slots.
#define GE_MTL_TEX_SKYBOX        2
#define GE_MTL_TEX_SKYBOX_SRGB   3

// ---------------------------------------------------------------------------
// Vertex stage: port of fullscreen_quad.vert.
// Emits a single oversized triangle covering the screen (3-vertex draw).
// GLSL:
//   f_uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
//   gl_Position = vec4(f_uv * 2.0 - 1.0, 1.0, 1.0);
// Metal NDC z is [0,1]; the GL far plane 1.0 maps to Metal far 1.0 unchanged
// (a full-screen skybox pass writes the far depth), so z stays 1.0. See note (3).
// ---------------------------------------------------------------------------
struct SkyboxVertexOut
{
    float4 position [[position]];
    float2 f_uv;
};

vertex SkyboxVertexOut skybox_vertex(uint vid [[vertex_id]])
{
    SkyboxVertexOut o;
    o.f_uv = float2(float((vid << 1) & 2), float(vid & 2));
    o.position = float4(o.f_uv * 2.0 - 1.0, 1.0, 1.0);
    return o;
}

// ---------------------------------------------------------------------------
// Fragment stage: port of skybox.frag.
// ---------------------------------------------------------------------------
fragment float4 skybox_main(
    SkyboxVertexOut in [[stage_in]],
    constant CameraBuffer& u_camera [[buffer(GE_MTL_BUF_CAMERA)]],
    texturecube<float> f_skybox_texture       [[texture(GE_MTL_TEX_SKYBOX)]],
    sampler            f_skybox_sampler        [[sampler(GE_MTL_TEX_SKYBOX)]],
    texturecube<float> f_skybox_texture_srgb  [[texture(GE_MTL_TEX_SKYBOX_SRGB)]],
    sampler            f_skybox_sampler_srgb   [[sampler(GE_MTL_TEX_SKYBOX_SRGB)]])
{
    // vec2 uv = 2.0f * f_uv - 1.0f;
    float2 uv = 2.0 * in.f_uv - 1.0;

    // Reconstruct the view ray exactly like the SP/OpenGL sky shader
    // (sky.frag): unproject to a *view-space* ray, then rotate it to world
    // space with w = 0 so the camera's world position drops out completely.
    // vec4 view_pos = u_camera.m_inverse_projection_matrix * vec4(uv, 1.0, 1.0);
    float4 view_pos = u_camera.m_inverse_projection_matrix * float4(uv, 1.0, 1.0);
    // vec3 dir = normalize((u_camera.m_inverse_view_matrix *
    //     vec4(view_pos.xyz / view_pos.w, 0.0)).xyz);
    float3 dir = normalize((u_camera.m_inverse_view_matrix *
        float4(view_pos.xyz / view_pos.w, 0.0)).xyz);

    // Aberrate the view ray exactly like sky.frag does.
    // dir = transformObserverRayToWorldDirection(dir);
    dir = transformObserverRayToWorldDirection(u_camera, dir);

    // if (u_deferred) o_color = texture(f_skybox_texture_srgb, dir);
    // else            o_color = texture(f_skybox_texture, dir);
    if (u_deferred)
        return f_skybox_texture_srgb.sample(f_skybox_sampler_srgb, dir);
    else
        return f_skybox_texture.sample(f_skybox_sampler, dir);
}
