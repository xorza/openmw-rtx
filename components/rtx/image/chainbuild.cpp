#include "chainbuild.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <mutex>

#include <rgbcx/rgbcx.h>

#include <components/rtx/shaders/halving.h>

#include "colour.hpp"
#include "texels.hpp"
#include "textureformat.hpp"

namespace Rtx
{
    namespace
    {
        /// How hard rgbcx searches a block: the level its author names for when in doubt, among the
        /// ones that match squish and NVTT, since a chain is built once and kept.
        constexpr std::uint32_t sEncodeLevel = 10;

        constexpr std::size_t sTexel = 4;

        std::byte quantise(float value)
        {
            return static_cast<std::byte>(std::clamp(std::lround(value * 255.0f), 0L, 255L));
        }

        bool isBc2(TextureFormat format)
        {
            return format == TextureFormat::Bc2Srgb || format == TextureFormat::Bc2Unorm;
        }

        bool isBc3(TextureFormat format)
        {
            return format == TextureFormat::Bc3Srgb || format == TextureFormat::Bc3Unorm;
        }

        bool isLoose(TextureFormat format)
        {
            return format == TextureFormat::Rgba8Srgb || format == TextureFormat::Rgba8Unorm
                || format == TextureFormat::Bgra8Srgb || format == TextureFormat::Bgra8Unorm;
        }

        /// rgbcx's tables, made once for the process: its `init` writes globals and is not
        /// thread-safe, which the library states, so the outside world's state is set here and once.
        void initEncoders()
        {
            static std::once_flag made;
            std::call_once(made, [] { rgbcx::init(rgbcx::bc1_approx_mode::cBC1Ideal); });
        }

        /// The sixteen texels of block `bx`, `by` of `level`, RGBA, the edge's texel standing in for a
        /// texel past a level smaller than a block, as a block-compressed file pads it.
        std::array<std::uint8_t, 64> blockAt(
            std::span<const std::byte> texels, const MipLevel& level, std::uint32_t bx, std::uint32_t by)
        {
            std::array<std::uint8_t, 64> block{};
            for (std::uint32_t y = 0; y < 4; ++y)
                for (std::uint32_t x = 0; x < 4; ++x)
                {
                    const std::uint32_t sx = std::min(bx * 4 + x, level.mWidth - 1);
                    const std::uint32_t sy = std::min(by * 4 + y, level.mHeight - 1);
                    const std::size_t from = (std::size_t{ sy } * level.mWidth + sx) * sTexel;
                    for (std::size_t channel = 0; channel < sTexel; ++channel)
                        block[(y * 4 + x) * sTexel + channel] = std::to_integer<std::uint8_t>(texels[from + channel]);
                }
            return block;
        }
    }

    bool ChainBuild::rebuilds(const TextureData& file)
    {
        const TextureFormat format = file.mFormat;
        const MipLevel& finest = file.mLevels.front();
        return std::size_t{ finest.mWidth } * finest.mHeight > 1 && file.levelsFit()
            && (isBc1(format) || isBc2(format) || isBc3(format) || isLoose(format));
    }

