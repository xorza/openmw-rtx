#include "chainpass.hpp"

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
        digest.add(file.mBytes.first(file.mLevels[1].mOffset));
    }

    void ChainPass::run(const Input& input, Output& output)
    {
        describe(input);
        output.clear();
        if (mFile.has_value() && ChainBuild::rebuilds(*mFile))
        {
            const TextureData& file = *mFile;
            mChain.build(file, static_cast<std::uint32_t>(file.mLevels.size()));
            if (!mChain.holdsCut(file))
                mChain.encode(file, output);
        }

        // Let go of the image, as a texture pass does: the next ask describes its own.
        mImage = nullptr;
        mFile.reset();
    }
}
