#include "chainpass.hpp"

#include <algorithm>
#include <span>
#include <string>

#include <components/misc/result.hpp>
#include <components/rtx/image/imagedescription.hpp>
#include <components/rtx/image/textureformat.hpp>

namespace Rtx
{
    namespace
    {
        /// Whether the run reads every level `file` carries: a colour's own chain, encoded as it stands
        /// where the file is loose, and a painted colour's, each level delit where it stands.
        bool readsOwnChain(const TextureData& file)
        {
            return isColour(file.mEncoding) && file.mLevels.size() > 1;
        }
    }

    void ChainPass::describe(const Input& input)
    {
        if (mImage.get() == &input.mImage && mEncoding == input.mEncoding && mWrap == input.mWrap)
            return;

        mImage = &input.mImage;
        mEncoding = input.mEncoding;
        mWrap = input.mWrap;
        mLevels.clear();
        mTexels.clear();
        const Misc::Result<TextureData, std::string> read
            = describeImage(input.mImage, input.mEncoding, mLevels, mTexels);
        mFile = read.isOk() ? std::optional(read.value()) : std::nullopt;
        if (mFile.has_value())
            mFile->mWrap = input.mWrap;
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
        if (file.mEncoding == TextureEncoding::Painted)
            digest.add(file.mWrap);
        const auto read = readsOwnChain(file) ? static_cast<std::uint32_t>(file.mLevels.size()) : 1u;
        for (std::uint32_t level = 0; level < read; ++level)
            digest.add(file.bytesOf(level));
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
            const bool loose = !layoutOf(file.mFormat).isBlocked();
            const bool painted = file.mEncoding == TextureEncoding::Painted;
            const bool normals = file.mEncoding == TextureEncoding::Normal;

            // A colour's own chain stands, and is laid as it stands where it is loose; every other chain
            // is built, a painted colour's from every level it carries.
            const bool built = file.mEncoding != TextureEncoding::Colour || chainless;
            if (built)
                mChain.build(file,
                    chainless ? levelsTo1x1(finest.mWidth, finest.mHeight)
                              : static_cast<std::uint32_t>(file.mLevels.size()));
            else if (loose)
                mChain.layOwnLevels(file);

            const bool cut = built && mChain.holdsCut(file);
            const bool small = std::max(finest.mWidth, finest.mHeight) <= sLargestBc7ChainSide;
            if (normals || loose || (chainless && small) || (cut && (chainless || painted)))
            {
                output.mForm = ChainForm::Bc7;
                mChain.encodeBc7(file.mEncoding, output.mBytes);
            }
            else if (painted)
            {
                // Every level delit, so the finest is made again with the rest, in the file's format.
                output.mForm = ChainForm::Own;
                mChain.encode(file, 0, output.mBytes);
            }
            else if (built && !cut)
            {
                output.mForm = ChainForm::Coarser;
                mChain.encode(file, 1, output.mBytes);
            }
        }

        // Let go of the image, as a texture pass does: the next ask describes its own.
        mImage = nullptr;
        mFile.reset();
    }
}
