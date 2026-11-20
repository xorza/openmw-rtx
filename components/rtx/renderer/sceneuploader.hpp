#pragma once

#include <cstddef>
#include <cstdint>

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
    };
}
