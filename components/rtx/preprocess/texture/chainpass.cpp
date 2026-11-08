#include "chainpass.hpp"

#include <algorithm>
#include <span>
#include <string>

#include <components/misc/result.hpp>
#include <components/rtx/image/imagedescription.hpp>
#include <components/rtx/image/textureformat.hpp>

namespace Rtx
{
    void ChainPass::describe(const Input& input)
    {
        if (mImage.get() == &input.mImage && mEncoding == input.mEncoding)
            return;

        mImage = &input.mImage;
        mEncoding = input.mEncoding;
        mLevels.clear();
        mTexels.clear();
        const Misc::Result<TextureData, std::string> read
            = describeImage(input.mImage, input.mEncoding, mLevels, mTexels);
        mFile = read.isOk() ? std::optional(read.value()) : std::nullopt;
    }

    void ChainPass::digest(const Input& input, ContentDigest& digest)
    {
        describe(input);
        const bool rebuilt = mFile.has_value() && ChainBuild::rebuilds(*mFile);
        digest.add(rebuilt);
        if (!rebuilt)
            return;

        const TextureData& file = *mFile;
        const MipLevel& finest = file.mLevels.front();
        digest.add(file.mFormat);
        digest.add(file.mEncoding);
        digest.add(static_cast<std::uint32_t>(file.mLevels.size()));
        digest.add(finest.mWidth);
        digest.add(finest.mHeight);
        digest.add(
            file.mBytes.subspan(finest.mOffset, layoutOf(file.mFormat).levelBytes(finest.mWidth, finest.mHeight)));
    }

    void ChainPass::run(const Input& input, Output& output)
    {
        describe(input);
        output.mForm = ChainForm::Stands;
        output.mBytes.clear();
        if (mFile.has_value() && ChainBuild::rebuilds(*mFile))
        {
            const TextureData& file = *mFile;
            const MipLevel& finest = file.mLevels.front();
            const bool chainless = file.mLevels.size() == 1;
            mChain.build(file,
                chainless ? levelsTo1x1(finest.mWidth, finest.mHeight)
                          : static_cast<std::uint32_t>(file.mLevels.size()));

            const bool cut = mChain.holdsCut(file);
            const bool small = std::max(finest.mWidth, finest.mHeight) <= sLargestLooseChainSide;
            if (chainless && ((layoutOf(file.mFormat).isBlocked() && small) || cut))
            {
                output.mForm = ChainForm::Loose;
                for (std::uint32_t level = 0; level < mChain.getLevelCount(); ++level)
                {
                    const std::span<const std::byte> texels = mChain.getTexels(level);
                    output.mBytes.insert(output.mBytes.end(), texels.begin(), texels.end());
                }
            }
            else if (!cut)
            {
                output.mForm = ChainForm::Coarser;
                mChain.encode(file, output.mBytes);
            }
        }

        // Let go of the image, as a texture pass does: the next ask describes its own.
        mImage = nullptr;
        mFile.reset();
    }
}
