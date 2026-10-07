#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_CENSUS_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_CENSUS_GLSL

// The census of stores that were not finite, `Census` in `counts.h`. **Every store of an image
// goes through the two macros below** (`RtxSourceTreeTest`), so a store added beside the others
// cannot cross uncounted; a float written into a buffer is counted where it is written.
//
// **Compiled in or out, `RTX_CENSUS`, and never specialized away**: every shader is built twice
// (`rtx_compile_shader`), and only the harness's set counts. The game's modules carry no test of a
// value, no binding and no constant of the census, and nor does a measured run's, whose figures are
// the game's kernels. A specialization constant folds the test away and leaves the binding: Vulkan
// holds a pipeline to every binding a module's code names, reached or not, so the game would push
// the census to every dispatch. `check` asserts the census, a test asserts on it, and `view` stops
// at a NaN.

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

/// Counts `value` against this module where any component of it is a NaN or an infinity. One word
/// for every store that crossed, so a frame carrying one froxel of NaN reads as one and a frame gone
/// black reads as the frame. A NaN is a logic error and is refused nowhere: the count is what turns
/// it into a failed check rather than a picture that goes black by itself.
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
/// **A macro because an image with a format is a type of its own in SPIR-V**, and the stores are
/// many formats and three dimensions: a function would take one of them. Substituted textually, so
/// `value` is taken once into a local and `target` may name any image.
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