    void ChainBuild::build(const TextureData& finest, const std::uint32_t levels)
    {
        assert(levels >= 1 && "a chain of no level");

        // The first level alone, as the readers take a description: the levels under it are what is
        // made here, and never read.
        TextureData first = finest;
        first.mLevels = finest.mLevels.first(1);
        if (finest.mLevels.size() > 1)
            first.mBytes = finest.mBytes.first(finest.mLevels[1].mOffset);
        mAlpha.build(first);

        const MipLevel& top = first.mLevels.front();
        mLevels.clear();
        std::size_t bytes = 0;
        for (std::uint32_t level = 0; level < levels; ++level)
        {
            const MipLevel laid{ .mOffset = static_cast<std::uint32_t>(bytes),
                .mWidth = std::max(top.mWidth >> level, 1u),
                .mHeight = std::max(top.mHeight >> level, 1u) };
            mLevels.push_back(laid);
            bytes += std::size_t{ laid.mWidth } * laid.mHeight * sTexel;
        }
        mTexels.assign(bytes, std::byte{ 0 });

        // The finest level through the readers that know every format: colour a band of blocks at a
        // time, as stored, and alpha a byte a texel.
        for (std::uint32_t row = 0; row < top.mHeight; row += 4)
        {
            readTexelBand(first, top, row / 4, mBand);
            for (std::uint32_t y = row; y < row + mBand.size() / top.mWidth; ++y)
                for (std::uint32_t x = 0; x < top.mWidth; ++x)
                {
                    const osg::Vec3f& colour = mBand[std::size_t{ y - row } * top.mWidth + x];
                    const std::size_t at = mLevels[0].texelOffset(x, y, sTexel);
                    for (int channel = 0; channel < 3; ++channel)
                        mTexels[at + static_cast<std::size_t>(channel)] = quantise(colour[channel]);
                    mTexels[at + 3] = static_cast<std::byte>(mAlpha.at(0, x, y));
                }
        }

        const bool encoded = isSrgb(finest.mFormat);
        const bool coverage = finest.mEncoding == TextureEncoding::Colour;
        for (std::size_t at = 1; at < mLevels.size(); ++at)
        {
            const MipLevel& above = mLevels[at - 1];
            const MipLevel& level = mLevels[at];
            for (std::uint32_t y = 0; y < level.mHeight; ++y)
                for (std::uint32_t x = 0; x < level.mWidth; ++x)
                {
                    const Shaders::AxisTaps across = Shaders::axisTaps(x, above.mWidth);
                    const Shaders::AxisTaps down = Shaders::axisTaps(y, above.mHeight);

                    osg::Vec3f weighed;
                    osg::Vec3f even;
                    float painted = 0.0f;
                    for (const std::uint32_t dy : { 0u, 1u, 2u })
                        for (const std::uint32_t dx : { 0u, 1u, 2u })
                        {
                            const float weight = across.mWeights[dx] * down.mWeights[dy];
                            const std::uint32_t sx = std::min(across.mFirst + dx, above.mWidth - 1);
                            const std::uint32_t sy = std::min(down.mFirst + dy, above.mHeight - 1);
                            const std::size_t from = above.texelOffset(sx, sy, sTexel);
                            const auto stored = [&](std::size_t offset) {
                                return std::to_integer<std::uint8_t>(mTexels[from + offset]);
                            };

                            // Through the byte's table and not a `pow`, and alpha linear in every
                            // format, its byte's own share.
                            const auto channel = [&](std::size_t offset) {
                                return encoded ? toLinear(stored(offset)) : stored(offset) / 255.0f;
                            };

                            const osg::Vec3f texel(channel(0), channel(1), channel(2));
                            const float alpha = stored(3) / 255.0f;
                            even += texel * weight;
                            weighed += texel * (alpha * weight);
                            painted += alpha * weight;
                        }

                    const osg::Vec3f mean = coverage && painted > 0.0f ? weighed / painted : even;
                    const std::size_t into = level.texelOffset(x, y, sTexel);
                    for (int channel = 0; channel < 3; ++channel)
                        mTexels[into + static_cast<std::size_t>(channel)]
                            = quantise(encoded ? toEncoded(mean[channel]) : mean[channel]);
                    mTexels[into + 3] = quantise(painted);
                }
        }
    }

    bool ChainBuild::holdsCut(const TextureData& finest) const
    {
        if (!isBc1(finest.mFormat))
            return false;

        const MipLevel& top = mLevels.front();
        for (std::uint32_t y = 0; y < top.mHeight; ++y)
            for (std::uint32_t x = 0; x < top.mWidth; ++x)
                if (mAlpha.at(0, x, y) == 0)
                    return true;
        return false;
    }

    std::span<const std::byte> ChainBuild::getTexels(std::uint32_t level) const
    {
        const MipLevel& at = mLevels[level];
        return std::span(mTexels).subspan(at.mOffset, std::size_t{ at.mWidth } * at.mHeight * sTexel);
    }

    void ChainBuild::encode(const TextureData& file, std::vector<std::byte>& into) const
    {
        assert(mLevels.size() >= file.mLevels.size() && "a chain encoded for a file it was not built of");
        initEncoders();

        const TextureFormat format = file.mFormat;
        for (std::uint32_t at = 1; at < getLevelCount(); ++at)
        {
            const MipLevel& level = mLevels[at];
            const std::span<const std::byte> texels = getTexels(at);

            if (isLoose(format))
            {
                const std::size_t start = into.size();
                into.insert(into.end(), texels.begin(), texels.end());
                if (isBgr(format))
                    for (std::size_t texel = start; texel < into.size(); texel += sTexel)
                        std::swap(into[texel], into[texel + 2]);
                continue;
            }

            const std::uint32_t across = (level.mWidth + 3) / 4;
            const std::uint32_t down = (level.mHeight + 3) / 4;
            const std::size_t blockBytes = isBc1(format) ? 8 : 16;
            const std::size_t start = into.size();
            into.resize(start + std::size_t{ across } * down * blockBytes);
            for (std::uint32_t by = 0; by < down; ++by)
                for (std::uint32_t bx = 0; bx < across; ++bx)
                {
                    const std::array<std::uint8_t, 64> block = blockAt(texels, level, bx, by);
                    std::byte* const target = into.data() + start + (std::size_t{ by } * across + bx) * blockBytes;

                    if (isBc1(format))
                        rgbcx::encode_bc1(sEncodeLevel, target, block.data(), true, false);
                    else if (isBc3(format))
                        rgbcx::encode_bc3(sEncodeLevel, target, block.data());
                    else
                    {
                        // BC2's alpha is sixteen nibbles, texel by texel, the low one first; its
                        // colour half a BC1 block that is always of four colours.
                        for (std::size_t texel = 0; texel < 16; ++texel)
                        {
                            const auto nibble
                                = static_cast<std::uint32_t>((block[texel * sTexel + 3] * 15u + 127u) / 255u);
                            target[texel / 2] |= static_cast<std::byte>(nibble << (4 * (texel % 2)));
                        }
                        rgbcx::encode_bc1(sEncodeLevel, target + 8, block.data(), false, false);
                    }
                }
        }
    }
}
