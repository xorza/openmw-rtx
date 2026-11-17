#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <osg/Referenced>
#include <osg/ref_ptr>

#include <components/misc/bytebuffer.hpp>

#include "texturedata.hpp"
#include "textureformat.hpp"

namespace Rtx
{
    /// Where a made chain's levels stand in a `width` by `height` file: `mCount` levels from the finest
    /// down, in `mFormat`.
    struct MadeLayout
    {
        TextureFormat mFormat{};
        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;
        std::uint32_t mCount = 0;

        /// The bytes of its levels, back to back.
        std::size_t bytes() const;
    };

    /// A file's chain made again by the host (`ChainPass`): every level from the finest down, in BC7 or the file's own
    /// format, which a slot describes as it stands and reads nothing of its file for.
    class MadeImage final : public osg::Referenced
    {
    public:
        /// `bytes` laid at `layout`, which they fill exactly: what reaches here is a pass's run or a
        /// stored entry its pass found to fit its file (`ChainPass::fits`).
        static osg::ref_ptr<const MadeImage> make(const MadeLayout& layout, Misc::ByteBuffer bytes);

        TextureFormat getFormat() const { return mFormat; }

        /// Every level, each offset into `getBytes`.
        std::span<const MipLevel> getLevels() const { return mLevels; }
        std::span<const std::byte> getBytes() const { return mBytes; }

        /// The layout the levels are laid at.
        const TexelLayout& getLaid() const { return mLaid; }

    private:
        MadeImage() = default;
        ~MadeImage() override = default;

        TextureFormat mFormat{};
        Misc::ByteBuffer mBytes;
        std::vector<MipLevel> mLevels;
        TexelLayout mLaid{};
    };
}
