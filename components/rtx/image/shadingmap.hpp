#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

#include <osg/Vec3f>

#include "alphaimage.hpp"
#include "texturewrap.hpp"

namespace Rtx
{
    struct TextureData;

    /// An estimate of the light a texture already has painted into it: Morrowind's textures were lit before they were
    /// saved, so a ray tracer lights their corners dark twice over. The low-frequency part of a texture's brightness as
    /// a factor to divide out, normalised to average one so it never changes how much light there is, and clamped to a
    /// factor of two so that black paint stays near black. Coarse on purpose: following the detail is the
    /// over-correction that flattens a texture into a colour.
    ///
    /// **Made on the host, as a painted colour's chain is made** (`ChainBuild::build`), which divides each level by it
    /// where it stands; the texture sheet draws the same.
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

        /// How many times the grid is box blurred, three by three, before it is normalised: three passes are a close
        /// enough Gaussian, and a correction with an edge in it would put that edge into the picture. **Wrapping along
        /// an axis the texture repeats**, because a blur that clamped there would invent a gradient across every wall;
        /// **and clamped along one it clamps** (`TextureData::mWrap`), because a banner's or a flame's border cells
        /// would otherwise take the opposite edge's light.
        static constexpr std::uint32_t sBlurPasses = 3;

        /// Estimates the map from the texture's largest level, weighed by `alpha`, the texture's own
        /// (`AlphaImage`), which the caller decoded already: a chain build holds it for the build.
        ShadingMap(const TextureData& texture, const AlphaImage& alpha);

        /// `sExtent * sExtent` factors, row by row, averaging one.
        std::span<const float> getValues() const { return mValues; }

    private:
        std::array<float, sCells> mValues;
    };

    /// Where a point stands along one axis of the map: the two cells it lies between, as indices the
    /// axis's wrap gives, and how far it is from the first toward the second. Apart from the lerp, so a
    /// level's every column and row is placed once and not at each texel (`ChainBuild::delight`).
    struct PaintedAxis
    {
        std::uint32_t mLow;
        std::uint32_t mHigh;
        float mAcross;
    };

    /// `at`, from nought to one across the texture, along an axis that clamps or repeats: **wrapping
    /// along an axis the texture repeats**, because Morrowind's textures tile and a clamp would seam
    /// every wall that repeats, **and held at the edge along one it clamps**, as the estimate's blur is.
    PaintedAxis paintedAxis(float at, bool clamps);

    /// The map at a point placed along both axes, bilinear across it. Here and not in the source file: a
    /// chain asks it once a texel (`ChainBuild::delight`).
    ///
    /// @param map `ShadingMap::sExtent` squared factors, which is what `ShadingMap::getValues`
    ///        holds.
    inline float paintedLight(std::span<const float> map, const PaintedAxis& across, const PaintedAxis& down)
    {
        constexpr std::size_t extent = ShadingMap::sExtent;
        assert(map.size() == extent * extent);

        const auto cell = [&](std::uint32_t column, std::uint32_t row) { return map[row * extent + column]; };
        const float top = std::lerp(cell(across.mLow, down.mLow), cell(across.mHigh, down.mLow), across.mAcross);
        const float bottom = std::lerp(cell(across.mLow, down.mHigh), cell(across.mHigh, down.mHigh), across.mAcross);
        return std::lerp(top, bottom, down.mAcross);
    }

    /// The map at `u`, `v`, addressed past its edges as a texture of `wrap` is: the two above.
    float paintedLight(std::span<const float> map, float u, float v, TextureWrap wrap);

    /// `light`, a colour in linear light, with the painted light `factor` divided out: **held at a
    /// reflectance of one, brought down whole**, since a glint in a fold painted dark divides to past
    /// one and a surface that reflects more than reaches it makes light at every bounce, and the colour
    /// is kept by scaling all three.
    inline osg::Vec3f delit(const osg::Vec3f& light, const float factor)
    {
        const osg::Vec3f divided = light / factor;
        const float brightest = std::max(std::max(divided.x(), divided.y()), divided.z());
        return brightest > 1.0f ? divided / brightest : divided;
    }
}
