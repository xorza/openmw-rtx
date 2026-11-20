#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <osg/Referenced>
#include <osg/ref_ptr>

#include <components/rtx/common/bufferpool.hpp>

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

        /// The first level held: what is read of a chain from a level its reader wants, the finer ones left out.
        std::uint32_t mFirst = 0;

        /// The bytes of the levels held, back to back.
        std::size_t bytes() const;

        /// Where `level` begins in a chain held whole, every level before it laid ahead of it.
        std::size_t offsetOf(std::uint32_t level) const;
    };

    /// A file's chain made again by the host (`ChainPass`): every level from the finest down, in BC7 or the file's own
    /// format, which a slot describes as it stands and reads nothing of its file for.
    class MadeImage final : public osg::Referenced
    {
    public:
        /// `bytes` laid at `layout`, which they fill exactly: what reaches here is a pass's run or a
        /// stored entry its pass found to fit its file (`ChainPass::fits`). Its buffer goes back to its pool as the
        /// made image goes, the device having its copy.
        static osg::ref_ptr<const MadeImage> make(const MadeLayout& layout, PooledBuffer bytes);

        TextureFormat getFormat() const { return mFormat; }

        /// Every level, each held from the layout's first offset into `getBytes`, those before it at nought and held
        /// nowhere.
        std::span<const MipLevel> getLevels() const { return mLevels; }
        std::span<const std::byte> getBytes() const { return mBytes.bytes(); }

        /// The layout the levels are laid at.
        const TexelLayout& getLaid() const { return mLaid; }

    private:
        MadeImage() = default;
        ~MadeImage() override = default;

        TextureFormat mFormat{};
        PooledBuffer mBytes;
        std::vector<MipLevel> mLevels;
        TexelLayout mLaid{};
    };
}
