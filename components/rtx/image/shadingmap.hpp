#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <osg/Vec3f>

#include "texturewrap.hpp"

namespace Rtx
{
    struct TextureData;

    /// An estimate of the light a texture already has painted into it. Morrowind's textures were
    /// lit before they were saved, so a ray tracer lights their corners dark twice over. This is
    /// the low-frequency part of a texture's brightness as a factor to divide out: normalised to
    /// average one, so it moves light around and never changes how much there is, and clamped to
    /// a factor of two, so paint that is black stays near black. Coarse on purpose, because
    /// painted lighting varies slowly and painted detail does not, and following the detail is the
    /// over-correction that flattens a texture into a colour.
    ///
    /// **Made on the host, as a painted colour's chain is made** (`ChainBuild::build`), which
    /// divides each level by it where it stands; the texture sheet draws the same.
    class ShadingMap
    {
    public:
        /// Cells along each edge of the grid the estimate is made on. Coarse on purpose: painted lighting
        /// varies slowly across a surface and painted detail does not, so a grid this size follows the
        /// first and cannot follow the second.
        static constexpr std::uint32_t sExtent = 32;
        static constexpr std::size_t sCells = std::size_t{ sExtent } * sExtent;

        /// How far the correction may reach, either way.
        static constexpr float sFloor = 0.5f;
        static constexpr float sCeiling = 2.0f;

        /// How many times the grid is box blurred, three by three, before it is normalised: three passes
        /// are a close enough Gaussian for anything this coarse, and a correction with an edge in it
        /// would put that edge into the picture. **Wrapping along an axis the texture repeats**, because
        /// Morrowind's textures tile and a blur that clamped there would invent a gradient across every
        /// wall; **and clamped along one it clamps** (`TextureData::mWrap`), because that is an unwrapped
        /// picture — a banner, a flame — whose border cells took the opposite edge's light and divided
        /// real albedo by it.
        static constexpr std::uint32_t sBlurPasses = 3;

        /// A map that changes nothing, which is what a texture that would not load has to get: a
        /// shader reading a missing one reads the array's stand-in, which is how every untextured
        /// surface in the reference implementation came to be divided by two.
        ShadingMap();

        /// Estimates the map from the texture's largest level.
        explicit ShadingMap(const TextureData& texture);

        /// `sExtent * sExtent` factors, row by row, averaging one.
        std::span<const float> getValues() const { return mValues; }

    private:
        std::array<float, sCells> mValues;
    };

    /// The map at a point, bilinear across it, addressed past its edges as its texture is: wrapping
    /// along an axis the texture repeats, because Morrowind's textures tile and a clamp would seam
    /// every wall that repeats, and held at the edge along one it clamps, as the estimate's blur is.
    ///
    /// @param map `ShadingMap::sExtent` squared factors, which is what `ShadingMap::getValues`
    ///        holds.
    float paintedLight(std::span<const float> map, float u, float v, TextureWrap wrap);

    /// `light`, a colour in linear light, with the painted light `factor` divided out: **held at a
    /// reflectance of one, brought down whole**, since a glint in a fold painted dark divides to past
    /// one and a surface that reflects more than reaches it makes light at every bounce, and the colour
    /// is kept by scaling all three.
    osg::Vec3f delit(const osg::Vec3f& light, float factor);
}
