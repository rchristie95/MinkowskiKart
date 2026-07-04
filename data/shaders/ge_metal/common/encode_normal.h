// encode_normal.h - MSL port of data/shaders/utils/encode_normal.frag
// Octahedron Normal Vector encoding. Ported numerically identically; do not
// alter the math (the decode side depends on the exact wrap/sign behaviour).
#ifndef GE_METAL_ENCODE_NORMAL_H
#define GE_METAL_ENCODE_NORMAL_H

#include <metal_stdlib>
using namespace metal;

inline float2 OctWrap(float2 v)
{
    float2 w = 1.0 - abs(v.yx);
    if (v.x < 0.0) w.x = -w.x;
    if (v.y < 0.0) w.y = -w.y;
    return w;
}

inline float2 EncodeNormal(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    n.xy = n.z >= 0.0 ? n.xy : OctWrap(n.xy);
    n.xy = n.xy * 0.5 + 0.5;
    return n.xy;
}

#endif // GE_METAL_ENCODE_NORMAL_H
