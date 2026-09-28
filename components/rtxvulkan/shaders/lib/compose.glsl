#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_COMPOSE_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_COMPOSE_GLSL

// How a pixel's light is put back together, for the two places that do it: the trace, where
// nothing filters the bounce (`VisibilityConstants::mComposed`), and `composite.comp` behind a
// filter or a running sum.

/// The picture a pixel's channels make: what the trace resolved on its own, and the bounce with
/// the surface's albedo multiplied back in.
vec3 composedLight(vec3 direct, vec3 albedo, vec3 indirect)
{
    return direct + albedo * indirect;
}

#endif
