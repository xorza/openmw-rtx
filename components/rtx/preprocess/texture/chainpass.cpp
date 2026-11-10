#include "chainpass.hpp"

#include <algorithm>
#include <span>
#include <string>

#include <components/misc/result.hpp>
#include <components/rtx/image/imagedescription.hpp>
#include <components/rtx/image/texturedata.hpp>
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
        const Result<TextureData, std::string> read = describeImage(input.mImage, input.mEncoding, mLevels, mTexels);
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

            // A colour's own chain stands, and is encoded as it stands where it is loose; every other
            // chain is built, a painted colour's from every level it carries.
            const bool built = file.mEncoding != TextureEncoding::Colour || chainless;
            if (built)
                mChain.build(file,
                    chainless ? levelsTo1x1(finest.mWidth, finest.mHeight)
                              : static_cast<std::uint32_t>(file.mLevels.size()));

            const bool cut = built && mChain.holdsCut(file);
            const bool small = std::max(finest.mWidth, finest.mHeight) <= sLargestBc7ChainSide;
            if (normals || loose || (chainless && small) || (cut && (chainless || painted)))
            {
                output.mForm = ChainForm::Bc7;
                if (built)
                    mChain.encodeBc7(file.mEncoding, output.mBytes);
                else
                    ChainBuild::encodeOwnBc7(file, output.mBytes);
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

        release();
    }

    bool fillsLayout(const std::optional<MadeLayout>& layout, const MadeChain& chain)
    {
        if (!layout.has_value())
            return chain.mForm == ChainForm::Stands && chain.mBytes.empty();
        return chain.mBytes.size() == layout->bytes();
    }

    std::optional<MadeLayout> ChainPass::madeLayout(const Input& input, const ChainForm form)
    {
        const osg::Image& image = input.mImage;
        if (form == ChainForm::Stands || image.s() <= 0 || image.t() <= 0)
            return std::nullopt;

        // As `run` builds: a file with no chain to a texel, and every other to its own count of levels.
        const auto width = static_cast<std::uint32_t>(image.s());
        const auto height = static_cast<std::uint32_t>(image.t());
        const std::uint32_t fileLevels = std::max(levelCountOf(image), 1u);
        const std::uint32_t count = fileLevels == 1 ? levelsTo1x1(width, height) : fileLevels;
        const TextureFormat own = readFormat(image, input.mEncoding);
        const MadeLayout whole{ .mWidth = width, .mHeight = height, .mFirst = 0, .mCount = count };
        switch (form)
        {
            case ChainForm::Bc7:
            {
                MadeLayout bc7 = whole;
                bc7.mFormat = bc7FormatOf(input.mEncoding);
                return bc7;
            }
            case ChainForm::Own:
            case ChainForm::Coarser:
            {
                // A loose file's chain is made in BC7 alone, and a widened one's is loose.
                if (!layoutOf(own).isBlocked())
                    return std::nullopt;
                MadeLayout laid = whole;
                laid.mFormat = own;
                laid.mFirst = form == ChainForm::Coarser ? 1 : 0;
                if (laid.mFirst >= laid.mCount)
                    return std::nullopt;
                return laid;
            }
            case ChainForm::Stands:
                break;
        }
        return std::nullopt;
    }

    bool ChainPass::fits(const Input& input, const Output& output)
    {
        return fillsLayout(madeLayout(input, output.mForm), output);
    }

    ChainScratch ChainPass::scratchOf(const Input& input)
    {
        const osg::Image& image = input.mImage;
        const TextureFormat format = readFormat(image, input.mEncoding);
        ChainScratch scratch;
        scratch[ChainBuffer::File] = laidBytes(image, format);

        // A file the description refuses builds and makes nothing (`checkUploadable`).
        if (image.s() <= 0 || image.t() <= 0 || (!isUploadable(format) && !isWidened(format)))
            return scratch;

        // As `run` builds: a file with no chain to a texel, and every other to its own count of levels.
        const auto width = static_cast<std::uint32_t>(image.s());
        const auto height = static_cast<std::uint32_t>(image.t());
        const std::uint32_t fileLevels = std::max(levelCountOf(image), 1u);
        const bool chainless = fileLevels == 1;
        const std::uint32_t levels = chainless ? levelsTo1x1(width, height) : fileLevels;
        const bool built = input.mEncoding != TextureEncoding::Colour || chainless;
        if (built)
            scratch.setBuild(ChainBuild::scratchOf(width, height, fileLevels, input.mEncoding, levels));

        // Whole in BC7, or whole in the file's own block format, the larger: a loose file is made in BC7
        // alone.
        const TexelLayout bc7 = layoutOf(bc7FormatOf(input.mEncoding));
        const TexelLayout own = layoutOf(format);
        std::size_t asBc7 = 0;
        std::size_t asOwn = 0;
        for (std::uint32_t level = 0; level < (built ? levels : fileLevels); ++level)
        {
            const std::uint32_t across = std::max(width >> level, 1u);
            const std::uint32_t down = std::max(height >> level, 1u);
            asBc7 += bc7.levelBytes(across, down);
            asOwn += own.levelBytes(across, down);
        }
        scratch[ChainBuffer::Made] = own.isBlocked() ? std::max(asBc7, asOwn) : asBc7;
        scratch[ChainBuffer::Payload] = scratch[ChainBuffer::Made] + 1;
        return scratch;
    }

    void ChainPass::release()
    {
        mImage = nullptr;
        mFile.reset();
    }
}
