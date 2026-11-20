#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "arrivalwindow.hpp"
#include "framespend.hpp"
#include "residency.hpp"
#include "sceneholder.hpp"
#include "slot.hpp"

namespace Rtx
{
    class CompositeQueue;
    class SceneDesc;
    class TextureFeed;

    /// What handing a mirrored scene to a renderer came to.
    struct SceneUpload
    {
        enum class Kind
        {
            /// Nothing arrived: what residency issued was done and the slots that moved had their transforms
            /// rewritten.
            Placed,
            /// Geometry arrived and was appended — new structures built, everything already there left where it
            /// was.
            Extended,
            /// This uploader had built nothing for this renderer and this scene, so the slot was built from the whole
            /// table, every texture drawing the stand-in until residency stands it.
            Rebuilt,
        };

        Kind mKind = Kind::Placed;

        /// How many meshes the scene gained since the last hand-over, which an `Extended` or a `Rebuilt` upload built
        /// structures for on the frame's own queue. Nought for a `Placed`.
        std::uint32_t mArrivedMeshes = 0;

        /// How many texture slots the scene gave back.
        std::size_t mDropped = 0;
    };

    /// **Hands a mirrored scene to a renderer once a frame**, written once for the game and the harness: a first build
    /// where the backend holds nothing of the scene, and otherwise residency's hand-over, which appends what arrived,
    /// does its operations and places (`Residency::handOver`). Stateless against the backend, which says what it holds
    /// through `SceneHolder::describeHeld`.
    class SceneUploader
    {
    public:
        /// What one hand-over is of.
        struct Handing
        {
            /// Which of the renderer's scenes. A doll takes the same branches a cell does: a slider drag redraws the
            /// same subject sixty times a second.
            SceneSlot mSlot;

            /// Walked this frame, and mutable because its arrivals are consumed here, so a caller cannot upload them
            /// twice or lose them.
            SceneDesc& mScene;

            /// What decides what stands, and where the texels come from: one each for every scene of a renderer.
            Residency& mResidency;
            TextureFeed& mFeed;

            /// The world's terrain baker, or null for a scene with no distant ground: a doll and a map tile have none
            /// to flatten.
            CompositeQueue* mComposites = nullptr;

            /// Where the halves of the hand-over are timed into, or null — `Bake` and `Upload`. Timed here, because a
            /// backend that timed itself would be answering a question about the host's frame.
            FrameSpend* mSpend = nullptr;

            /// What the frame issues at most.
            Residency::Shares mShares{};
        };

        /// Hands the scene to `scenes`, building only what has to be built, and ends the placement it handed over —
        /// `PlacementTable::advance`. Here and nowhere else, because the hand-over is the one thing that reads the
        /// change lists through a backend.
        SceneUpload hand(SceneHolder& scenes, const Handing& handing);

        /// **One part in this many of what the budget leaves is other programs'**, the denominator: Windows lowers a
        /// process's budget when another wants memory, and Linux tells no one, so this is what a browser or the
        /// compositor finds free before the next reading sees their usage (`docs/architecture.md`, "The limit is
        /// derived from the budget").
        static constexpr std::uint64_t sOthersShare = 10;

        /// **What the meshes that rest may stand**: what `room`'s budget leaves of what is in use, less other
        /// programs' share, where in use is what the process holds less the `resting` bytes in it, so that what rests
        /// does not shrink its own limit. Nought where use fills the budget.
        static constexpr std::uint64_t restLimitOf(const ArrivalRoom& room, const std::uint64_t resting)
        {
            const std::uint64_t inUse = room.mHeld > resting ? room.mHeld - resting : 0;
            const std::uint64_t left = room.mBudget > inUse ? room.mBudget - inUse : 0;
            return left - left / sOthersShare;
        }

    private:
        /// Takes what the device made of the scene's meshes: reports what it could not stand with the scene's own
        /// refusals, stands each mesh's structure in the scene, which is written here and which the backend holds
        /// const, and files what each tight copy gives back once it rests.
        void takeAnswer(SceneHolder& scenes, SceneSlot slot, SceneDesc& scene);

        /// **Evicts for each mesh the last answer said waits what surely leaves it room** (`SceneDesc::evictFor`),
        /// once: a want evicted for cools until the frames that may still read what went are done, the device has
        /// asked again, and its answer is read, then is evicted for again if the device still waits.
        void serveWants(SceneDesc& scene);

        /// Whether a want for `mesh` was evicted for and its room is not yet the device's.
        bool isCooling(MeshIndex mesh) const;

        /// The most bytes one recent extension's structures took, which only the device knows once it built them: the
        /// room an arrival's structures may take (`SceneDesc::evictResting`), and the step what rests comes down to
        /// its limit by. Not a rebuild's, which hands over the whole world and would pace nothing.
        ArrivalWindow mArrivals;

        /// What the device made of a hand-over (`SceneHolder::takeAnswer`), refilled each time.
        DeviceAnswer mAnswer;

        /// A want evicted for (`serveWants`): whose, and the hand-over from which the room its eviction gave back is
        /// the device's.
        struct WantCooling
        {
            MeshIndex mMesh;
            std::uint64_t mUntil = 0;
        };

        /// The wants evicted for whose room is still coming, each mesh once: no more than the device waits on.
        std::vector<WantCooling> mCooling;

        /// The hand-overs made, the clock a want cools by.
        std::uint64_t mHandOvers = 0;
    };
}
