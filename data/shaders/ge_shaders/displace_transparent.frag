layout(location = 0) in vec4 f_vertex_color;
layout(location = 1) in vec2 f_uv;
layout(location = 3) flat in int f_material_id;

layout(location = 0) out vec4 o_color;

layout(push_constant) uniform Constants
{
    vec4 m_displace_direction;
} u_push_constants;

#include "utils/camera.glsl"
#include "utils/constants_utils.glsl"
#include "utils/sample_mesh_texture.glsl"
#include "../utils/displace_utils.frag"

layout (set = 3, binding = 0) uniform sampler2D u_displace_mask;
// NOTE(macOS/MoltenVK): u_displace_ssr (set=3,binding=1) flattens to Metal
// sampler index 16, exceeding Metal's hard limit of 16 samplers (indices
// 0-15), which makes vkCreateGraphicsPipelines("displace") fail. The material
// texture array already reserves 0-14 and u_displace_mask takes 15, so the SSR
// reflection sampler cannot fit. Drop it here; the displacement still renders,
// only the extra SSR reflection blend is skipped on Metal.
#if !defined(GE_DISABLE_DISPLACE_SSR)
layout (set = 3, binding = 1) uniform sampler2D u_displace_ssr;
#endif

void main()
{
#ifdef PBR_ENABLED
    vec4 color = sampleMeshTexture0(f_material_id, f_uv) * f_vertex_color;
    vec3 mixed_color = color.xyz;
    float alpha = color.w;
    mixed_color = convertColor(mixed_color);
    if (u_ssr)
    {
        float alpha = sampleMeshTexture0(f_material_id, f_uv).a;
        if (alpha == 0.0)
        {
            o_color = vec4(mixed_color * alpha, alpha);
            return;
        }
        float horiz = sampleMeshTexture2(f_material_id, f_uv + u_push_constants.m_displace_direction.xy * 150.).x;
        float vert = sampleMeshTexture2(f_material_id, (f_uv.yx + u_push_constants.m_displace_direction.zw * 150.) * vec2(0.9)).x;
        vec2 shift = getDisplaceShift(horiz, vert);
        ivec2 uv = getDisplaceUV(shift, u_camera.m_viewport, u_displace_mask);
#if defined(GE_DISABLE_DISPLACE_SSR)
        // No SSR sampler available (Metal sampler-limit workaround): emit the
        // displaced color without the screen-space reflection blend.
        o_color = vec4(mixed_color * alpha, alpha);
#else
        vec3 reflection = texelFetch(u_displace_ssr, uv, 0).xyz;
        o_color = vec4(mixed_color * alpha * 0.5 + reflection * alpha * 0.5 ,
            alpha);
#endif
    }
    else
    {
        o_color = vec4(mixed_color * alpha, alpha);
    }
#endif
}
