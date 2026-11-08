#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include <osg/Image>
#include <osg/Referenced>
#include <osg/ref_ptr>

#include "texturedata.hpp"
#include "textureformat.hpp"

namespace Rtx
{
    /// A file's chain made again by the host (`ChainPass`): the levels after the finest, laid as a file
    /// lays them, and the finest level's image where it is not the file's. A slot describes it as that
    /// finest level with these levels under it (`TextureData::mCoarser`).
    ///
    /// **The file's finest level is not copied where the chain keeps the file's format**, and not
    /// held either: the slot that holds this holds the file, as the resource cache and the scene
    /// graph do, so a made chain costs a third of its file's finest level and not the whole of it
    /// twice. A loose chain (`ChainForm::Loose`) is four bytes a texel from its finest level down, so
    /// its finest is an image of its own.
    class MadeImage final : public osg::Referenced
    {
    public:
        /// The levels after `file`'s finest, back to back, at `laid`: the layout `file` is described
        /// at, a widened file's four bytes a texel (`laidLayout`). Holds nothing of `file`.
        MadeImage(const osg::Image& file, const TexelLayout& laid, std::vector<std::byte> coarser);

        /// The layout `format`'s levels are described at: four bytes a texel where it is widened.
        static TexelLayout laidLayout(TextureFormat format);

        /// A loose chain, four bytes a texel from `image`'s finest level down to a texel, `levels`
        /// back to back: its finest level an RGBA8 image of its own under `image`'s name.
        static osg::ref_ptr<const MadeImage> loose(const osg::Image& image, std::span<const std::byte> levels);

        /// The image whose finest level the chain stands on: `file`, which it was made of, or its own.
        const osg::Image& finestOf(const osg::Image& file) const { return mOwnFinest != nullptr ? *mOwnFinest : file; }

        /// The levels after the finest, each offset into `getCoarser`, laid at `getLaid`.
        std::span<const MipLevel> getLevels() const { return mLevels; }
        std::span<const std::byte> getCoarser() const { return mCoarser; }
        const TexelLayout& getLaid() const { return mLaid; }

    private:
        ~MadeImage() override = default;

        /// The finest level a loose chain decoded, and null where the file's own stands.
        osg::ref_ptr<const osg::Image> mOwnFinest;
        std::vector<std::byte> mCoarser;
        std::vector<MipLevel> mLevels;
        TexelLayout mLaid;
    };
}
