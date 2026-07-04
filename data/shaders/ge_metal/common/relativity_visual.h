// relativity_visual.h  (MSL port of data/shaders/utils/relativity_visual.vert)
//
// Terrell-Penrose retarded-position + relativistic aberration used by the
// vertex stage to displace world-space geometry into the observer frame. This
// is the signature visual effect, so the math here is a byte-for-byte port of
// the GLSL: same branch guards, same epsilons, same operation order. GLSL
// inversesqrt() is spelled rsqrt() in MSL (identical semantics). The only
// structural change from GLSL is that the camera UBO is passed explicitly as
// `constant CameraBuffer& u_camera` instead of being a global block; the
// field aliases in relativity_bridge.h make the bodies read identically.
//
// Include order:
//     #include "../shared/relativity_bridge.h"   // CameraBuffer + u_* aliases
//     #include "../common/relativity_visual.h"

#ifndef GE_METAL_RELATIVITY_VISUAL_H
#define GE_METAL_RELATIVITY_VISUAL_H

#include <metal_stdlib>
using namespace metal;

// ---------------------------------------------------------------------------
// Accessors (mirror the GLSL helpers of the same name)
// ---------------------------------------------------------------------------
inline bool relativityVisualsEnabled(constant CameraBuffer& u_camera)
{
    return u_relativity_params.x > 0.5;
}

inline float3 getRelativityBetaVector(constant CameraBuffer& u_camera)
{
    return u_relativity_beta.xyz;
}

inline float getRelativityCLight(constant CameraBuffer& u_camera)
{
    return max(u_relativity_beta.w, 0.0);
}

inline float getRelativityGamma(constant CameraBuffer& u_camera)
{
    return max(u_relativity_params.z, 1.0);
}

inline float getRelativityInverseGamma(constant CameraBuffer& u_camera)
{
    float inv_gamma = u_relativity_params.w;
    return inv_gamma > 0.0 ? inv_gamma : 1.0;
}

// ---------------------------------------------------------------------------
// getRelativisticVisualFade
//
// GLSL had two overloads sharing a name; MSL cannot overload on a trailing
// float alone without ambiguity against the 2-arg form when a default is used,
// so the disable-flag form is named explicitly. Behaviour is identical.
// ---------------------------------------------------------------------------
inline float getRelativisticVisualFade(constant CameraBuffer& u_camera,
                                       float3 world_position,
                                       float3 object_velocity)
{
    if (!relativityVisualsEnabled(u_camera))
        return 0.0;
    return 1.0;
}

inline float getRelativisticVisualFade(constant CameraBuffer& u_camera,
                                       float3 world_position,
                                       float3 object_velocity,
                                       float disable_relativity_visual)
{
    if (disable_relativity_visual > 0.5)
        return 0.0;

    return getRelativisticVisualFade(u_camera, world_position, object_velocity);
}

// NOTE: this pipeline intentionally applies no explicit Lorentz contraction
// to positions, normals or tangents. What a camera photographs is fully
// described by retarded (emission-time) positions plus aberration of the
// incoming ray directions (Terrell-Penrose); coordinate contraction is a
// simultaneity statement and adding it on top double-counts the effect.
// Light transport happens in the world frame, so lighting keeps world normals.

// ---------------------------------------------------------------------------
// worldDirectionToObserverDirection: relativistic aberration of a unit ray
// ---------------------------------------------------------------------------
inline float3 worldDirectionToObserverDirection(
    constant CameraBuffer& u_camera, float3 world_direction)
{
    float3 beta_vector = getRelativityBetaVector(u_camera);
    float beta2 = dot(beta_vector, beta_vector);
    if (!relativityVisualsEnabled(u_camera) || beta2 < 1e-6)
        return world_direction;

    float gamma = getRelativityGamma(u_camera);
    float beta_dot = dot(beta_vector, world_direction);
    float denominator = 1.0 + beta_dot;
    if (abs(denominator) < 1e-5)
        return world_direction;

    float3 observer_direction =
        world_direction / gamma +
        (((gamma / (gamma + 1.0)) * beta_dot) + 1.0) * beta_vector;

    observer_direction /= denominator;
    float dir_length2 = dot(observer_direction, observer_direction);
    if (dir_length2 < 1e-8)
        return world_direction;
    return observer_direction * rsqrt(dir_length2);
}

