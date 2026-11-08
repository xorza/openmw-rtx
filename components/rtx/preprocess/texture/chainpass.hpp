#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <osg/Image>
#include <osg/ref_ptr>

#include <components/rtx/image/chainbuild.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/preprocess/contentkey.hpp>
#include <components/rtx/preprocess/contentpass.hpp>

namespace Rtx
{
    /// What a chain made of a file is (`MadeChain`).
    enum class ChainForm : std::uint8_t
    {
        /// Nothing made: the file's own chain stands.
        Stands,

        /// The levels under the file's finest, in its own format, laid as a file lays them
        /// (`MadeImage`).
        Coarser,

        /// The whole chain in four loose bytes a texel, from the finest down (`MadeImage::loose`).
        Loose,
    };

    /// The largest side of a block-compressed file with no chain whose chain is made loose: the
    /// largest of the hundred and eighty-seven single-level files Morrowind ships (`ChainPass`).
    inline constexpr std::uint32_t sLargestLooseChainSide = 512;

    /// A chain `ChainPass` made, and in which form.
    struct MadeChain
    {
        ChainForm mForm = ChainForm::Stands;
        std::vector<std::byte> mBytes{};
    };

    /// A file's levels under its finest, made again from that level (`ChainBuild`), as a pass: what
    /// the store keeps for every normal and data map, since the replacers' own chains are made with
    /// filters that are wrong for a map (`.notes/content-cache.md`, the chains' decisions), and the
    /// whole chain of a file that carried none.
    ///
    /// **A file that carried no chain gets one down to a texel.** Morrowind ships a hundred and
    /// eighty-seven such textures and its rain is one: read at its finest, a drop's peak alpha is
    /// 0.400 where the missing levels hold 0.283, 0.129 and 0.068, so a storm comes out as hard white
    /// marks that flicker; the rasterizer has the driver make them. Morrowind's own chains stop short
    /// of a texel, at eight by eight for a 256-square texture, whose last level is its mean to within
    /// what a ray can tell: a colour's own chain stands.
    ///
    /// **Loose for a block file of one level up to `sLargestLooseChainSide`**, so a vanilla level
    /// moves by the box's rounding and not by an encoder's step, and for a BC1 file of one level with
    /// a cut, whose coverage BC1 cannot hold; **coarser levels in the file's own format** for every
    /// other, a replacer's 4096-square BC1 with no levels 2.7 MB more where loose it was 89 MB;
    /// **nothing made** for a format `ChainBuild` does not rebuild and for a BC1 file that carried a
    /// chain and has a cut, whose own chain stands.
    ///
    /// **The key is everything the build reads**: the file's format, encoding and level count, and
    /// its finest level's extent and bytes. Not its name, so one picture under two names is one entry,
    /// and not its coarser levels, which nothing here reads.
    class ChainPass
    {
    public:
        struct Input
        {
            const osg::Image& mImage;
            TextureEncoding mEncoding;
        };

        using Output = MadeChain;

        static constexpr ContentPassId sPass = ContentPassId::Chain;
        static constexpr std::uint32_t sVersion = 2;
        static constexpr bool sStored = true;

        void digest(const Input& input, ContentDigest& digest);
        void run(const Input& input, Output& output);

    private:
        /// Describes `input`'s image into the scratch, and holds it, unless it is the one described.
        void describe(const Input& input);

        osg::ref_ptr<const osg::Image> mImage;
        TextureEncoding mEncoding = TextureEncoding::Colour;
        std::optional<TextureData> mFile;
        std::vector<MipLevel> mLevels;
        std::vector<std::byte> mTexels;
        ChainBuild mChain;
    };
}
