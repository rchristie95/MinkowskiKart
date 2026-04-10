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
    return displacement;
}

vec3 transformRelativisticNormal(vec3 world_normal)
{
    return normalize(world_normal);
}

vec3 boostRelativisticLightConePosition(vec3 relative_position, float ct_length)
{
    vec3 beta_vector = getRelativityBetaVector();
    float beta2 = dot(beta_vector, beta_vector);
    if (!relativityVisualsEnabled() || beta2 < 1e-6)
        return relative_position;

    float gamma = getRelativityGamma();
    float beta_dot = dot(beta_vector, relative_position);
    float parallel_scale = (gamma - 1.0) / beta2;
    return relative_position +
        (parallel_scale * beta_dot - gamma * ct_length) * beta_vector;
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
    float parallel_scale = (gamma - 1.0) / beta2;

    vec3 world_direction = normalized_direction +
        (parallel_scale * beta_dot - gamma) * beta_vector;
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

    float light_cone_ct = -sqrt(distance2);
    vec3 observer_relative =
        boostRelativisticLightConePosition(relative, light_cone_ct);
    return vec4(u_relativity_observer_pos.xyz + observer_relative, 1.0);
}
