#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_COMPOSE_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_COMPOSE_GLSL

// How a pixel's light is put back together, for the two places that do it: the trace, where
// nothing filters the bounce (`VisibilityConstants::mComposed`), and `composite.comp` behind a
// filter or a running sum.

/// The picture a pixel's channels make: what the trace resolved on its own, the bounce with the
/// surface's albedos multiplied back in, the shadowed sources by how much of them got through, the
/// lobe's light, and what was drawn for the see-through layers with theirs.
///
/// **The fill by the albedos' difference, over the whole bounce by the diffuse one**, which is
/// `albedo × (indirect - fill) + ambientAlbedo × fill` written so a surface whose two albedos are
/// one adds nought to the bounce it had: the same bits, and not the same sum rounded another way.
///
/// @param indirect,fill `CHANNEL_INDIRECT` and `CHANNEL_FILL`, or the cascade's means of both.
/// @param shadow the ray's own bit where nothing filters it, and the shadow denoiser's answer
///        where something does.
/// **`precise`, because the two places must meet to the bit**: a frame the trace composed and the
/// same frame the composite composed from a filter's first frame are one picture, and each compiler
/// fuses the sum's products where it likes otherwise — a pane's first filtered frame stood an ulp off
/// the composed one.
///
/// @param paneAlbedo,pane `CHANNEL_PANE_ALBEDO`, and `CHANNEL_PANE` or the pane filter's mean.
vec3 composedLight(vec3 direct, vec3 albedo, vec3 indirect, vec3 ambientAlbedo, vec3 fill, vec3 shadowed,
    float shadow, vec3 specular, vec3 paneAlbedo, vec3 pane)
{
    precise vec3 composed = direct + albedo * indirect + (ambientAlbedo - albedo) * fill + shadowed * shadow
        + specular + paneAlbedo * pane;
    return composed;
}

#endif
