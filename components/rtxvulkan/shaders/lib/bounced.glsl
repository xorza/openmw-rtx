#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_BOUNCED_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_BOUNCED_GLSL

// What the bounce's stages write of what a pixel's bounce brought back: the bounce's own where its
// ray met nothing (`bounce.comp`), and the landing's where it met a surface (`landing.comp`).

#include "shared/traceconstants.h"
#include "gbuffer.h"
#include "visibility.h"

#include "bindings.glsl"
#include "census.glsl"
#include "compose.glsl"
#include "frame.glsl"
#include "pixels.glsl"
#include "shading.glsl"
#include "stage.glsl"

/// Writes what arrived along `pixel`'s bounce into the indirect light, the fill and the lobe's share
/// of it; and the upscaler's masks, which weigh a part of the pixel against the whole of its light.
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

    // **What the upscaler is told its motion vector does not describe**, `CHANNEL_UPSCALE_MASKS`:
    // the layers' light, which moves as the nearest layer does, against the whole pixel's, which
    // the bounce is a part of; and the water's share, which the trace measured.
    const vec4 sky = imageLoad(shadowed, channelAt(CHANNEL_SHADOWED, at, width, swap));
    const vec4 lamps = imageLoad(lamped, channelAt(CHANNEL_LAMPED, at, width, swap));
    const vec3 pixelLight = composedLight(imageLoad(direct, channelAt(CHANNEL_DIRECT, at, width, swap)).rgb,
        imageLoad(albedo, channelAt(CHANNEL_ALBEDO, at, width, swap)).rgb, indirectLight,
        imageLoad(ambientAlbedo, channelAt(CHANNEL_AMBIENT_ALBEDO, at, width, swap)).rgb, filled, sky.rgb,
        float(shadowOpen(sky.a)), lamps.rgb, float(shadowOpen(lamps.a)),
        HAS_MAPS ? imageLoad(specular, channelAt(CHANNEL_SPECULAR, at, width, swap)).rgb
                * imageLoad(specularAlbedo, channelAt(CHANNEL_SPECULAR_ALBEDO, at, width, swap)).rgb
                 : vec3(0.0),
        imageLoad(paneAlbedo, channelAt(CHANNEL_PANE_ALBEDO, at, width, swap)).rgb,
        imageLoad(pane, channelAt(CHANNEL_PANE, at, width, swap)).rgb);
    const float pixelLit = max(dot(pixelLight, LUMINANCE_WEIGHTS), 1e-6);
    const float paneMisMoved = min(record.mPaneOwn / pixelLit, 1.0) * record.mPaneApart;
    RTX_STORE_COUNTED(upscaleMasks, channelAt(CHANNEL_UPSCALE_MASKS, at, width, swap),
        vec4(min(vec2(paneMisMoved, record.mWaterMisMoved), vec2(UPSCALE_MASK_CEILING)), 0.0, 0.0))
}

#endif
