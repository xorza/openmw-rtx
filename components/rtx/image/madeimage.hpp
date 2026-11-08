#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <osg/Image>
#include <osg/Referenced>
#include <osg/ref_ptr>

#include "texturedata.hpp"
#include "textureformat.hpp"

namespace Rtx
{
    /// A file's chain made again by the host (`ChainPass`), in one of two shapes. **Coarser**: the
    /// levels after the file's finest, in the layout the file is described at, which a slot describes
    /// under the file's own finest level (`TextureData::mCoarser`), so the finest is neither copied nor
    /// held: the slot that holds this holds the file, as the resource cache and the scene graph do.
    /// **Whole**: every level from the finest down in a format of its own (BC7), which a slot describes
    /// as it stands.
    class MadeImage final : public osg::Referenced
    {
    public:
        /// The levels after `file`'s finest, back to back, at `laid`: the layout `file` is described
        /// at, a widened file's four bytes a texel (`laidLayout`). Holds nothing of `file`.
        MadeImage(const osg::Image& file, const TexelLayout& laid, std::vector<std::byte> coarser);

        /// The whole chain of a `width` by `height` file in `format`, every level back to back from the
        /// finest down to a texel.
        static osg::ref_ptr<const MadeImage> whole(
            TextureFormat format, std::uint32_t width, std::uint32_t height, std::vector<std::byte> levels);

        /// The layout `format`'s levels are described at: four bytes a texel where it is widened.
        static TexelLayout laidLayout(TextureFormat format);

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

        /// Lays `mBytes` from level `first` of a `width` by `height` chain at `laid`, each level half
        /// the one above and at least a texel.
        void layLevels(std::uint32_t width, std::uint32_t height, std::uint32_t first, const TexelLayout& laid);

        std::optional<TextureFormat> mWholeFormat;
        std::vector<std::byte> mBytes;
        std::vector<MipLevel> mLevels;
        TexelLayout mLaid{};
    };
}
