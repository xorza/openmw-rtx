#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_FOOTPRINT_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_FOOTPRINT_GLSL

// Whether a sampler can still see a field of a given scale.
//
// A field finer than the sampler looking at it is not detail, it is noise dressed as detail. `footprint`
// is whatever is doing the looking, here a ray cone against a wavelength; see `coneAt`.
//
// Only for a field with no mip chain to climb: `waveLevel` and `fogFieldAt` make this argument in the
// sampler by picking the level whose texels are as wide as the cone. What is left is `rainSlope`'s rings.

/// How much of something that long a sampler this wide can still tell apart, from none of it to all.
float resolved(float wavelength, float footprint)
{
    return 1.0 - smoothstep(0.25 * wavelength, 0.75 * wavelength, footprint);
}

#endif