// ---------------------------------------------------------------------------
// transformObserverRayToWorldDirection: inverse aberration (observer -> world)
// ---------------------------------------------------------------------------
inline float3 transformObserverRayToWorldDirection(
    constant CameraBuffer& u_camera, float3 observer_direction)
{
    float observer_length2 = dot(observer_direction, observer_direction);
    if (observer_length2 < 1e-8)
        return observer_direction;

    float3 beta_vector = getRelativityBetaVector(u_camera);
    float beta2 = dot(beta_vector, beta_vector);
    if (!relativityVisualsEnabled(u_camera) || beta2 < 1e-6)
        return observer_direction * rsqrt(observer_length2);

    float3 normalized_direction =
        observer_direction * rsqrt(observer_length2);
    float gamma = getRelativityGamma(u_camera);
    float beta_dot = dot(beta_vector, normalized_direction);
    float denominator = 1.0 - beta_dot;
    if (abs(denominator) < 1e-5)
        return normalized_direction;

    float3 world_direction =
        normalized_direction / gamma +
        (((gamma / (gamma + 1.0)) * beta_dot) - 1.0) * beta_vector;
    world_direction /= denominator;
    float world_length2 = dot(world_direction, world_direction);
    if (world_length2 < 1e-8)
        return normalized_direction;
    return world_direction * rsqrt(world_length2);
}

// ---------------------------------------------------------------------------
// getRelativisticEmissionRelativePosition: retarded (emission-time) position
//
// Solves the light-cone quadratic for the emission time offset and shifts the
// relative position back along the object's velocity to where it was when the
// now-arriving light left it.
// ---------------------------------------------------------------------------
inline float3 getRelativisticEmissionRelativePosition(
    constant CameraBuffer& u_camera, float3 relative,
    float3 object_velocity)
{
    float c_light = getRelativityCLight(u_camera);
    if (!relativityVisualsEnabled(u_camera) || c_light <= 1e-6)
        return relative;

    float speed2 = dot(object_velocity, object_velocity);
    if (speed2 <= 1e-8)
        return relative;

    float c2 = c_light * c_light;
    float a = speed2 - c2;
    if (abs(a) < 1e-6)
        return relative;

    float b = dot(relative, object_velocity);
    float c = dot(relative, relative);
    float discriminant = b * b - a * c;
    if (discriminant < 0.0)
        return relative;

    float emission_dt = (-b + sqrt(discriminant)) / a;
    if (emission_dt > 0.0 || emission_dt < -1000.0)
        return relative;

    return relative + object_velocity * emission_dt;
}

// ---------------------------------------------------------------------------
// applyRelativisticVisualPosition: full retarded-position + aberration warp
// ---------------------------------------------------------------------------
inline float4 applyRelativisticVisualPosition(
    constant CameraBuffer& u_camera, float4 world_position,
    float3 object_velocity, float visual_fade)
{
    if (!relativityVisualsEnabled(u_camera) || visual_fade <= 1e-4)
        return world_position;

    float3 relative = world_position.xyz - u_relativity_observer_pos.xyz;
    float distance2 = dot(relative, relative);
    if (distance2 < 1e-6)
        return world_position;

    relative = getRelativisticEmissionRelativePosition(u_camera, relative,
        object_velocity);
    distance2 = dot(relative, relative);
    if (distance2 < 1e-6)
        return float4(u_relativity_observer_pos.xyz, 1.0);

    float distance = sqrt(distance2);
    float3 world_direction = relative / distance;
    float3 observer_direction =
        worldDirectionToObserverDirection(u_camera, world_direction);
    float3 observer_relative = observer_direction * distance;
    float3 blended_relative = mix(relative, observer_relative,
        clamp(visual_fade, 0.0, 1.0));
    return float4(u_relativity_observer_pos.xyz + blended_relative, 1.0);
}

// Convenience overload matching the GLSL zero-velocity default.
inline float4 applyRelativisticVisualPosition(
    constant CameraBuffer& u_camera, float4 world_position)
{
    return applyRelativisticVisualPosition(u_camera, world_position,
        float3(0.0), 1.0);
}

#endif // GE_METAL_RELATIVITY_VISUAL_H
