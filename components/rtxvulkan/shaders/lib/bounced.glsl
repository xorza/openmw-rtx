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

/// Writes what arrived along `pixel`'s bounce into the indirect light, the fill and the lobe's share
/// of it. The upscaler's masks, which weigh a part of the pixel against the whole of its light, are
/// the composing pass's (`storeUpscaleMasks`), which holds the whole filtered.
///
/// @param modulation what the lobe's half is divided by (`storedBounceAt`), which only the lobe
///        brings any back to, and only off a glossy solid, whose modulation it is.
void storeBounced(uvec2 pixel, StageRecord record, Arriving arriving, vec3 modulation)
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
    const vec3 indirectLight = bounced.mDiffuse * transmittance;
    const vec3 filled = bounced.mFill * transmittance;
    RTX_STORE_COUNTED(indirect, channelAt(CHANNEL_INDIRECT, at, width, swap), vec4(indirectLight, 1.0))
    RTX_STORE_COUNTED(fill, channelAt(CHANNEL_FILL, at, width, swap), vec4(filled, 1.0))

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
