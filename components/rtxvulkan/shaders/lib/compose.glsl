#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_COMPOSE_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_COMPOSE_GLSL

// How a pixel's light is put back together, for the two places that do it: the trace, where
// nothing filters the bounce (`VisibilityConstants::mComposed`), and `composite.comp` behind a
// filter or a running sum.

/// The picture a pixel's channels make: what the trace resolved on its own, the bounce with the
/// surface's albedo multiplied back in, the sky's source by how much of it got through, and the
/// lobe's light.
///
/// @param shadow the ray's own bit where nothing filters it, and the shadow denoiser's answer
///        where something does.
vec3 composedLight(vec3 direct, vec3 albedo, vec3 indirect, vec3 sunlit, float shadow, vec3 specular)
{
    return direct + albedo * indirect + sunlit * shadow + specular;
}

#endif
