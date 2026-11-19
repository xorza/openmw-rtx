#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <osg/Image>
#include <osg/ref_ptr>

#include <components/misc/result.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/image/madeimage.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/preprocess/preparedimages.hpp>
#include <components/rtx/scene/refusal.hpp>

namespace Rtx
{
    class SceneDesc;

    /// Every live texture a scene names, described, and the storage those descriptions point into.
    /// There is one description for each live slot: a slot the scene has given back is passed over.
    /// `TextureData` carries spans rather than bytes, so this holds the images and owns the level
    /// table while a backend reads them. Non-copyable because the descriptions point into its own
    /// vectors; held for the life of its owner and refilled per arrival, so every buffer settles at
    /// the busiest cell.
    class SceneTextures
    {
    public:
        SceneTextures() = default;

        SceneTextures(const SceneTextures&) = delete;
        SceneTextures& operator=(const SceneTextures&) = delete;
        ~SceneTextures() = default;

        /// Resolves and describes every texture `scene` still names, in table order, for a backend
        /// building an array from nothing. The free slots are not among them. A texture that cannot
        /// be described is described as the stand-in and refused, and so is the array running out
        /// of room.
        ///
        /// @param prepared where a normal or a data map's image with its chain made again is found
        ///        (`PreparedImages`), or null for none: a file whose image is still being made stands
        ///        in, and is named in `getPending`.
        /// @param settled whether to wait for the images the slots need before describing them, so none
        ///        is pending.
        void describeAll(const SceneDesc& scene, PreparedImages* prepared, bool settled);

        /// The same, for `slots` and nothing else — what stops a texture being described twice. A
        /// list and not an offset, because a slot a departing cell freed is taken over wherever it
        /// sits. **A slot the device already stands whole is left as it stands while its file is read
        /// again** (a want moved finer, `TextureTable::arriveFiner`), and named in `getPending`: stood
        /// in meanwhile, it drew the stand-in's grey, and a ground composite baked then kept it.
        void describe(
            const SceneDesc& scene, std::span<const TextureIndex> slots, PreparedImages* prepared, bool settled);

        /// A slot the last `describe` stood in for while its image with its chain made again is made: the
        /// file the image is made of, which the slot's owner asks after. Stood in for and not stood as its
        /// file, so the device is handed each file once, the image it keeps.
        struct Pending
        {
            TextureIndex mSlot;
            osg::ref_ptr<const osg::Image> mSource;
            TextureEncoding mEncoding;
            osg::ref_ptr<const osg::Image> mPartner;
        };

        std::span<const Pending> getPending() const { return mPending; }

        /// A file slot the last `describe` described, and the bytes of the whole image it handed the device,
        /// its own or its chain made again: nought where it stood in, or drew the stand-in. What its owner
        /// tells the slot (`TextureTable::stand`), so the slot is said to stand what it was last handed.
        struct Stood
        {
            TextureIndex mSlot;
            std::uint64_t mBytes = 0;

            /// Whether it is the file's chain made again.
            bool mMade = false;

            /// The level it was handed whole from, its readers' want (`TextureData::mWant`).
            std::uint8_t mFrom = 0;
        };

        std::span<const Stood> getStood() const { return mStood; }

        /// What the last `describe` found, each carrying the slot it goes to in `TextureData::mSlot`.
        std::span<const TextureData> getDescriptions() const { return mDescriptions; }

        /// What the last `describe` refused, for whoever owns the scene to report: the scene is
        /// read here, because a read-only caller describes it too.
        std::span<const Refusal> getRefusals() const { return mRefusals; }

        /// Lets go of the texels and chains the last `describe` held for its descriptions, once the backend
        /// has read them: until the next arrival they would keep a cell's texels on the host.
        void letGo();

    private:
        /// Whether a described slot is one of an array built from nothing, which stands in for what is still
        /// read, or an arrival into one that stands it already.
        enum class Handing
        {
            Whole,
            Arrivals,
        };

        /// What one `describe` is asked: the slots, where their images are found, whether to wait for them, and into
        /// what they are handed.
        struct Ask
        {
            std::span<const TextureIndex> mSlots;
            PreparedImages* mPrepared = nullptr;
            bool mSettled = false;
            Handing mHanding = Handing::Whole;
        };

        void describeSlots(const SceneDesc& scene, const Ask& ask);

        /// The chunk a composite slot flattens, and which of its two layers the slot holds.
        struct Ground
        {
            MaterialIndex mMaterial = sNoRow;
            bool mGloss = false;

            /// Where its layers' slots stand in `mReads`, which the description hands the device
            /// (`TextureData::mReads`).
            std::size_t mReadsAt = 0;
            std::size_t mReadsCount = 0;
        };

        /// One slot `describe` decided to describe, and what resolving it found.
        struct Kept
        {
            TextureIndex mSlot = sNoRow;

            /// What the scene's table binds the slot as, which the file's bytes are read as.
            TextureEncoding mEncoding = TextureEncoding::Colour;

            /// For a bake, the slot of the sprite texture it is made from on the device, or
            /// `sNoRow` where the table no longer holds that. Nothing for a slot that is no bake.
            std::optional<TextureIndex> mBakedFrom{};

            /// The file's image, or why none reads. Null for a slot that names no file. Its finest level
            /// alone where `mMade` is not null.
            Result<osg::ref_ptr<const osg::Image>, std::string> mImage = osg::ref_ptr<const osg::Image>();

            /// The file's chain made again, which the description takes its levels after the finest from.
            osg::ref_ptr<const MadeImage> mMade{};

            /// The row's `TextureRow::mFormat`, which the reserve and the description both read.
            TextureFormat mFormat = TextureFormat::Unnamed;

            /// For a chunk's flattened ground, which chunk and which layer. Nothing for a slot that
            /// is no composite.
            std::optional<Ground> mGround{};

            /// Whether the slot stands in until its chain is made: a normal map of two channels, which
            /// the trace does not decode (`decodeNormal`), and a classic `_spec`, whose channels are no
            /// metal-roughness map until it is converted.
            bool mStandsIn = false;
        };

        /// What `kept` is described as, or why it gets the stand-in.
        Result<TextureData, std::string> describeKept(const Kept& kept);

        // Refilled by every `describe` and never freed, so each settles at the busiest arrival so
        // far — which is where the room to grow one is least.

        /// The slots `describe` kept, because a free one is passed over and the descriptions are
        /// no longer one per entry of what it was asked for.
        std::vector<Kept> mKept;
        std::vector<Pending> mPending;
        std::vector<Stood> mStood;

        /// What a settled describe's first asks found, held until each slot's own ask: what the service
        /// hands out once and then only watches, which nothing else held between the two.
        std::vector<PreparedImage> mFound;

        /// Every image's levels, back to back. One table rather than one vector each: a cell reaches
        /// a couple of hundred textures, and the descriptions want a span into something stable.
        std::vector<MipLevel> mLevels;

        /// Every laid image's texels, back to back, for the same reason.
        std::vector<std::byte> mTexels;

        std::vector<TextureData> mDescriptions;

        /// Every slot of the scene's table, which is what a rebuild asks about. Held rather than
        /// built, because a rebuild is a fifth of a second and none of it should be this.
        std::vector<TextureIndex> mEverything;

        /// Every composite's layer slots, back to back, which its description spans: filled before the first
        /// description is taken, and never grown while one spans it.
        std::vector<TextureIndex> mReads;

        std::vector<Refusal> mRefusals;
    };
}
