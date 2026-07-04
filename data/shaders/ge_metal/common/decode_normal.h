// decode_normal.h - MSL port of data/shaders/utils/decodeNormal.frag
// Octahedron Normal Vector decoding. Ported numerically identically; must stay
// the exact inverse of EncodeNormal in encode_normal.h.
#ifndef GE_METAL_DECODE_NORMAL_H
#define GE_METAL_DECODE_NORMAL_H

#include <metal_stdlib>
using namespace metal;

inline float3 DecodeNormal(float2 n)
{
    n = n * 2.0 - 1.0;
    float3 ret = float3(n.x, n.y, 1.0 - abs(n.x) - abs(n.y));
    float t = max(-ret.z, 0.0);
    ret.x += ret.x >= 0.0 ? -t : t;
    ret.y += ret.y >= 0.0 ? -t : t;
    return normalize(ret);
}

#endif // GE_METAL_DECODE_NORMAL_H
