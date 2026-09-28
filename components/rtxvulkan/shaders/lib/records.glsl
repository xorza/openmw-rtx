#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_RECORDS_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_RECORDS_GLSL

// The records that cross the frame and belong to no walk: what a surface is in the filter's and
// the composite's terms.
//
// **Their own file because the payload and the reprojection read them and nothing else of the
// walks that fill them.** Declared where they were filled, the miss shader compiled the whole of
// the water and the shading to hold a `VisibilityPayload`, and the reprojection pulled in the
// water for one struct.

/// What a shading model made of a surface, in the terms the filter and the composite read.
///
/// **Reported by whatever shaded the pixel rather than guessed after it.** The composite puts the
/// bounce back by the albedo, and the filter tells surfaces apart by the normal and the roughness,
/// so those three have to describe what this renderer actually did — and only the function that
/// did it knows. A constant roughness of one and the *flat quad's* normal for water is a
/// description of a renderer nobody wrote.
struct SurfaceResponse
{
    /// The normal the shading used, which for water is the wave's and not the plane's.
    vec3 mNormal;

    /// What the diffuse half is multiplied by, and nothing else: the surface's own albedo, with
    /// none of what the path took off it between here and the eye.
    vec3 mDiffuse;

    /// Nought for a mirror and one for Lambert.
    float mRoughness;
};

/// A pixel with no surface behind it: the sky, or a ray that reached nothing.
///
/// **Nought throughout.** Nothing reads the sky's albedo or roughness: the composite multiplies the
/// albedo into a bounce of nought, and the filters know the sky by its normal, which stays nought.
SurfaceResponse noResponse()
{
    return SurfaceResponse(vec3(0.0), vec3(0.0), 0.0);
}
#endif
