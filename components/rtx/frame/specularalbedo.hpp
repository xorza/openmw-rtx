#pragma once

#include <array>
#include <cstddef>

#include <osg/Vec2f>

#include <components/rtx/shaders/brdf.h>

namespace Rtx
{
    /// How many values `sSpecularAlbedo` holds: two a node of a `SPECULAR_TABLE_SIZE` square.
    inline constexpr std::size_t sSpecularAlbedoValues
        = std::size_t{ Shaders::SPECULAR_TABLE_SIZE } * Shaders::SPECULAR_TABLE_SIZE * 2;

    /// The GGX lobe of `shaders/brdf.h` integrated over the hemisphere into a `SPECULAR_TABLE_SIZE` square
    /// of nodes over the square root of the cosine to the eye and the perceptual roughness. Two integrals a
    /// node: `∫ w D V (n.l) dl` with `w` Schlick's weight, and `∫ D V (n.l) dl`, the pair one lookup reads
    /// together. `specularAlbedoOf` makes a surface's directional albedo out of the pair for any
    /// reflectance, and `specularCompensation` its scale for the energy one scattering event loses.
    ///
    /// **Integrated rather than fitted.** The fits in use (Karis's, the split-sum fit of Ray Tracing Gems
    /// ch. 32) assume another masking term, and a compensation off a lobe the shader does not draw puts back
    /// energy it did not lose. Made by the build (`makeSpecularAlbedo`) and compiled in, so no start spends
    /// the 16.8 million draws it takes.
    extern const std::array<float, sSpecularAlbedoValues> sSpecularAlbedo;

    /// What the shader's lookup answers at `cosine` and `roughness`: the four nodes around the point
    /// blended, in the square root of the cosine. Read by the tests and by nothing else, which hold the
    /// device to it.
    osg::Vec2f specularAlbedoAt(float cosine, float roughness);
}
