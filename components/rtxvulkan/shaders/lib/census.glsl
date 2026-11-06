#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_CENSUS_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_CENSUS_GLSL

// The census of stores that were not finite, `Census` in `counts.h`.
//
// **Compiled in or out, `RTX_CENSUS`, and never specialized away**: every shader is built twice
// (`rtx_compile_shader`), and only the harness's set counts.

#ifndef RTX_CENSUS
#error "every shader is compiled with RTX_CENSUS 0 or 1"
#endif

#include "shared/counts.h"
#include "shared/sets.h"

#include "finite.glsl"

#if RTX_CENSUS

/// This module's word of `Census::mNotFinite`: its place among the census set's modules.
layout(constant_id = SPEC_CENSUS_KERNEL) const uint CENSUS_KERNEL = 0u;

layout(set = SET_PASS, binding = BIND_CENSUS, scalar) buffer CensusBlock
{
    Census census;
};

/// Counts `value` against this module where any component of it is a NaN or an infinity.
void countNotFinite(vec4 value)
{
    if (any(notFinite(value)))
        atomicAdd(census.mNotFinite[CENSUS_KERNEL], 1u);
}

#else

void countNotFinite(vec4 value) {}

#endif

void countNotFinite(vec3 value)
{
    countNotFinite(vec4(value, 0.0));
}

void countNotFinite(vec2 value)
{
    countNotFinite(vec4(value, 0.0, 0.0));
}

void countNotFinite(float value)
{
    countNotFinite(vec4(value, 0.0, 0.0, 0.0));
}

/// Stores the float `value` into `target` at `at`, counted first.
///
/// **A macro because an image with a format is a type of its own in SPIR-V**, and the stores are many
/// formats and three dimensions: a function would take one of them.
#define RTX_STORE_COUNTED(target, at, value)                                                                           \
    {                                                                                                                  \
        const vec4 crossing = (value);                                                                                 \
        countNotFinite(crossing);                                                                                      \
        imageStore((target), (at), crossing);                                                                          \
    }

/// Stores the words `value` into the integer image `target` at `at`, uncounted: bits and packs,
/// whose packing counted the floats it took (`packRgb9e5`). A `uvec4` local, which no float
/// converts into without a cast the line shows.
#define RTX_STORE_WORDS(target, at, value)                                                                             \
    {                                                                                                                  \
        const uvec4 words = (value);                                                                                   \
        imageStore((target), (at), words);                                                                             \
    }

#endif
