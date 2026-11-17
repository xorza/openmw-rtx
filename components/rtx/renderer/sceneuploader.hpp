#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <osg/Image>
#include <osg/ref_ptr>

#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/texturewrap.hpp>
#include <components/rtx/preprocess/preparedimages.hpp>

#include "arrivalwindow.hpp"
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

        /// **One part in this many of what the budget leaves is other programs'**, the denominator: Windows lowers a
        /// process's budget when another wants memory, and Linux tells no one, so this is what a browser or the
        /// compositor finds free before the next reading sees their usage (`docs/architecture.md`, "The limit is
        /// derived from the budget").
        static constexpr std::uint64_t sOthersShare = 10;

        /// **What everything that rests may stand**: what `room`'s budget leaves of what is in use, less other
        /// programs' share, where in use is what the process holds less the `resting` bytes in it, so that what rests
        /// does not shrink its own limit. Nought where use fills the budget.
        static constexpr std::uint64_t restLimitOf(const ArrivalRoom& room, const std::uint64_t resting)
        {
            const std::uint64_t inUse = room.mHeld > resting ? room.mHeld - resting : 0;
            const std::uint64_t left = room.mBudget > inUse ? room.mBudget - inUse : 0;
            return left - left / sOthersShare;
        }

    private:
        /// Takes what the device made of a hand-over: reports what it could not stand with the scene's own refusals,
        /// stands each mesh's structure in the scene, which is written here and which the backend holds const, and
        /// files what each texture and tight copy gives back once it rests. Answers the refusals, valid until the next
        /// take; what waits on room is served at the next hand-over (`serveWants`).
        std::span<const Refusal> takeAnswer(SceneHolder& scenes, SceneSlot slot, SceneDesc& scene);

        /// Counts a hand-over against every stand-in that waits, refusing into `scene` one that waited its most, and
        /// forgets a smaller side that tried its most and those whose slot holds their file no more; asks `prepared` to
        /// read again the file of each the device's `free` bytes now hold and no eviction for it is coming back to
        /// (`isCooling`), and hands over again each read — each at once where nothing reads them or the hand-over is
        /// `settled`, whose description reads and waits.
        void serveWaits(SceneDesc& scene, PreparedImages* prepared, bool settled, std::uint64_t free);

        /// Frees what rests, the oldest first, until the device's room holds an arrival of `arriving` bytes of textures
        /// beside the structures it may build.
        void evictForRoom(SceneDesc& scene, const ArrivalRoom& room, std::uint64_t arriving);

        /// **Evicts for each want of the last answer what surely leaves it room** (`SceneDesc::evictFor`), once: a want
        /// evicted for cools until the frames that may still read what went are done, the device has asked again, and
        /// its answer is read, then is evicted for again if the device still waits, the room taken by another first.
        void serveWants(SceneDesc& scene);

        /// Whether a want of `kind` for `slot` was evicted for and its room is not yet the device's.
        bool isCooling(RestKind kind, Index slot) const;

        /// Files the textures the last answer named stood short, and forgets those this hand-over described and the
        /// device stood whole: a wait handed over again, or every one under a rebuild, which describes the whole table.
        void waitForRoom(const TextureTable& textures);

        /// Whether `slot` waits for room.
        bool isWaiting(Index slot) const;

        /// What an arrival is described into, and the storage the descriptions point at. Held, so
        /// an arrival frame does not pay for the buffers; nothing reads them between calls.
        SceneTextures mTextures;

        /// The slots stood in for while their images are made: asked after at every hand-over, and
        /// stood once made (`SceneTextures::getPending`).
        std::vector<SceneTextures::Pending> mPending;

        /// The most bytes one recent extension has handed the device, of its arriving files and the structures it
        /// built: the room what rests leaves free (`SceneDesc::evictResting`), and the step it comes down to its limit
        /// by (`RestLimit::mStep`); and of the structures alone, the room an arrival's structures may take, which only
        /// the device knows once it built them, beside its textures, which the descriptions say before
        /// (`evictForRoom`). Not a rebuild's, which hands over the whole world and would pace nothing.
        ArrivalWindow mArrivals;

        /// What the device made of a hand-over (`SceneHolder::takeAnswer`), refilled each time.
        DeviceAnswer mAnswer;

        /// **A texture the device stood short, waiting for the room its whole file takes** (`DeviceAnswer::mShort`):
        /// its slot, and the file it waits with, which the slot may give up or give another, as a pending one does
        /// (`mPending`); the file's bytes and what the device stands of it, whose difference the room kept counts; how
        /// many hand-overs a stand-in has waited, or how many times a smaller side was handed over and stood short
        /// again, up to `sMostRoomWaits`, past which a stand-in is refused and a smaller side stands as it is; and
        /// whether its texels are being read again for the hand-over.
        /// A wait the hand-over stood whole goes, and with it what was read.
        struct TextureWait
        {
            Index mSlot = sNoIndex;
            osg::ref_ptr<const osg::Image> mSource;
            std::uint64_t mFileBytes = 0;
            std::uint64_t mStoodBytes = 0;
            std::uint32_t mWaited = 0;
            bool mReading = false;

            /// How the read was asked, while `mReading`: what a wait that ends before it was handed forgets
            /// (`PreparedImages::forget`), its slot perhaps another file's by then.
            TextureEncoding mEncoding{};
            TextureWrap mWrap{};
            osg::ref_ptr<const osg::Image> mPartner;

            /// What the read handed back where it was ready at once, held until the hand-over describes it.
            PreparedImage mRead{};

            /// What the device's room has to hold for it to stand whole: what it lacks of its file.
            std::uint64_t getLacking() const { return mFileBytes - std::min(mStoodBytes, mFileBytes); }
        };

        /// The textures waiting for room, each slot once, so no more than the texture table's capacity.
        std::vector<TextureWait> mTextureWaits;

        /// A want evicted for (`serveWants`): whose, and the hand-over from which the room its eviction gave back is
        /// the device's.
        struct WantCooling
        {
            RestKind mKind = RestKind::Texture;
            Index mSlot = sNoIndex;
            std::uint64_t mUntil = 0;
        };

        /// The wants evicted for whose room is still coming, each kind and slot once: no more than the device waits on,
        /// the textures' capacity and the meshes waiting.
        std::vector<WantCooling> mCooling;

        /// The hand-overs made, the clock a want cools by.
        std::uint64_t mHandOvers = 0;
    };
}
