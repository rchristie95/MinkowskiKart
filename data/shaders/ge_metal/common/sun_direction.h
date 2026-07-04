// sun_direction.h - MSL port of data/shaders/ge_shaders/utils/sun_direction.glsl
// Sun Most Representative Point (MRP area lighting, from "Frostbite going PBR").
// Ported numerically identically.
//
// transpose(inverse_view_matrix) * vec4(...) uses the same column-major matrix
// convention in MSL as GLSL, so transpose() and the matrix*vector product map
// element-for-element.
#ifndef GE_METAL_SUN_DIRECTION_H
#define GE_METAL_SUN_DIRECTION_H

#include <metal_stdlib>
using namespace metal;

inline float3 sunDirection(float3 R, float3 sun_direction,
                           float sun_angle_tan_half, float4x4 inverse_view_matrix)
{
    sun_direction = normalize((transpose(inverse_view_matrix) *
        float4(sun_direction, 0.0)).xyz);
    float DdotR = dot(sun_direction, R);
    float3 S = normalize(R - DdotR * sun_direction);
    float sun_angle_tan_half2 = 1 + sun_angle_tan_half * sun_angle_tan_half;
    float2 sun_angle_sin_cos =
        float2(2 * sun_angle_tan_half, 2 - sun_angle_tan_half2) /
        sun_angle_tan_half2;
    // Equivalent to DdotR < cos(sun_angle)
    float factor = step(DdotR, sun_angle_sin_cos.y);
    return mix(R, normalize(sun_direction * sun_angle_sin_cos.y +
        S * sun_angle_sin_cos.x), factor);
}

#endif // GE_METAL_SUN_DIRECTION_H
