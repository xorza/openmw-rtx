#include "finesttexels.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include "alphaimage.hpp"
#include "contentkey.hpp"

namespace Rtx
{
    void FinestTexels::describe(const osg::Image& image, ContentDigest& digest)
    {
        mFinest = describeFinest(image, mScratch);

        const bool described = mFinest.has_value();
        digest.addValue(described);
        if (!described)
            return;

        const TextureData& finest = *mFinest;
        const MipLevel& level = finest.mLevels.front();
        digest.addValue(finest.mFormat);
        digest.addValue(finest.mEncoding);
        digest.addValue(level.mWidth);
        digest.addValue(level.mHeight);

        // Clamped as the readers clamp it, so the key never reaches past what they could read.
        const std::size_t from = std::min<std::size_t>(level.mOffset, finest.mBytes.size());
        const std::size_t bytes
            = std::min(layoutOf(finest.mFormat).levelBytes(level.mWidth, level.mHeight), finest.mBytes.size() - from);
        digest.add(finest.mBytes.subspan(from, bytes));
    }
}
