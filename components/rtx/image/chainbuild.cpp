#include "chainbuild.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <mutex>

#include <rgbcx/bc7enc.h>
#include <rgbcx/rgbcx.h>

#include <components/rtx/shaders/halving.h>

#include "colour.hpp"
#include "shadingmap.hpp"
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
            std::call_once(made, [] {
                rgbcx::init(rgbcx::bc1_approx_mode::cBC1Ideal);
                bc7enc_compress_block_init();
            });
        }

        /// The direction a decoded normal points, and straight up where it points none: a texel of
        /// (0.5, 0.5, 0.5), or a box of normals that cancel.
        osg::Vec3f unitNormal(const osg::Vec3f& decoded)
        {
            const float length = decoded.length();
            return length > 0.0f ? decoded / length : osg::Vec3f(0.0f, 0.0f, 1.0f);
        }

        /// A unit normal's component as the byte `2 c − 1` decodes back to it.
        std::byte quantiseNormal(float component)
        {
            return quantise((component + 1.0f) * 0.5f);
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
        const bool normalBc5 = format == TextureFormat::Bc5Unorm && file.mEncoding == TextureEncoding::Normal;
        return std::size_t{ finest.mWidth } * finest.mHeight > 1 && file.levelsFit()
            && (isBc1(format) || isBc2(format) || isBc3(format) || isLoose(format) || normalBc5);
    }

    void ChainBuild::build(const TextureData& file, const std::uint32_t levels)
    {
        assert(levels >= 1 && "a chain of no level");

        // **The levels read, as the readers take a description**: a painted colour's own levels, each
        // delit where it stands, and every other chain's finest alone, the levels under it what is made
        // here and never read.
        const std::uint32_t read = file.mEncoding == TextureEncoding::Painted
            ? std::min(levels, static_cast<std::uint32_t>(file.mLevels.size()))
            : 1u;
        TextureData first = file;
        first.mLevels = file.mLevels.first(read);
        first.mBytes = file.mBytes.first(file.mLevels[read - 1].mOffset + file.bytesOf(read - 1).size());
        first.mCoarser = {};
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

        if (file.mEncoding == TextureEncoding::Normal)
        {
            buildNormals(first);
            return;
        }

        // The levels read through the readers that know every format: colour a band of blocks at a
        // time, as stored, and alpha a byte a texel.
        for (std::uint32_t at = 0; at < read; ++at)
        {
            const MipLevel& level = first.mLevels[at];
            for (std::uint32_t row = 0; row < level.mHeight; row += 4)
            {
                readTexelBand(first, level, row / 4, mBand);
                for (std::uint32_t y = row; y < row + mBand.size() / level.mWidth; ++y)
                    for (std::uint32_t x = 0; x < level.mWidth; ++x)
                    {
                        const osg::Vec3f& colour = mBand[std::size_t{ y - row } * level.mWidth + x];
                        const std::size_t into = mLevels[at].texelOffset(x, y, sTexel);
                        for (int channel = 0; channel < 3; ++channel)
                            mTexels[into + static_cast<std::size_t>(channel)] = quantise(colour[channel]);
                        mTexels[into + 3] = static_cast<std::byte>(mAlpha.at(at, x, y));
                    }
            }
        }

        if (file.mEncoding == TextureEncoding::Painted)
            delight(first);

        const bool encoded = isSrgb(file.mFormat);
        const bool coverage = isColour(file.mEncoding);
        for (std::size_t at = read; at < mLevels.size(); ++at)
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

    void ChainBuild::delight(const TextureData& read)
    {
        assert(isSrgb(read.mFormat) && "a painted colour that is not display-encoded");

        // **The light painted in, estimated off the finest level** (`ShadingMap`), and divided out of
        // every level read at its own texels' centres, in linear light.
        const ShadingMap map(read);
        for (std::size_t at = 0; at < read.mLevels.size(); ++at)
        {
            const MipLevel& level = mLevels[at];
            for (std::uint32_t y = 0; y < level.mHeight; ++y)
                for (std::uint32_t x = 0; x < level.mWidth; ++x)
                {
                    const float factor = paintedLight(map.getValues(), (static_cast<float>(x) + 0.5f) / level.mWidth,
                        (static_cast<float>(y) + 0.5f) / level.mHeight, read.mWrap);
                    const std::size_t into = level.texelOffset(x, y, sTexel);
                    osg::Vec3f light;
                    for (int channel = 0; channel < 3; ++channel)
                        light[channel] = toLinear(
                            std::to_integer<std::uint8_t>(mTexels[into + static_cast<std::size_t>(channel)]));
                    const osg::Vec3f divided = Rtx::delit(light, factor);
                    for (int channel = 0; channel < 3; ++channel)
                        mTexels[into + static_cast<std::size_t>(channel)] = quantise(toEncoded(divided[channel]));
                }
        }
    }

    void ChainBuild::buildNormals(const TextureData& first)
    {
        const MipLevel& top = first.mLevels.front();
        mMeans.assign(mTexels.size() / sTexel, osg::Vec3f());
        const auto meanAt = [&](const MipLevel& level, std::uint32_t x, std::uint32_t y) -> osg::Vec3f& {
            return mMeans[level.texelOffset(x, y, sTexel) / sTexel];
        };

        // **Every texel of the finest level its unit normal**: three channels decoded as `2 c − 1`, and a
        // map of two its blue raised from the other two, as a sampler of the map would have.
        if (first.mFormat == TextureFormat::Bc5Unorm)
        {
            const TexelLayout layout = layoutOf(first.mFormat);
            for (std::uint32_t by = 0; by < (top.mHeight + 3) / 4; ++by)
                for (std::uint32_t bx = 0; bx < (top.mWidth + 3) / 4; ++bx)
                {
                    std::array<std::uint8_t, 64> block{};
                    rgbcx::unpack_bc5(first.mBytes.data() + top.blockOffset(bx, by, layout.mBytes), block.data());
                    for (std::uint32_t y = by * 4; y < std::min(by * 4 + 4, top.mHeight); ++y)
                        for (std::uint32_t x = bx * 4; x < std::min(bx * 4 + 4, top.mWidth); ++x)
                        {
                            const std::size_t at = (std::size_t{ y - by * 4 } * 4 + (x - bx * 4)) * sTexel;
                            const float across = block[at] / 255.0f * 2.0f - 1.0f;
                            const float along = block[at + 1] / 255.0f * 2.0f - 1.0f;
                            const float up = std::sqrt(std::max(1.0f - across * across - along * along, 0.0f));
                            meanAt(top, x, y) = unitNormal(osg::Vec3f(across, along, up));
                        }
                }
        }
        else
            for (std::uint32_t row = 0; row < top.mHeight; row += 4)
            {
                readTexelBand(first, top, row / 4, mBand);
                for (std::uint32_t y = row; y < row + mBand.size() / top.mWidth; ++y)
                    for (std::uint32_t x = 0; x < top.mWidth; ++x)
                    {
                        const osg::Vec3f& stored = mBand[std::size_t{ y - row } * top.mWidth + x];
                        meanAt(top, x, y) = unitNormal(stored * 2.0f - osg::Vec3f(1.0f, 1.0f, 1.0f));
                    }
            }

        // **Each level the box of the unit normals under it, stored as its direction**: the means are
        // carried unnormalised, so a level is the mean of the finest texels it covers and not a mean of
        // means each rounded to a byte and made unit again. The height beside them a plain box.
        for (std::size_t at = 0; at < mLevels.size(); ++at)
        {
            const MipLevel& level = mLevels[at];
            for (std::uint32_t y = 0; y < level.mHeight; ++y)
                for (std::uint32_t x = 0; x < level.mWidth; ++x)
                {
                    float height = 0.0f;
                    if (at == 0)
                        height = mAlpha.at(0, x, y) / 255.0f;
                    else
                    {
                        const MipLevel& above = mLevels[at - 1];
                        const Shaders::AxisTaps across = Shaders::axisTaps(x, above.mWidth);
                        const Shaders::AxisTaps down = Shaders::axisTaps(y, above.mHeight);
                        osg::Vec3f mean;
                        for (const std::uint32_t dy : { 0u, 1u, 2u })
                            for (const std::uint32_t dx : { 0u, 1u, 2u })
                            {
                                const float weight = across.mWeights[dx] * down.mWeights[dy];
                                const std::uint32_t sx = std::min(across.mFirst + dx, above.mWidth - 1);
                                const std::uint32_t sy = std::min(down.mFirst + dy, above.mHeight - 1);
                                mean += meanAt(above, sx, sy) * weight;
                                height += std::to_integer<std::uint8_t>(mTexels[above.texelOffset(sx, sy, sTexel) + 3])
                                    / 255.0f * weight;
                            }
                        meanAt(level, x, y) = mean;
                    }

                    const osg::Vec3f direction = unitNormal(meanAt(level, x, y));
                    const std::size_t into = level.texelOffset(x, y, sTexel);
                    for (int channel = 0; channel < 3; ++channel)
                        mTexels[into + static_cast<std::size_t>(channel)] = quantiseNormal(direction[channel]);
                    mTexels[into + 3] = quantise(height);
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

    void ChainBuild::layOwnLevels(const TextureData& file)
    {
        assert(isLoose(file.mFormat) && "own levels laid of a file whose bytes are not its texels");
        mLevels.clear();
        mTexels.clear();
        for (std::uint32_t at = 0; at < file.mLevels.size(); ++at)
        {
            const MipLevel& level = file.mLevels[at];
            mLevels.push_back(MipLevel{ .mOffset = static_cast<std::uint32_t>(mTexels.size()),
                .mWidth = level.mWidth,
                .mHeight = level.mHeight });
            const std::span<const std::byte> texels = file.bytesOf(at);
            const std::size_t start = mTexels.size();
            mTexels.insert(mTexels.end(), texels.begin(), texels.end());
            if (isBgr(file.mFormat))
                for (std::size_t texel = start; texel < mTexels.size(); texel += sTexel)
                    std::swap(mTexels[texel], mTexels[texel + 2]);
        }
    }

    void ChainBuild::encodeBc7(const TextureEncoding encoding, std::vector<std::byte>& into) const
    {
        initEncoders();

        bc7enc_compress_block_params params;
        bc7enc_compress_block_params_init(&params);
        if (!isSrgb(bc7FormatOf(encoding)))
            bc7enc_compress_block_params_init_linear_weights(&params);

        const TexelLayout layout = layoutOf(bc7FormatOf(encoding));
        for (std::uint32_t at = 0; at < getLevelCount(); ++at)
        {
            const MipLevel& level = mLevels[at];
            const std::span<const std::byte> texels = getTexels(at);
            const std::uint32_t across = (level.mWidth + layout.mSide - 1) / layout.mSide;
            const std::uint32_t down = (level.mHeight + layout.mSide - 1) / layout.mSide;
            const std::size_t start = into.size();
            into.resize(start + layout.levelBytes(level.mWidth, level.mHeight));
            for (std::uint32_t by = 0; by < down; ++by)
                for (std::uint32_t bx = 0; bx < across; ++bx)
                {
                    const std::array<std::uint8_t, 64> block = blockAt(texels, level, bx, by);

                    // **A normal map's block in one mode**, as DirectXTex narrows its search by mode
                    // (`TEX_COMPRESS_BC7_QUICK`): mode 5 where it holds a height, whose alpha it keeps
                    // apart from the normal, and mode 6 where it holds none, which bc7enc tries first for
                    // an opaque block. Five times the full search's speed, a height's error less and a
                    // normal's a tenth of a degree more on the mean (`docs/rtx/architecture.md`, "A normal map
                    // is whole in BC7, in one encoding"). bc7enc tells the kinds apart by the same test,
                    // and asserts a mode it can take for each.
                    if (encoding == TextureEncoding::Normal)
                    {
                        bool height = false;
                        for (std::size_t texel = 0; texel < 16; ++texel)
                            height = height || block[texel * sTexel + 3] < 255;
                        params.m_mode_mask = height ? 1u << 5 : 1u << 6;
                    }
                    bc7enc_compress_block(
                        into.data() + start + (std::size_t{ by } * across + bx) * layout.mBytes, block.data(), &params);
                }
        }
    }

    std::span<const std::byte> ChainBuild::getTexels(std::uint32_t level) const
    {
        const MipLevel& at = mLevels[level];
        return std::span(mTexels).subspan(at.mOffset, std::size_t{ at.mWidth } * at.mHeight * sTexel);
    }

    void ChainBuild::encode(const TextureData& file, const std::uint32_t from, std::vector<std::byte>& into) const
    {
        assert(mLevels.size() >= file.mLevels.size() && "a chain encoded for a file it was not built of");
        initEncoders();

        const TextureFormat format = file.mFormat;
        for (std::uint32_t at = from; at < getLevelCount(); ++at)
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
