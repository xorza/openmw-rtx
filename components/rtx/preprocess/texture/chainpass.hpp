#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <osg/Image>
#include <osg/ref_ptr>

#include <components/contentstore/key.hpp>
#include <components/misc/bytebuffer.hpp>
#include <components/rtx/common/letgo.hpp>
#include <components/rtx/image/chainbuild.hpp>
#include <components/rtx/image/madeimage.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/texturewrap.hpp>
#include <components/rtx/preprocess/contentpass.hpp>
#include <components/rtx/preprocess/filekeys.hpp>
#include <components/rtx/preprocess/texelreads.hpp>

#include "chainscratch.hpp"

namespace Rtx
{
    /// What a chain made of a file is (`MadeChain`).
    enum class ChainForm : std::uint8_t
    {
        /// Nothing made: the file's own chain stands.
        Stands,

        /// The whole chain in BC7, from the finest down (`MadeImage`).
        Bc7,

        /// The whole chain in the file's own format, from the finest down: a painted colour's, every
        /// level delit, and every other's finest as the file holds it above the levels built
        /// (`MadeImage`).
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

    /// Whether `chain` fills `layout` exactly, or, with no layout, stands and holds no byte: the one
    /// check of a made chain against its file (`ChainPass::fits`, `ConversionPass::fits`).
    bool fillsLayout(const std::optional<MadeLayout>& layout, const MadeChain& chain);

    /// Where a made chain laid at `layout` ends each level and then its form's byte, as its codec lays the payload,
    /// into `ends` (`ChainPass::segmentEndsOf`); nothing where it stands, or its bytes do not fill the layout.
    void segmentEndsOfLayout(
        const std::optional<MadeLayout>& layout, const MadeChain& chain, std::vector<std::uint64_t>& ends);

    /// A file's chain made again from its finest level (`ChainBuild`), as a pass: what the store keeps
    /// for every normal and data map, since the replacers' own chains are made with filters that are
    /// wrong for a map (`docs/architecture.md`, "Every normal and data map's chain is rebuilt, and a
    /// colour file keeps its own"), and for a file that carried none.
    ///
    /// **A file that carried no chain gets one down to a texel.** Morrowind ships a hundred and
    /// eighty-seven such textures and its rain is one: a drop's peak alpha is 0.400 at its finest where
    /// the missing levels hold 0.283, 0.129 and 0.068, so a storm comes out as hard white marks that
    /// flicker. Morrowind's own chains stop short of a texel, at eight by eight for a 256-square
    /// texture, whose last level is its mean to within what a ray can tell: a colour's own chain stands.
    ///
    /// **The whole chain in BC7 for every normal map**, and for a loose file and every chain the host
    /// would make loose (`docs/architecture.md`, "A loose chain is whole in BC7"): a block file of
    /// one level up to `sLargestBc7ChainSide`, which BC7's error moves less than BC1's would, and a
    /// BC1 file of one level with a cut, whose coverage BC1 cannot hold. **The whole chain in the file's
    /// own format**, its finest as the file holds it, for every other block file, so a slot reads every level
    /// it stands at from the store and none from the file (`docs/architecture.md`, "Every chain the store keeps
    /// is whole"); **nothing made** for a format `ChainBuild` does not rebuild, a block colour that carried its
    /// chain, and a BC1 file that carried a chain and has a cut.
    ///
    /// **A painted colour's whole chain delit** (`docs/architecture.md`, "A painted colour is stored
    /// delit, keyed by use"): every level it carries, and the levels under a chainless one boxed from
    /// the delit finest, in the file's own format; in BC7 where it is loose, small and chainless, or
    /// holds a cut, whose one bit of alpha rgbcx's BC1 does not encode.
    ///
    /// **The key is everything the run reads**: the file's (`FileKeys`), the encoding, and a painted colour's wrap. A
    /// header's key reads nothing, so a chain the store holds whole comes back with no read of its file; a run reads
    /// the file's texels through the header.
    class ChainPass
    {
    public:
        struct Input
        {
            const osg::Image& mImage;
            TextureEncoding mEncoding{};

            /// How the slot addresses it past its edges, which a painted colour's estimate follows.
            TextureWrap mWrap = TextureWrap::Repeat;

            /// Why a run reads a header's texels, where it must.
            TexelRead mWhy = TexelRead::File;

            /// Where a run leaves the file's texels it read, for a caller that reads them next, or null.
            osg::ref_ptr<const osg::Image>* mRead = nullptr;
        };

        using Output = MadeChain;

        /// @param build the chain build this pass and the conversion share, which the preprocessor owns:
        ///        one a thread, holding its largest chain's scratch, and not one a pass.
        /// @param keys the keys of the preprocessor's files, and `reads` where it counts a header's texels read.
        ChainPass(ChainBuild& build, FileKeys& keys, TexelReads& reads)
            : mChain(build)
            , mKeys(keys)
            , mReads(reads)
        {
        }

        static constexpr ContentPassId sPass = ContentPassId::Chain;
        static constexpr std::uint32_t sVersion = 8;

        void digest(const Input& input, ContentStore::Digest& digest);
        void run(const Input& input, Output& output);

        /// Where a chain of `form` made of `input`'s file lays its levels, read off the file's header
        /// alone; nothing for `ChainForm::Stands`, and for a form no run of `input` makes in that shape: own
        /// levels of a loose file.
        static std::optional<MadeLayout> madeLayout(const Input& input, ChainForm form);

        /// Where `output`'s payload's segments end as its codec lays it (`ContentCache::keep`): each level `madeLayout`
        /// lays, and the form's byte after them; nothing for a chain that stands, one segment.
        static void segmentEndsOf(const Input& input, const Output& output, std::vector<std::uint64_t>& ends);

        /// Whether `output` can be a run of `input`'s (`ContentCache::find`): its form lays levels for
        /// the file and its bytes fill them exactly, or nothing stands and it holds no byte. An entry
        /// whose digest holds and whose bytes are another file's laid nowhere past this.
        static bool fits(const Input& input, const Output& output);

        /// What a run of `input` holds of each buffer at most, read off its image's header alone: the
        /// file laid, the build, and the chain made in the largest form it could take, held once as the
        /// output and once as the store's payload. Its own buffers' room, and the build's, never past it.
        static ChainScratch scratchOf(const Input& input);

        /// What the file's laid levels hold now (`ChainBuffer::File`); the build reports its own.
        std::size_t capacity() const { return mTexels.capacity(); }

        /// Gives the laid levels' room back.
        void letGo()
        {
            release();
            letGoOf(mLevels);
            letGoOf(mTexels);
        }

    private:
        /// Describes `input`'s file into the scratch, its texels read and held where it is a header.
        void describe(const Input& input);

        /// Lets go of the texels a run described, as it ends: held, they kept a file the scene let go of alive.
        void release();

        osg::ref_ptr<const osg::Image> mHeld;
        std::optional<TextureData> mFile;
        std::vector<MipLevel> mLevels;
        std::vector<std::byte> mTexels;
        ChainBuild& mChain;
        FileKeys& mKeys;
        TexelReads& mReads;
    };
}
