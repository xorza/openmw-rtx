#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_BOUNCED_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_BOUNCED_GLSL

// What the bounce's stages write of what a pixel's bounce brought back: the bounce's own where its
// ray met nothing (`bounce.comp`), and the landing's where it met a surface (`landing.comp`).

#include "shared/traceconstants.h"
#include "gbuffer.h"
#include "visibility.h"

#include "bindings.glsl"
#include "census.glsl"
#include "frame.glsl"
#include "pixels.glsl"
#include "shading.glsl"
#include "stagerecord.glsl"

/// Writes what arrived along `pixel`'s bounce into the indirect light, the fill and the lobe's share of it.
///
/// @param modulation what the lobe's half is divided by (`storedBounceAt`), which only the lobe
///        brings any back to, and only off a glossy solid, whose modulation it is.
/// @param onto whether a bounce ray before this one wrote the pixel's bounce, which a later ray's
///        light adds to; the first writes it whole, in `bounce.comp` or in its landing.
void storeBounced(uvec2 pixel, StageRecord record, Arriving arriving, vec3 modulation, bool onto)
{
    const uint width = frame.mEyes.mWorld.mWidth;
    const uint swap = fieldSwapOf(frame.mFrame);
    const ivec2 at = ivec2(pixel);

    // **What the path took off the way to the solid**, as the direct stage took it off the lamps'
    // half: the bounce carries it, and the albedo carries only the surface. The layers' share as
    // well, since the layers' stage lays nothing over the bounce.
    const vec4 through = imageLoad(stageThroughput, storedAt(at, width, swap));
    const vec3 transmittance = through.rgb * through.a;
    const Bounce bounced = bounceOf(arriving, record.mBounceLobe);
    const ivec2 indirectAt = channelAt(CHANNEL_INDIRECT, at, width, swap);
    const ivec2 fillAt = channelAt(CHANNEL_FILL, at, width, swap);
    // **Written whole where nothing came before**, and not added to a nought: a negative zero plus
    // nought is a positive one, and the eye's bounce writes the bits it always wrote.
    const vec3 indirectLight = onto ? imageLoad(indirect, indirectAt).rgb + bounced.mDiffuse * transmittance
                                    : bounced.mDiffuse * transmittance;
    const vec3 filled = onto ? imageLoad(fill, fillAt).rgb + bounced.mFill * transmittance
                             : bounced.mFill * transmittance;
    RTX_STORE_COUNTED(indirect, indirectAt, vec4(indirectLight, 1.0))
    RTX_STORE_COUNTED(fill, fillAt, vec4(filled, 1.0))

    // **The lobe's half of the bounce added to the lamps' the direct stage wrote**, by what the path
    // took: the pair one modulation divides.
    const vec3 lobe = bounced.mSpecular / modulation * transmittance;
    if (HAS_MAPS && record.mKind != STAGE_NO_SOLID)
    {
        const ivec2 specularAt = channelAt(CHANNEL_SPECULAR, at, width, swap);
        const vec4 lobeSoFar = imageLoad(specular, specularAt);
        RTX_STORE_COUNTED(specular, specularAt, vec4(lobeSoFar.rgb + lobe, lobeSoFar.a))
    }
}

#endif
