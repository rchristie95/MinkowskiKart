bool relativityVisualsEnabled()
{
    return u_relativity_params.x > 0.5;
}

const float RELATIVITY_OBSERVER_STABILITY_RADIUS = 0.45;
const float RELATIVITY_OBSERVER_STABILITY_FADE_WIDTH = 0.65;

vec4 applyRelativisticVisualPosition(vec4 world_position, vec3 object_velocity,
                                     float visual_fade);

float getRelativisticVisualFade(vec3 world_position, vec3 object_velocity)
{
    if (!relativityVisualsEnabled())
        return 0.0;

    float observer_distance =
        length(world_position - u_relativity_observer_pos.xyz);
    return smoothstep(RELATIVITY_OBSERVER_STABILITY_RADIUS,
        RELATIVITY_OBSERVER_STABILITY_RADIUS +
        RELATIVITY_OBSERVER_STABILITY_FADE_WIDTH, observer_distance);
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

vec3 applyRelativisticDisplacement(vec3 displacement, float visual_fade)
{
    if (visual_fade <= 1e-4)
        return displacement;

    vec3 contracted = contractRelativisticDisplacement(displacement);
    return mix(displacement, contracted, clamp(visual_fade, 0.0, 1.0));
}

// Lorentz-contract a world-space position along the beta direction, using the
// observer position as the reference point. This is the correct reference frame
// for scene geometry (contraction is relative to the observer, not to each
// instance's arbitrary authoring anchor). Using this avoids large batched
// meshes (forests, monolithic track materials) appearing to pivot around their
// instance origin as the beta direction changes.
vec4 applyRelativisticContraction(vec4 world_position, float visual_fade)
{
    if (!relativityVisualsEnabled() || visual_fade <= 1e-4)
        return world_position;

    vec3 relative = world_position.xyz - u_relativity_observer_pos.xyz;
    vec3 contracted = contractRelativisticDisplacement(relative);
    vec3 blended = mix(relative, contracted, clamp(visual_fade, 0.0, 1.0));
    return vec4(u_relativity_observer_pos.xyz + blended, 1.0);
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

vec3 applyRelativisticNormalTransform(vec3 world_normal, float visual_fade)
{
    vec3 normalized_normal = normalize(world_normal);
    if (visual_fade <= 1e-4)
        return normalized_normal;

    vec3 transformed_normal = transformRelativisticNormal(normalized_normal);
    return normalize(mix(normalized_normal, transformed_normal,
        clamp(visual_fade, 0.0, 1.0)));
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

    float emission_dt = (-b + sqrt(discriminant)) / a;
    if (emission_dt > 0.0 || emission_dt < -1000.0)
        return relative;

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
