#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <osg/Referenced>
#include <osg/ref_ptr>

#include <components/misc/bytebuffer.hpp>

#include "texturedata.hpp"
#include "textureformat.hpp"

namespace Rtx
{
    /// Where a made chain's levels stand in a `width` by `height` file: `mCount` levels from level
    /// `mFirst` down, in `mFormat`. A whole chain starts at the finest; a coarser one at the level under
    /// it, in the file's own format, so the slot keeps the file's finest.
    struct MadeLayout
    {
        TextureFormat mFormat{};
        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;
        std::uint32_t mFirst = 0;
        std::uint32_t mCount = 0;

        /// The bytes of its levels, back to back.
        std::size_t bytes() const;
    };

    /// A file's chain made again by the host (`ChainPass`), in one of two shapes. **Coarser**: the
    /// levels after the file's finest, in the layout the file is described at, which a slot describes
    /// under the file's own finest level (`TextureData::mCoarser`), so the finest is neither copied nor
    /// held: the slot that holds this holds the file, as the resource cache and the scene graph do.
    /// **Whole**: every level from the finest down in a format of its own (BC7), which a slot describes
    /// as it stands.
    class MadeImage final : public osg::Referenced
    {
    public:
        /// `bytes` laid at `layout`, which they fill exactly: what reaches here is a pass's run or a
        /// stored entry its pass found to fit its file (`ChainPass::fits`).
        static osg::ref_ptr<const MadeImage> make(const MadeLayout& layout, Misc::ByteBuffer bytes);

        /// The format of a whole chain, or nothing for coarser levels, which are in their file's.
        std::optional<TextureFormat> getWholeFormat() const { return mWholeFormat; }

        /// Coarser: the levels after the finest. Whole: every level. Each offset into `getBytes`.
        std::span<const MipLevel> getLevels() const { return mLevels; }
        std::span<const std::byte> getBytes() const { return mBytes; }

        /// The layout the levels are laid at.
        const TexelLayout& getLaid() const { return mLaid; }

    private:
        MadeImage() = default;
        ~MadeImage() override = default;

        std::optional<TextureFormat> mWholeFormat;
        Misc::ByteBuffer mBytes;
        std::vector<MipLevel> mLevels;
        TexelLayout mLaid{};
    };
}
