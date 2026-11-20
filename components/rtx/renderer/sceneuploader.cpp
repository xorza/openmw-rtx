#include "sceneuploader.hpp"

#include <chrono>
#include <cstdint>
#include <limits>
#include <string>

#include <components/rtx/common/clock.hpp>
#include <components/rtx/scene/compositequeue.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/scene/refusals.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/texturetable.hpp>

#include "framespend.hpp"
#include "sceneholder.hpp"
#include "slot.hpp"

namespace Rtx
{
    namespace
    {
        /// What a composite's queue asks of residency of a layer: whether its texture stands at all.
        class ResidentLayers final : public LayerStanding
        {
        public:
            ResidentLayers(const Residency& residency, const SceneSlot slot)
                : mResidency(residency)
                , mSlot(slot)
            {
            }

            bool stands(const TextureIndex texture) const override
            {
                return mResidency.standsFrom(mSlot, texture, std::numeric_limits<std::uint8_t>::max());
            }

        private:
            const Residency& mResidency;
            SceneSlot mSlot;
        };
    }

    SceneUpload SceneUploader::hand(SceneHolder& scenes, const Handing& handing)
    {
        const SceneSlot slot = handing.mSlot;
        SceneDesc& scene = handing.mScene;
        Residency& residency = handing.mResidency;

        // Timed into a row nobody reads where the caller handed none, so the stretches below are written once each
        // rather than guarded.
        FrameSpend unread;
        FrameSpend& timed = handing.mSpend != nullptr ? *handing.mSpend : unread;

        // Whether the backend holds this scene in this slot: appending onto a slot something else filled would begin
        // past the end of this scene's own tables.
        const SceneHeld held = scenes.describeHeld(slot);
        const bool mine = held.mIdentity == scene.getIdentity();

        // Here rather than where a walk ends, because a scene can be walked more than once: the game walks its
        // precipitation beside its world, and a light met by the second walk would be outside an order the first had
        // settled. `SceneDesc::orderLights` says what depends on it.
        scene.orderLights();

        // The halves of the hand-over are timed apart, because a frame that stalls stalls in one of them and the
        // profile cannot say which.
        const std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
        const std::size_t baked
            = handing.mComposites != nullptr ? handing.mComposites->advance(scene, ResidentLayers(residency, slot)) : 0;
        const std::chrono::steady_clock::time_point gathered = std::chrono::steady_clock::now();
        timed.at(Timing::Bake) = since(began, gathered);

        // **A slot built anew holds nothing residency stood of the scene before**, so nothing rests on it; one that
        // stands keeps what rests within its bound.
        if (!mine)
            scene.releaseResting();
        else
            scene.boundResting();

        // After the call above, because it rewrites what the spans reach.
        scene.compact();
        const SceneDesc& tables = scene;

        SceneUpload done;
        const bool arrived = !mine || tables.getStructureRevision() != held.mStructureRevision || baked > 0;
        done.mKind = !mine ? SceneUpload::Kind::Rebuilt
            : arrived      ? SceneUpload::Kind::Extended
                           : SceneUpload::Kind::Placed;
        done.mArrivedMeshes = static_cast<std::uint32_t>(!mine ? tables.meshes().getLiveCount()
                : arrived                                      ? tables.meshes().getArrived().size()
                                                               : 0);
        done.mDropped = tables.textures().getFreed().size();

        // **A slot built anew holds nothing residency stood of the scene before**: what stood went with the old build,
        // so residency forgets it, and every texture draws the stand-in until it stands again.
        if (!mine)
        {
            if (residency.holds(slot))
                residency.forgetScene(slot, handing.mFeed);
            residency.addScene(slot);
            scenes.standScene(slot, tables);
        }

        // **What arrived appended, residency's operations done, and the scene placed: one submit.**
        std::ignore = residency.handOver(Residency::HandOver{
            .mDevice = scenes, .mSlot = slot, .mScene = tables, .mFeed = handing.mFeed, .mShares = handing.mShares });
        scene.refusals().refuse(residency.getRefused());
        for (const Residency::StandingChange& change : residency.getStandingChanges())
            scene.standTexture(change.mTexture, change.mStands);
        for (const Residency::StructureStandingChange& change : residency.getStructureStandingChanges())
            scene.standMesh(change.mMesh, change.mStands);
        timed.at(Timing::Upload) = since(gathered, std::chrono::steady_clock::now());

        // **The array's own limit, met where a texture was added and not where one stands**: one refusal for what
        // this hand-over could not take, the text built once.
        if (tables.textures().getRefusedArrivals() > 0)
        {
            static const std::string past
                = "past the " + std::to_string(TextureTable::sCapacity) + " textures the array holds";
            scene.refusals().refuse(Refused::Texture, {}, past);
        }

        // Every hand-over has read the scene's arrivals.
        scene.clearArrivals();

        // Last, after everything has read what moved: every hand-over, a picture's too, whose scene would otherwise
        // hold every change it ever had.
        scene.placements().advance();

        return done;
    }
}
