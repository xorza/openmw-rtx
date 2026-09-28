#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_RECORDS_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_RECORDS_GLSL

// The records that cross the frame and belong to no walk: what a surface is in the filter's and
// the composite's terms.
//
// **Their own file because the payload and the reprojection read them and nothing else of the
// walks that fill them.** Declared where they were filled, the miss shader compiled the whole of
// the water and the shading to hold a `VisibilityPayload`, and the reprojection pulled in the
// water for one struct.

#include "gbuffer.h"

/// What a shading model made of a surface, in the terms the filter and the composite read.
///
/// **Reported by whatever shaded the pixel rather than guessed after it.** The composite puts the
/// bounce back by the albedo, and the filter tells surfaces apart by the normal, so the two have to
/// describe what this renderer actually did — and only the function that did it knows. The *flat
/// quad's* normal for water is a description of a renderer nobody wrote.
struct SurfaceResponse
{
    /// The normal the shading used, which for water is the wave's and not the plane's, as the
    /// surface channel's code: `packSurfaceNormal`, once, where the shading has it whole, so the
    /// payload carries and the launch stores the one rounding the channel holds.
    float mNormal;

    /// What the diffuse half is multiplied by, and nothing else: the surface's own albedo, with
    /// none of what the path took off it between here and the eye.
    vec3 mDiffuse;
};

/// A pixel with no surface behind it: the sky, or a ray that reached nothing.
///
/// **No normal and no albedo.** Nothing reads the sky's albedo: the composite multiplies it into a
/// bounce of nought, and the filters know the sky by `SURFACE_NO_NORMAL`.
SurfaceResponse noResponse()
{
    return SurfaceResponse(SURFACE_NO_NORMAL, vec3(0.0));
}
#endif
