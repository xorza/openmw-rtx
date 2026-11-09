#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <osg/Image>
#include <osg/ref_ptr>

#include <components/misc/bytebuffer.hpp>
#include <components/rtx/image/chainbuild.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/texturewrap.hpp>
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

        /// The whole chain in BC7, from the finest down (`MadeImage::whole`).
        Bc7,

        /// The whole chain in the file's own format, from the finest down: a painted colour's, every
        /// level delit (`MadeImage::whole`).
        Own,
    };

    /// The largest side of a block-compressed file with no chain whose chain is made in BC7: the
    /// largest of the hundred and eighty-seven single-level files Morrowind ships (`ChainPass`).
    inline constexpr std::uint32_t sLargestBc7ChainSide = 512;

    /// A chain `ChainPass` made, and in which form.
    struct MadeChain
    {
        ChainForm mForm = ChainForm::Stands;
        Misc::ByteBuffer mBytes{};
    };

    /// A file's chain made again from its finest level (`ChainBuild`), as a pass: what the store keeps
    /// for every normal and data map, since the replacers' own chains are made with filters that are
    /// wrong for a map (`docs/rtx/architecture.md`, "Every normal and data map's chain is rebuilt, and a colour
    /// file keeps its own"), and for a file that carried none.
    ///
    /// **A file that carried no chain gets one down to a texel.** Morrowind ships a hundred and
    /// eighty-seven such textures and its rain is one: read at its finest, a drop's peak alpha is
    /// 0.400 where the missing levels hold 0.283, 0.129 and 0.068, so a storm comes out as hard white
    /// marks that flicker; the rasterizer has the driver make them. Morrowind's own chains stop short
    /// of a texel, at eight by eight for a 256-square texture, whose last level is its mean to within
    /// what a ray can tell: a colour's own chain stands.
    ///
    /// **The whole chain in BC7 for every normal map**, in one encoding (`ChainBuild`), its finest level
    /// made again with the rest; **and for a loose file and every chain the host would make loose**
    /// (`docs/rtx/architecture.md`, "A loose chain is whole in BC7"): a loose file, four bytes a texel, its
    /// own chain where it is a colour that carried one and the chain made again otherwise; a block file
    /// of one level up to `sLargestBc7ChainSide`, which BC7's error moves less than BC1's would; and a
    /// BC1 file of one level with a cut, whose coverage BC1 cannot hold. A quarter of a loose chain's
    /// bytes. **Coarser levels in the file's own format** for every other block file, a replacer's
    /// 4096-square BC1 with no levels 2.7 MB more; **nothing made** for a format `ChainBuild` does not
    /// rebuild, for a block colour that carried its chain, and for a BC1 file that carried a chain and
    /// has a cut, whose own chain stands.
    ///
    /// **A painted colour's whole chain delit** (`docs/rtx/architecture.md`, "A painted colour is stored delit,
    /// keyed by use"):
    /// every level it carries, and the levels under a chainless one boxed from the delit finest, in the
    /// file's own format, so a BC1 colour keeps its four bits a texel; in BC7 where it is loose, small
    /// and chainless, or holds a cut, whose one bit of alpha rgbcx's BC1 does not encode.
    ///
    /// **The key is everything the run reads**: the file's format, encoding and level count, its
    /// finest level's extent, a painted colour's wrap, and the bytes of its finest level, or of every
    /// level where a colour's own chain is read. Not its name, so one picture under two names is one
    /// entry, and not the coarser levels of a chain made again, which nothing here reads.
    class ChainPass
    {
    public:
        struct Input
        {
            const osg::Image& mImage;
            TextureEncoding mEncoding;

            /// How the slot addresses it past its edges, which a painted colour's estimate follows.
            TextureWrap mWrap = TextureWrap::Repeat;
        };

        using Output = MadeChain;

        /// @param build the chain build this pass and the conversion share, which the preprocessor owns:
        ///        one a thread, holding its largest chain's scratch, and not one a pass.
        explicit ChainPass(ChainBuild& build)
            : mChain(build)
        {
        }

        static constexpr ContentPassId sPass = ContentPassId::Chain;
        static constexpr std::uint32_t sVersion = 7;
        static constexpr bool sStored = true;

        void digest(const Input& input, ContentDigest& digest);
        void run(const Input& input, Output& output);

        /// Lets go of the image `digest` described, which a run lets go of as it ends and the
        /// preprocessor once the store answered: held, it kept a file the scene let go of alive.
        void release();

    private:
        /// Describes `input`'s image into the scratch, and holds it, unless it is the one described.
        void describe(const Input& input);

        osg::ref_ptr<const osg::Image> mImage;
        TextureEncoding mEncoding = TextureEncoding::Colour;
        TextureWrap mWrap = TextureWrap::Repeat;
        std::optional<TextureData> mFile;
        std::vector<MipLevel> mLevels;
        std::vector<std::byte> mTexels;
        ChainBuild& mChain;
    };
}
