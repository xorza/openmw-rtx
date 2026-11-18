#include "texturepass.hpp"

#include <algorithm>
#include <cassert>
#include <optional>
#include <span>

#include <osg/Image>

#include <components/resource/imageheader.hpp>
#include <components/rtx/image/alphaimage.hpp>
#include <components/rtx/image/textureformat.hpp>

namespace Rtx
{
    FinestTexels::FinestTexels(TexelScratch& scratch)
        : mScratch(scratch)
    {
    }

    FinestTexels::~FinestTexels() = default;

    void FinestTexels::describe(const osg::Image& image)
    {
        assert(!Resource::isHeader(image) && "a texture pass handed a header, whose facts its derivation holds");
        mImage = &image;
        mFinest = describeFinest(image, mScratch);
    }

    void FinestTexels::clear()
    {
        mFinest.reset();
        mImage = nullptr;
    }

    bool solidReachOf(const std::optional<TextureData>& finest, TexelScratch&)
    {
        return !finest.has_value() || reachesSolid(*finest);
    }

    MeanTexel texelMeanOf(const std::optional<TextureData>& finest, TexelScratch& scratch)
    {
        return finest.has_value() ? meanTexel(*finest, scratch) : MeanTexel();
    }

    bool solidFallOf(const std::optional<TextureData>& finest, TexelScratch&)
    {
        return finest.has_value() && fallsBelowSolid(*finest);
    }

    AlphaRange alphaSpanOf(const std::optional<TextureData>& finest, TexelScratch&)
    {
        return finest.has_value() ? alphaRange(*finest) : AlphaRange{};
    }

    SpecularLayout specularKindOf(const std::optional<TextureData>& finest, TexelScratch& scratch)
    {
        if (!finest.has_value() || !readsColour(*finest))
            return SpecularLayout::MetalRoughness;

        // **The mean of each texel's channels' spread, as stored**: a classic map's highlight is grey,
        // its three channels near equal, where a metal-roughness map holds a metalness, a roughness and
        // an occlusion apart (`docs/architecture.md`, "One specular layout, a classic map converted").
        const TextureData& texture = *finest;
        const MipLevel& level = texture.mLevels.front();
        double spread = 0.0;
        for (std::uint32_t row = 0; row < level.mHeight; row += 4)
        {
            readTexelBand(texture, level, row / 4, scratch.mColours);
            for (const osg::Vec3f& texel : scratch.mColours)
                spread += static_cast<double>(
                    std::max({ texel.x(), texel.y(), texel.z() }) - std::min({ texel.x(), texel.y(), texel.z() }));
        }
        const double texels = static_cast<double>(level.mWidth) * level.mHeight;
        return spread / texels < sClassicSpreadMost ? SpecularLayout::Classic : SpecularLayout::MetalRoughness;
    }
}
