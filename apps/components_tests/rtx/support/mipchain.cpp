#include "mipchain.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include <osg/Vec3f>

#include <components/rtxvulkan/shaders/shared/halving.h>

#include <components/rtx/image/colour.hpp>
#include <components/rtx/image/texels.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/textureformat.hpp>

namespace Rtx::Testing
{
    namespace
    {
        std::byte quantise(float value)
        {
            return static_cast<std::byte>(std::clamp(std::lround(value * 255.0f), 0L, 255L));
        }
    }

    void MipChain::build(const TextureData& described)
    {
        mTexture.reuse();
        mEncoded = true;

        if (!described.wantsCompletedChain())
            return;

        const MipLevel& finest = described.mLevels.front();
        mEncoded = isSrgb(described.mFormat);
        const bool coverage = described.mEncoding == TextureEncoding::Colour;

        // The whole shape first, so the texels are asked for once and the levels never move.
        mTexture.openChain(finest.mWidth, finest.mHeight,
            mEncoded ? TextureFormat::Rgba8Srgb : TextureFormat::Rgba8Unorm, described.mEncoding);
        mTexture.setName(described.mName);

        const std::uint32_t width = mTexture.getWidth();
        const std::uint32_t height = mTexture.getHeight();

        // The finest level, through the readers that already know every format. Alpha is a byte
        // a texel in all of them and colour a band of blocks at a time, so nothing here knows what
        // a block is.
        mAlpha.build(described);
        for (std::uint32_t first = 0; first < height; first += 4)
        {
            readTexelBand(described, finest, first / 4, mBand);
            for (std::uint32_t y = first; y < first + mBand.size() / width; ++y)
                for (std::uint32_t x = 0; x < width; ++x)
                {
                    const osg::Vec3f& colour = mBand[std::size_t{ y - first } * width + x];
                    const std::span<std::byte, OwnedTexture::sStride> into = mTexture.at(0, x, y);

                    for (int channel = 0; channel < 3; ++channel)
                        into[static_cast<std::size_t>(channel)] = quantise(colour[channel]);

                    into[3] = static_cast<std::byte>(mAlpha.at(0, x, y));
                }
        }

        // Each level from the one above it, a colour's weighed by the alpha it carries and data's
        // even, in light and not in bytes: `mipchain.comp` says why each.
        for (std::uint32_t at = 1; at < mTexture.getShape().getLevelCount(); ++at)
        {
            const MipLevel above = mTexture.getShape().getLevel(at - 1);
            const MipLevel level = mTexture.getShape().getLevel(at);

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
                            const std::span<const std::byte, OwnedTexture::sStride> from
                                = std::as_const(mTexture).at(at - 1, sx, sy);

                            const auto stored
                                = [&](std::size_t offset) { return std::to_integer<std::uint8_t>(from[offset]); };

                            // Through the byte and not through a float divided by 255, which is
                            // the same number by a table rather than by a `pow` a texel a channel a
                            // level. Alpha is linear in every format and is the byte's own share.
                            const auto channel = [&](std::size_t offset) {
                                return mEncoded ? toLinear(stored(offset)) : stored(offset) / 255.0f;
                            };

                            const osg::Vec3f texel(channel(0), channel(1), channel(2));
                            const float alphaHere = stored(3) / 255.0f;
                            even += texel * weight;
                            weighed += texel * (alphaHere * weight);
                            painted += alphaHere * weight;
                        }

                    const osg::Vec3f mean = coverage && painted > 0.0f ? weighed / painted : even;

                    const std::span<std::byte, OwnedTexture::sStride> into = mTexture.at(at, x, y);
                    for (int channel = 0; channel < 3; ++channel)
                        into[static_cast<std::size_t>(channel)]
                            = quantise(mEncoded ? toEncoded(mean[channel]) : mean[channel]);

                    into[3] = quantise(painted);
                }
        }
    }

    TextureData MipChain::describe() const
    {
        return mTexture.describe();
    }
}
