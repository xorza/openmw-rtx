#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <osg/Image>
#include <osg/ref_ptr>

#include <components/rtx/preprocess/preparedimages.hpp>

#include "framespend.hpp"
#include "sceneholder.hpp"
#include "scenetextures.hpp"
#include "slot.hpp"

namespace Rtx
{
    class CompositeQueue;
    class SceneDesc;
    class TextureTable;

    /// What handing a mirrored scene to a renderer came to.
    struct SceneUpload
    {
        enum class Kind
        {
            /// Nothing arrived: the slots that moved had their transforms rewritten and that is all.
            Placed,
            /// Geometry arrived and was appended — new structures built, new textures added to the
            /// array, everything already there left where it was.
            Extended,
            /// This uploader had built nothing for this renderer and this scene, so there was
            /// nothing to append to and every array was made from the whole table.
            Rebuilt,
        };

        Kind mKind = Kind::Placed;

        /// How many meshes the scene gained since the last hand-over, which an `Extended` or a
        /// `Rebuilt` upload built structures for on the frame's own queue. Nought for a `Placed`.
        std::uint32_t mArrivedMeshes = 0;

        /// How many textures had to be described, which is zero on a `Placed`.
        std::size_t mDescribed = 0;

        /// How many texture slots the scene gave back, whose images the renderer was told to drop.
        /// Not zero on a `Placed`: leaving a region is a frame where nothing arrives.
        std::size_t mDropped = 0;
    };

    /// Takes, once a frame, the cheapest of the three ways to hand a mirrored scene over, written
    /// once for the game and the harness: a place is under a millisecond, an extend a few, a
    /// rebuild a fifth of a second, and choosing wrongly is fatal rather than slow. Stateless
    /// against the backend, which says what it holds through `SceneHolder::describeHeld`.
    class SceneUploader
    {
    public:
        /// What one hand-over is of. The three pointers are what a caller may not have.
        struct Handing
        {
            /// Which of the renderer's scenes. A doll takes the same three branches a cell does: a
            /// slider drag redraws the same subject sixty times a second.
            SceneSlot mSlot;

            /// Walked this frame, and mutable because its arrivals are consumed here, so a caller
            /// cannot upload them twice or lose them.
            SceneDesc& mScene;

            /// The world's terrain baker, or null for a scene with no distant ground: a doll and a
            /// map tile have none to flatten.
            CompositeQueue* mComposites = nullptr;

            /// Where the three halves of the hand-over are timed into, or null — `Bake`, `Textures`
            /// and `Upload`. Timed here, because a backend that timed itself would be answering a
            /// question about the host's frame.
            FrameSpend* mSpend = nullptr;

            /// Where a normal or a data map's image with its chain made again is found
            /// (`PreparedImages`), or null for a scene that reads its files as they stand.
            PreparedImages* mPrepared = nullptr;

            /// Whether the hand-over waits for the images its arrivals need before it describes them,
            /// as a settled walk waits for its cells (`CellRing::setSettled`): what makes two processes,
            /// whatever their threads' timing, stand one image on one frame (`SceneTextures::describe`).
            bool mSettled = false;
        };

        /// Hands the scene to `scenes`, building only what has to be built, and ends the
        /// placement it handed over — `PlacementTable::advance`. Here and nowhere else, because the
        /// hand-over is the one thing that reads the change lists through a backend, and a slot that
        /// left the lists unread would stand on the device as it stood before.
        SceneUpload hand(SceneHolder& scenes, const Handing& handing);

        /// How many slots stand their files as they are while their images are made: what a run that
        /// compares frames waits for, beside the cells.
        std::size_t getPendingCount() const { return mPending.size(); }

        /// **What everything that rests may stand on the device**, measured (`RestReuse`): on the desk's tours, a
        /// least-recently-rested cache of 2 GiB kept 97.5% of what woke under the repack and 88.4% under the PBR
        /// replacers, where doubling it bought 2.5 and 7.1 points more.
        static constexpr std::uint64_t sRestMost = std::uint64_t{ 2 } << 30;

        /// And no more than this share of the device's budget for content, the denominator: the rest of it is the
        /// live content's and the frame's, and on a handheld the system's.
        static constexpr std::uint64_t sRestShare = 4;

        /// What everything that rests may stand under a content budget of `budget`: the share, up to the measured
        /// limit.
        static constexpr std::uint64_t restLimitOf(const std::uint64_t budget)
        {
            return std::min(sRestMost, budget / sRestShare);
        }

    private:
        /// Takes what the device made of a hand-over: reports what it could not stand with the scene's own refusals,
        /// and stands each mesh's structure in the scene, which is written here and which the backend holds const.
        /// Answers the refusals, valid until the next take.
        std::span<const Refusal> takeAnswer(SceneHolder& scenes, SceneSlot slot, SceneDesc& scene);

        /// Counts a hand-over against every texture that waits, refusing into `scene` those that waited their most and
        /// forgetting those whose slot holds their file no more, letting `prepared` go of what it read of either, and
        /// hands over again those `free` bytes of the device's room now hold.
        void serveWaits(SceneDesc& scene, PreparedImages* prepared, std::uint64_t free);

        /// Files the textures the last answer named waiting for room, and forgets those this hand-over described and
        /// the device stood: a wait handed over again, or every one under a rebuild, which describes the whole table.
        void waitForRoom(const TextureTable& textures);

        /// Whether `slot` waits for room.
        bool isWaiting(Index slot) const;

        /// What an arrival is described into, and the storage the descriptions point at. Held, so
        /// an arrival frame does not pay for the buffers; nothing reads them between calls.
        SceneTextures mTextures;

        /// The slots stood in for while their images are made: asked after at every hand-over, and
        /// stood once made (`TextureTable::prepare`).
        std::vector<SceneTextures::Pending> mPending;

        /// The most bytes one extension has handed the device, of its arriving files and the structures it built: the
        /// room what rests leaves free (`SceneDesc::evictResting`).
        std::uint64_t mLargestArrival = 0;

        /// What the device made of a hand-over (`SceneHolder::takeAnswer`), refilled each time.
        DeviceAnswer mAnswer;

        /// **A texture the device had no room for, waiting for it** (`DeviceAnswer::mRoomless`): its slot, which draws
        /// the stand-in meanwhile, and the file it waits with, which the slot may give up or give another, as a pending
        /// one does (`mPending`); the bytes it was handed in, which the room kept counts; and how many hand-overs it
        /// has waited, up to `sMostRoomWaits`.
        struct TextureWait
        {
            Index mSlot = sNoIndex;
            osg::ref_ptr<const osg::Image> mSource;
            std::uint64_t mBytes = 0;
            std::uint32_t mWaited = 0;
        };

        /// The textures waiting for room, each slot once, so no more than the texture table's capacity.
        std::vector<TextureWait> mTextureWaits;
    };
}
