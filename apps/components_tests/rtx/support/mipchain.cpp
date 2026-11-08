#include "mipchain.hpp"

#include <algorithm>
#include <cstddef>
#include <span>

#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/textureformat.hpp>

namespace Rtx::Testing
{
    void MipChain::build(const TextureData& described)
    {
        mTexture.reuse();
        mEncoded = true;

        if (!described.wantsCompletedChain())
            return;

        // The host's one chain (`ChainBuild`), laid into a texture of this object's own.
        const MipLevel& finest = described.mLevels.front();
        mEncoded = isSrgb(described.mFormat);
        mTexture.openChain(finest.mWidth, finest.mHeight,
            mEncoded ? TextureFormat::Rgba8Srgb : TextureFormat::Rgba8Unorm, described.mEncoding);
        mTexture.setName(described.mName);

        mChain.build(described, mTexture.getShape().getLevelCount());
        for (std::uint32_t at = 0; at < mChain.getLevelCount(); ++at)
        {
            const std::span<const std::byte> built = mChain.getTexels(at);
            const std::span<std::byte> into = mTexture.level(at);
            std::copy(built.begin(), built.end(), into.begin());
        }
    }

    TextureData MipChain::describe() const
    {
        return mTexture.describe();
    }
}
