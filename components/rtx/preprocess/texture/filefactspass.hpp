#pragma once

#include <cstdint>
#include <optional>

#include <osg/ref_ptr>

#include <components/contentstore/key.hpp>
#include <components/contentstore/store.hpp>
#include <components/rtx/image/imagefacts.hpp>
#include <components/rtx/image/texels.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/preprocess/contentpass.hpp>

namespace osg
{
    class Image;
}

namespace Rtx
{
    /// An image's finest level as `FileFactsPass` reads it — `describeFinest` — for one run.
    ///
    /// **The image is held as long as its description**, because the description spans the image's
    /// own bytes wherever they need no laying out (`describeImage`).
    ///
    /// **The scratch is the preprocessor's**, which a file's key is hashed through as well: a finest level widened
    /// to four bytes a texel is 16 MiB for a 2048-square picture, and a pass at a time runs on a
    /// preprocessor's thread. Not copyable: the description spans the scratch's buffers.
    class FinestTexels
    {
    public:
        explicit FinestTexels(TexelScratch& scratch);
        ~FinestTexels();

        FinestTexels(const FinestTexels&) = delete;
        FinestTexels& operator=(const FinestTexels&) = delete;

        /// Describes `image` into the scratch, and holds it until the next `describe` or `clear`.
        void describe(const osg::Image& image);

        /// What the last `describe` found, spanning the scratch or the held image: nothing for an
        /// image no reader here decodes.
        const std::optional<TextureData>& get() const { return mFinest; }

        TexelScratch& getScratch() { return mScratch; }

        /// Lets go of the image and its description.
        void clear();

    private:
        TexelScratch& mScratch;
        osg::ref_ptr<const osg::Image> mImage;
        std::optional<TextureData> mFinest;
    };

    /// **Every fact a material asks of a file's texels, from one description of its finest level** (`ImageFacts`),
    /// keyed by the file's key, which its asker hands in: `hashFile` of the texels where the file is first read
    /// (`ContentPreprocessor::derive`), `FileKeys::of` for an image that holds its own texels. Handed an image that
    /// holds its texels: a header's facts are derived where its file is first read.
    ///
    /// **One walk where four passes were**: whether the alpha reaches solid stops at the first solid texel and the
    /// mean decodes every one, which on the frame cost 11 ms at `seyda-neen-ship-dawn` where an additive sheet
    /// alone asked the mean; read where a file is first read, on a loader's thread, every fact is asked of the file
    /// sooner or later, and four entries a file were four reads of the store.
    class FileFactsPass
    {
    public:
        struct Input
        {
            const osg::Image& mTexels;
            ContentStore::Key mFile;
        };

        using Output = ImageFacts;

        static constexpr ContentPassId sPass = ContentPassId::FileFacts;

        /// Two: its kind held `SolidReach`'s entries at one, which the store's sweep removes by their version.
        static constexpr std::uint32_t sVersion = 2;

        void digest(const Input& input, ContentStore::Digest& digest) { digest.add(input.mFile); }

        /// Answers for `input`'s texels, and lets go of them.
        void run(const Input& input, ImageFacts& facts);

    private:
        /// Made by a `ContentPreprocessor` and by nothing else — `ShapeFold` says why — over the scratch it holds for
        /// reading a file's texels.
        friend class ContentPreprocessor;
        explicit FileFactsPass(TexelScratch& scratch)
            : mFinest(scratch)
        {
        }

        FinestTexels mFinest;
    };

    /// The most a classic `_spec` map's channels spread, on the mean: the Fullrest Repack's fourteen
    /// spread 0.004 to 0.042, and the PBR installs' 3400 metal-roughness maps 0.151 at the least, so a
    /// bound between them (`docs/architecture.md`, "One specular layout, a classic map converted").
    inline constexpr double sClassicSpreadMost = 0.1;

    /// Every fact of a finest level as described (`FinestTexels`): the defaults of `ImageFacts` for an image no
    /// reader here decodes.
    ImageFacts factsOf(const std::optional<TextureData>& finest, TexelScratch& scratch);
}
