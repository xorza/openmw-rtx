#include "texturepass.hpp"

#include <cassert>
#include <cstddef>
#include <optional>
#include <span>

#include <osg/Image>

#include <components/rtx/image/alphaimage.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/preprocess/contentkey.hpp>

namespace Rtx
{
    FinestTexels::FinestTexels() = default;

    FinestTexels::~FinestTexels() = default;

    void FinestTexels::describe(const osg::Image& image)
    {
        mImage = &image;
        mFinest = describeFinest(image, mScratch);
    }

    void FinestTexels::clear()
    {
        mFinest.reset();
        mImage = nullptr;
    }

    void FinestTexels::addTo(ContentDigest& digest) const
    {
        const bool described = mFinest.has_value();
        digest.add(described);
        if (!described)
            return;

        const TextureData& finest = *mFinest;
        const MipLevel& level = finest.mLevels.front();
        digest.add(finest.mFormat);
        digest.add(finest.mEncoding);
        digest.add(level.mWidth);
        digest.add(level.mHeight);

        assert(finest.levelsFit() && "a key of a description short of its bytes");
        digest.add(
            finest.mBytes.subspan(level.mOffset, layoutOf(finest.mFormat).levelBytes(level.mWidth, level.mHeight)));
    }

    bool solidReachOf(const std::optional<TextureData>& finest, TexelScratch&)
    {
        return !finest.has_value() || reachesSolid(*finest);
    }

    MeanTexel texelMeanOf(const std::optional<TextureData>& finest, TexelScratch& scratch)
    {
        return finest.has_value() ? meanTexel(*finest, scratch) : MeanTexel();
    }
}
