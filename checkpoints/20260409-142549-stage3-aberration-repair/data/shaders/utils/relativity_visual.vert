bool relativityVisualsEnabled()
{
    return u_relativity_params.x > 0.5;
}

vec3 getRelativityBetaVector()
{
    return u_relativity_beta.xyz;
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

vec3 contractRelativisticDisplacement(vec3 displacement)
{
    vec3 beta_vector = getRelativityBetaVector();
    float beta2 = dot(beta_vector, beta_vector);
    if (!relativityVisualsEnabled() || beta2 < 1e-6)
        return displacement;

    vec3 beta_direction = normalize(beta_vector);
    vec3 parallel = beta_direction * dot(displacement, beta_direction);
    vec3 perpendicular = displacement - parallel;
    return perpendicular + parallel * getRelativityInverseGamma();
}

vec3 transformRelativisticNormal(vec3 world_normal)
{
    vec3 beta_vector = getRelativityBetaVector();
    float beta2 = dot(beta_vector, beta_vector);
    if (!relativityVisualsEnabled() || beta2 < 1e-6)
        return normalize(world_normal);

    vec3 beta_direction = normalize(beta_vector);
    vec3 parallel = beta_direction * dot(world_normal, beta_direction);
    vec3 perpendicular = world_normal - parallel;
    return normalize(perpendicular + parallel * getRelativityGamma());
}

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
    if (!relativityVisualsEnabled())
        return world_position;

    vec3 relative = world_position.xyz - u_relativity_observer_pos.xyz;
    float distance2 = dot(relative, relative);
    if (distance2 < 1e-6)
        return world_position;

    float distance = sqrt(distance2);
    vec3 world_direction = relative / distance;
    vec3 observer_direction =
        worldDirectionToObserverDirection(world_direction);
    vec3 observer_relative = observer_direction * distance;
    return vec4(u_relativity_observer_pos.xyz + observer_relative, 1.0);
}
