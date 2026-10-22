#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_RESOLVE_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_RESOLVE_GLSL

// A pixel's light resolved from the channels behind a filter, for the two modules that do it:
// `composite.comp`, and the cascade's last level where it composes (`atrouscompose.comp`). The
// including module declares the images by the names below, where its own bindings put them.

#include "shared/composite.h"
#include "shared/shadow.h"
#include "colour.h"
#include "gbuffer.h"
#include "look.h"

#include "compose.glsl"
#include "stagerecord.glsl"

/// The picture at the pixel stored at `stored`: `directLight`, `CHANNEL_DIRECT`'s, with the bounce
/// and its fill as the filter left them multiplied back by their albedos, each shadowed source by
/// what got through — the shadow denoiser's answer where its field ran (`COMPOSITE_SHADOWED_*` in
/// `shadowedFields`), the ray's own bit where it did not — the lobe's light where a surface can
/// have one (`lobed`), and the layers'.
vec3 resolvedLight(ivec2 stored, vec3 directLight, vec3 bounce, vec3 fillShare, uint shadowedFields, uint lobed)
{
    const vec4 sky = imageLoad(shadowed, stored);
    const vec4 lamps = imageLoad(lamped, stored);
    const float skySeen = (shadowedFields & COMPOSITE_SHADOWED_SKY) != 0u ? uintBitsToFloat(imageLoad(shadow, stored).r)
                                                                          : float(shadowOpen(sky.a));
    const float lampSeen = (shadowedFields & COMPOSITE_SHADOWED_LAMPS) != 0u
        ? uintBitsToFloat(imageLoad(lampShadow, stored).r)
        : float(shadowOpen(lamps.a));
    const vec3 lobe = lobed != 0u ? imageLoad(specular, stored).rgb * imageLoad(specularAlbedo, stored).rgb : vec3(0.0);
    return composedLight(directLight, imageLoad(albedo, stored).rgb, bounce, imageLoad(ambientAlbedo, stored).rgb,
        fillShare,
        sky.rgb, skySeen, lamps.rgb, lampSeen, lobe, imageLoad(paneAlbedo, stored).rgb, imageLoad(pane, stored).rgb);
}

/// Writes what the upscaler is told its motion vector does not describe at the pixel `at`, whose
/// record is stored at `stored` (`CHANNEL_UPSCALE_MASKS`): the layers' light, which moves as the
/// nearest layer does, against `light`, the whole pixel's as this pass composed it; and the water's
/// share, which the trace measured. **Against the filtered light and not the trace's**, which a
/// mask the upscaler reads to decide how far to trust its history has to be: FSR's guide asks for
/// one stable from frame to frame, and one sample's light is not.
void storeUpscaleMasks(ivec2 at, ivec2 stored, vec3 light)
{
    const StageRecord record = unpackStageRecord(imageLoad(stageRecord, stored));
    const float lit = max(dot(light, LUMINANCE_WEIGHTS), 1e-6);
    const float paneMisMoved = min(record.mPaneOwn / lit, 1.0) * record.mPaneApart;
    RTX_STORE_COUNTED(upscaleMasks, at,
        vec4(min(vec2(paneMisMoved, record.mWaterMisMoved), vec2(UPSCALE_MASK_CEILING)), 0.0, 0.0))
}

#endif
