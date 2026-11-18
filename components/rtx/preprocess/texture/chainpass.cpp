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
    void ChainPass::describe(const Input& input)
    {
        mHeld = readTexels(input.mImage, input.mWhy, mReads);
        mLevels.clear();
        mTexels.clear();
        const Result<TextureData, std::string> read = describeImage(*mHeld, input.mEncoding, mLevels, mTexels);
        mFile = read.isOk() ? std::optional(read.value()) : std::nullopt;
        if (mFile.has_value())
            mFile->mWrap = input.mWrap;
    }

    void ChainPass::digest(const Input& input, ContentStore::Digest& digest)
    {
        digest.add(mKeys.of(input.mImage));
        digest.add(input.mEncoding);
        if (input.mEncoding == TextureEncoding::Painted)
            digest.add(input.mWrap);
    }

    namespace
    {
        /// **What a run makes of a file's header**, the one derivation `run`, `madeLayout` and `scratchOf` read: a
        /// file with no chain is built to a texel and every other to its own count of levels, and a colour's own
        /// chain stands where it carries one, every other chain built.
        struct ChainPlan
        {
            std::uint32_t mWidth;
            std::uint32_t mHeight;
            std::uint32_t mFileLevels;
            bool mChainless;
            std::uint32_t mLevels;
            bool mBuilt;

            static ChainPlan of(const std::uint32_t width, const std::uint32_t height, const std::uint32_t fileLevels,
                const TextureEncoding encoding)
            {
                const bool chainless = fileLevels == 1;
                return ChainPlan{ .mWidth = width,
                    .mHeight = height,
                    .mFileLevels = fileLevels,
                    .mChainless = chainless,
                    .mLevels = chainless ? levelsTo1x1(width, height) : fileLevels,
                    .mBuilt = encoding != TextureEncoding::Colour || chainless };
            }

            /// The plan of a file as its header states it, at one level at least.
            static ChainPlan of(const osg::Image& image, const TextureEncoding encoding)
            {
                return of(static_cast<std::uint32_t>(image.s()), static_cast<std::uint32_t>(image.t()),
                    std::max(levelCountOf(image), 1u), encoding);
            }
        };
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
            const ChainPlan plan = ChainPlan::of(
                finest.mWidth, finest.mHeight, static_cast<std::uint32_t>(file.mLevels.size()), file.mEncoding);
            const bool chainless = plan.mChainless;
            const bool built = plan.mBuilt;
            const bool loose = !layoutOf(file.mFormat).isBlocked();
            const bool painted = file.mEncoding == TextureEncoding::Painted;
            const bool normals = file.mEncoding == TextureEncoding::Normal;

            // A colour's own chain stands, and is encoded as it stands where it is loose; every other
            // chain is built, a painted colour's from every level it carries.
            if (built)
                mChain.build(file, plan.mLevels);

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
                // The file's finest as it holds it, which a build of it leaves as it stands, above the levels built.
                output.mForm = ChainForm::Own;
                const std::span<const std::byte> held = file.bytesOf(0);
                output.mBytes.assign(held.begin(), held.end());
                mChain.encode(file, 1, output.mBytes);
            }
        }

        if (input.mRead != nullptr)
            *input.mRead = mHeld;
        release();
    }

    bool fillsLayout(const std::optional<MadeLayout>& layout, const ChainForm form, const std::size_t bytes)
    {
        if (!layout.has_value())
            return form == ChainForm::Stands && bytes == 0;
        return bytes == layout->bytes();
    }

    std::optional<MadeLayout> ChainPass::madeLayout(const Input& input, const ChainForm form)
    {
        const osg::Image& image = input.mImage;
        if (form == ChainForm::Stands || image.s() <= 0 || image.t() <= 0)
            return std::nullopt;

        const ChainPlan plan = ChainPlan::of(image, input.mEncoding);
        const TextureFormat own = readFormat(image, input.mEncoding);
        const MadeLayout whole{ .mWidth = plan.mWidth, .mHeight = plan.mHeight, .mCount = plan.mLevels };
        switch (form)
        {
            case ChainForm::Bc7:
            {
                MadeLayout bc7 = whole;
                bc7.mFormat = bc7FormatOf(input.mEncoding);
                return bc7;
            }
            case ChainForm::Own:
            {
                // A loose file's chain is made in BC7 alone, and a widened one's is loose.
                if (!layoutOf(own).isBlocked())
                    return std::nullopt;
                MadeLayout laid = whole;
                laid.mFormat = own;
                return laid;
            }
            case ChainForm::Stands:
                break;
        }
        return std::nullopt;
    }

    void segmentEndsOfLayout(
        const std::optional<MadeLayout>& layout, const MadeChain& chain, std::vector<std::uint64_t>& ends)
    {
        if (!layout.has_value() || chain.mBytes.size() != layout->bytes())
            return;
        const TexelLayout laid = layoutOf(layout->mFormat);
        std::uint64_t end = 0;
        for (std::uint32_t level = 0; level < layout->mCount; ++level)
        {
            end += laid.levelBytes(std::max(layout->mWidth >> level, 1u), std::max(layout->mHeight >> level, 1u));
            ends.push_back(end);
        }
        ends.push_back(end + 1);
    }

    void ChainPass::segmentEndsOf(const Input& input, const Output& output, std::vector<std::uint64_t>& ends)
    {
        segmentEndsOfLayout(madeLayout(input, output.mForm), output, ends);
    }

    bool ChainPass::fits(const Input& input, const Output& output)
    {
        return fillsLayout(madeLayout(input, output.mForm), output.mForm, output.mBytes.size());
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

        const ChainPlan plan = ChainPlan::of(image, input.mEncoding);
        const std::uint32_t width = plan.mWidth;
        const std::uint32_t height = plan.mHeight;
        if (plan.mBuilt)
            scratch.setBuild(ChainBuild::scratchOf(width, height, plan.mFileLevels, input.mEncoding, plan.mLevels));

        // Whole in BC7, or whole in the file's own block format, the larger: a loose file is made in BC7
        // alone.
        const TexelLayout bc7 = layoutOf(bc7FormatOf(input.mEncoding));
        const TexelLayout own = layoutOf(format);
        std::size_t asBc7 = 0;
        std::size_t asOwn = 0;
        for (std::uint32_t level = 0; level < (plan.mBuilt ? plan.mLevels : plan.mFileLevels); ++level)
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
        mFile.reset();
        mHeld = nullptr;
    }
}
