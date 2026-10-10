#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_RESOLVE_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_RESOLVE_GLSL

// A pixel's light resolved from the channels behind a filter, for the two modules that do it:
// `composite.comp`, and the cascade's last level where it composes (`atrouscompose.comp`). The
// including module declares the images by the names below, where its own bindings put them.

#include "shared/composite.h"
#include "shared/shadow.h"

#include "compose.glsl"

/// The picture at `at`: `directLight`, `CHANNEL_DIRECT`'s, with the bounce and its fill as the
/// filter left them multiplied back by their albedos, each shadowed source by what got through —
/// the shadow denoiser's answer where its field ran (`COMPOSITE_SHADOWED_*` in `shadowedFields`),
/// the ray's own bit where it did not — the lobe's light where a surface can have one (`lobed`),
/// and the layers'.
vec3 resolvedLight(ivec2 at, vec3 directLight, vec3 bounce, vec3 fillShare, uint shadowedFields, uint lobed)
{
    const vec4 sky = imageLoad(shadowed, at);
    const vec4 lamps = imageLoad(lamped, at);
    const float skySeen = (shadowedFields & COMPOSITE_SHADOWED_SKY) != 0u ? uintBitsToFloat(imageLoad(shadow, at).r)
                                                                          : float(shadowOpen(sky.a));
    const float lampSeen = (shadowedFields & COMPOSITE_SHADOWED_LAMPS) != 0u
        ? uintBitsToFloat(imageLoad(lampShadow, at).r)
        : float(shadowOpen(lamps.a));
    const vec3 lobe = lobed != 0u ? imageLoad(specular, at).rgb * imageLoad(specularAlbedo, at).rgb : vec3(0.0);
    return composedLight(directLight, imageLoad(albedo, at).rgb, bounce, imageLoad(ambientAlbedo, at).rgb, fillShare,
        sky.rgb, skySeen, lamps.rgb, lampSeen, lobe, imageLoad(paneAlbedo, at).rgb, imageLoad(pane, at).rgb);
}

#endif
