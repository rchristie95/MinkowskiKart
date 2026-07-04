// get_world_location.h - MSL port of data/shaders/utils/get_world_location.vert
// Quaternion vertex transform + 10-bit vector unpack helpers for the SPM vertex
// path. Ported numerically identically.
#ifndef GE_METAL_GET_WORLD_LOCATION_H
#define GE_METAL_GET_WORLD_LOCATION_H

#include <metal_stdlib>
using namespace metal;

inline float3 rotateVector(float4 quat, float3 vec)
{
    return vec + 2.0 * cross(cross(vec, quat.xyz) + quat.w * vec, quat.xyz);
}

inline float4 getWorldPosition(float3 origin, float4 rotation, float3 scale,
                               float3 local_pos)
{
    local_pos = local_pos * scale;
    local_pos = rotateVector(rotation, local_pos);
    local_pos = local_pos + origin;
    return float4(local_pos, 1.0);
}

inline float4 convert10BitVector(float4 orig)
{
    float4 ret;
    ret.x = orig.x * 0.00195694715;
    ret.y = orig.y * 0.00195694715;
    ret.z = orig.z * 0.00195694715;
    ret.w = max(orig.w, -1.0);
    return ret;
}

#endif // GE_METAL_GET_WORLD_LOCATION_H
