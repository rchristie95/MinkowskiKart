bool relativityVisualsEnabled()
{
    return u_relativity_params.x > 0.5;
}

vec4 applyRelativisticVisualPosition(vec4 world_position, vec3 object_velocity,
                                     float visual_fade);

float getRelativisticVisualFade(vec3 world_position, vec3 object_velocity)
{
    if (!relativityVisualsEnabled())
        return 0.0;
    return 1.0;
}

float getRelativisticVisualFade(vec3 world_position, vec3 object_velocity,
                                float disable_relativity_visual)
{
    if (disable_relativity_visual > 0.5)
        return 0.0;

    return getRelativisticVisualFade(world_position, object_velocity);
}

vec3 getRelativityBetaVector()
{
    return u_relativity_beta.xyz;
}

float getRelativityCLight()
{
    return max(u_relativity_beta.w, 0.0);
}

float getRelativityGamma()
{
    return max(u_relativity_params.z, 1.0);
}

float getRelativityInverseGamma()
{
    float inv_gamma = u_relativity_params.w;
    return inv_gamma > 0.0 ? inv_gamma : 1.0;
}

// NOTE: this pipeline intentionally applies no explicit Lorentz contraction
// to positions, normals or tangents. What a camera photographs is fully
// described by retarded (emission-time) positions plus aberration of the
// incoming ray directions (Terrell-Penrose); coordinate contraction is a
// simultaneity statement and adding it on top double-counts the effect.
// Light transport happens in the world frame, so lighting keeps world normals.

vec3 worldDirectionToObserverDirection(vec3 world_direction)
{
    vec3 beta_vector = getRelativityBetaVector();
    float beta2 = dot(beta_vector, beta_vector);
    if (!relativityVisualsEnabled() || beta2 < 1e-6)
        return world_direction;

    float gamma = getRelativityGamma();
    float beta_dot = dot(beta_vector, world_direction);
    float denominator = 1.0 + beta_dot;
    if (abs(denominator) < 1e-5)
        return world_direction;

    vec3 observer_direction =
        world_direction / gamma +
        (((gamma / (gamma + 1.0)) * beta_dot) + 1.0) * beta_vector;

    observer_direction /= denominator;
    float dir_length2 = dot(observer_direction, observer_direction);
    if (dir_length2 < 1e-8)
        return world_direction;
    return observer_direction * inversesqrt(dir_length2);
}

vec3 transformObserverRayToWorldDirection(vec3 observer_direction)
{
    float observer_length2 = dot(observer_direction, observer_direction);
    if (observer_length2 < 1e-8)
        return observer_direction;

    vec3 beta_vector = getRelativityBetaVector();
    float beta2 = dot(beta_vector, beta_vector);
    if (!relativityVisualsEnabled() || beta2 < 1e-6)
        return observer_direction * inversesqrt(observer_length2);

    vec3 normalized_direction =
        observer_direction * inversesqrt(observer_length2);
    float gamma = getRelativityGamma();
    float beta_dot = dot(beta_vector, normalized_direction);
    float denominator = 1.0 - beta_dot;
    if (abs(denominator) < 1e-5)
        return normalized_direction;

    vec3 world_direction =
        normalized_direction / gamma +
        (((gamma / (gamma + 1.0)) * beta_dot) - 1.0) * beta_vector;
    world_direction /= denominator;
    float world_length2 = dot(world_direction, world_direction);
    if (world_length2 < 1e-8)
        return normalized_direction;
    return world_direction * inversesqrt(world_length2);
}

vec4 applyRelativisticVisualPosition(vec4 world_position)
{
    return applyRelativisticVisualPosition(world_position, vec3(0.0), 1.0);
}

vec3 getRelativisticEmissionRelativePosition(vec3 relative,
                                             vec3 object_velocity)
{
    float c_light = getRelativityCLight();
    if (!relativityVisualsEnabled() || c_light <= 1e-6)
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

    // The equation a*t^2 + 2*b*t + c = 0 has two roots; one retarded (past
    // emission) and one advanced (future). The physical retarded time is the
    // largest negative root — the most recent past emission event. Consider
    // both roots, matching the CPU implementation in relativity_math.cpp.
    float sqrt_disc = sqrt(discriminant);
    float root0 = (-b + sqrt_disc) / a;
    float root1 = (-b - sqrt_disc) / a;
    bool valid0 = root0 <= 0.0 && root0 >= -1000.0;
    bool valid1 = root1 <= 0.0 && root1 >= -1000.0;
    if (!valid0 && !valid1)
        return relative;
    float emission_dt = valid0 && valid1 ?
        max(root0, root1) : (valid0 ? root0 : root1);
    return relative + object_velocity * emission_dt;
}

vec4 applyRelativisticVisualPosition(vec4 world_position, vec3 object_velocity,
                                     float visual_fade)
{
    if (!relativityVisualsEnabled() || visual_fade <= 1e-4)
        return world_position;

    vec3 relative = world_position.xyz - u_relativity_observer_pos.xyz;
    float distance2 = dot(relative, relative);
    if (distance2 < 1e-6)
        return world_position;

    relative = getRelativisticEmissionRelativePosition(relative,
        object_velocity);
    distance2 = dot(relative, relative);
    if (distance2 < 1e-6)
        return vec4(u_relativity_observer_pos.xyz, 1.0);

    float distance = sqrt(distance2);
    vec3 world_direction = relative / distance;
    vec3 observer_direction =
        worldDirectionToObserverDirection(world_direction);
    vec3 observer_relative = observer_direction * distance;
    vec3 blended_relative = mix(relative, observer_relative,
        clamp(visual_fade, 0.0, 1.0));
    return vec4(u_relativity_observer_pos.xyz + blended_relative, 1.0);
}
