#ifndef GE_METAL_SUN_MRP_H
#define GE_METAL_SUN_MRP_H

// Faithful MSL port of data/shaders/utils/SunMRP.frag.
//
// GLSL globals -> MSL parameters:
//   u_inverse_view_matrix -> float4x4 inverse_view_matrix
//   sundirection          -> float3 sundirection
//   sun_angle             -> float sun_angle

#include <metal_stdlib>
using namespace metal;

// Sun Most Representative Point (used for MRP area lighting method)
// From "Frostbite going PBR" paper
inline float3 SunMRP(float3 normal, float3 eyedir,
                     float4x4 inverse_view_matrix,
                     float3 sundirection, float sun_angle)
{
    float3 local_sundir = normalize((transpose(inverse_view_matrix) *
        float4(sundirection, 0.)).xyz);
    float3 R = reflect(-eyedir, normal);
    float angularRadius = 3.14 * sun_angle / 180.;
    float3 D = local_sundir;
    float d = cos(angularRadius);
    float r = sin(angularRadius);
    float DdotR = dot(D, R);
    float3 S = R - DdotR * D;
    return (DdotR < d) ? normalize(d * D + normalize(S) * r) : R;
}

#endif // GE_METAL_SUN_MRP_H
