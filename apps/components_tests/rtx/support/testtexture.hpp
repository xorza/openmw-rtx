#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string_view>
#include <vector>

#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureformat.hpp>

namespace Rtx::Testing
{
    /// One level over one texel, which is what most of the textures these tests build are.
    ///
    /// **A constant of static storage, because a description spans its levels rather than holding
    /// them**: a level written beside the call outlives nothing.
    inline constexpr MipLevel sOneTexel{ 0, 1, 1 };

    /// A one-texel description over `texel`, whose four bytes the caller keeps.
    inline TextureData describeTexel(std::span<const std::uint8_t> texel, TextureIndex slot = TextureIndex(0))
    {
        return TextureData{
            .mSlot = slot,
            .mFormat = TextureFormat::Rgba8Unorm,
            .mWidth = 1,
            .mHeight = 1,
            .mBytes = std::as_bytes(texel),
            .mLevels = std::span(&sOneTexel, 1),
        };
    }

    /// A texture a test paints by hand, and the storage its description spans.
    ///
    /// **Moved and never copied**: `mData` carries spans into the two vectors beside it, and a copy would
    /// read the original's buffers. A move keeps the vectors' buffers where they are.
    struct TestTexture
    {
        std::vector<std::uint8_t> mBytes;
        std::vector<MipLevel> mLevels;
        TextureData mData;

        TestTexture() = default;
        TestTexture(TestTexture&&) noexcept = default;
        TestTexture& operator=(TestTexture&&) noexcept = default;
        TestTexture(const TestTexture&) = delete;
        TestTexture& operator=(const TestTexture&) = delete;
        ~TestTexture() = default;

        /// Describes what a painter wrote, over the levels it pushed.
        ///
        /// @param format what those bytes are to be read as. Unorm by default, which is the one
        ///        with no transfer function under it and so the one a hand-computed byte belongs in.
        void describe(std::uint32_t width, std::uint32_t height, std::string_view name,
            TextureFormat format = TextureFormat::Rgba8Unorm)
        {
            mData = TextureData{
                .mFormat = format,
                .mWidth = width,
                .mHeight = height,
                .mBytes = std::as_bytes(std::span(mBytes)),
                .mLevels = mLevels,
                .mName = name,
            };
        }
    };

    /// A texture whose every mip is one flat colour — level `i` is `40 + 30i`, evenly spaced
    /// and none of them black.
    ///
    /// **The byte a ray comes back with reads out the level it sampled**, and because the levels are
    /// evenly spaced, `textureLod` blending two lands exactly on `40 + 30 * lod`: a *fractional* level
    /// is readable, so a cone's width is measurable. Flat colours make the answer independent of where
    /// the cone landed.
    inline void paintMipLadder(TestTexture& texture)
    {
        constexpr std::uint32_t extent = 64;
        constexpr std::uint32_t levels = 7;

        for (std::uint32_t level = 0; level < levels; ++level)
        {
            const std::uint32_t side = extent >> level;
            texture.mLevels.push_back(MipLevel{ static_cast<std::uint32_t>(texture.mBytes.size()), side, side });
            texture.mBytes.insert(
                texture.mBytes.end(), std::size_t{ side } * side * 4, static_cast<std::uint8_t>(40 + 30 * level));
        }

        texture.describe(extent, extent, "mip ladder");
    }

    /// Which level of `paintMipLadder` a linear sample came from, whole or fractional. The inverse
    /// of what that function paints, stated beside it so the two cannot come to disagree.
    inline float ladderLevel(float sampled)
    {
        return (sampled * 255.0f - 40.0f) / 30.0f;
    }

    /// A texture of exactly these texels at `extent` square: one level, uncompressed and not
    /// display-encoded, so what comes back out is what went in.
    ///
    /// The multi-texel counterpart of `describeTexel`, over the storage `TestTexture` holds for a
    /// description that spans rather than owns.
    inline void paintFlat(
        TestTexture& texture, std::uint32_t extent, std::span<const std::uint8_t> texels, std::string_view name)
    {
        texture.mBytes.assign(texels.begin(), texels.end());
        texture.mLevels.assign(1, MipLevel{ 0, extent, extent });
        texture.describe(extent, extent, name);
    }

    /// A texture of `levels` levels from `width` by `height` down, every byte of every level
    /// `value`: for a test that asks where a texture begins and what that costs, and not what it
    /// draws.
    inline void paintLevels(TestTexture& texture, std::uint32_t width, std::uint32_t height, std::uint32_t levels,
        std::string_view name, std::uint8_t value = 128)
    {
        texture.mBytes.clear();
        texture.mLevels.clear();
        for (std::uint32_t level = 0; level < levels; ++level)
        {
            const std::uint32_t levelWidth = std::max(width >> level, 1u);
            const std::uint32_t levelHeight = std::max(height >> level, 1u);
            texture.mLevels.push_back(
                MipLevel{ static_cast<std::uint32_t>(texture.mBytes.size()), levelWidth, levelHeight });
            texture.mBytes.insert(texture.mBytes.end(), std::size_t{ levelWidth } * levelHeight * 4, value);
        }

        texture.describe(width, height, name);
    }

    /// Adds one level of `width` by `height` to an uncompressed texture whose levels' alphas a
    /// test states outright, with colour it ignores, and describes it over every level so far.
    inline void addAlphaLevel(
        TestTexture& texture, std::uint32_t width, std::uint32_t height, std::initializer_list<std::uint8_t> alphas)
    {
        texture.mLevels.push_back(MipLevel{ static_cast<std::uint32_t>(texture.mBytes.size()), width, height });
        for (const std::uint8_t alpha : alphas)
        {
            texture.mBytes.insert(texture.mBytes.end(), 3, std::uint8_t{ 255 });
            texture.mBytes.push_back(alpha);
        }

        texture.describe(texture.mLevels.front().mWidth, texture.mLevels.front().mHeight, "alpha sheet");
    }
}
