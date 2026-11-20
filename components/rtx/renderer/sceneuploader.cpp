#include "sceneuploader.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string>

#include <components/debug/debuglog.hpp>
#include <components/rtx/common/clock.hpp>
#include <components/rtx/common/mebibytes.hpp>
#include <components/rtx/scene/compositequeue.hpp>
#include <components/rtx/scene/deviceanswer.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/scene/refusals.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/texturetable.hpp>

#include "framesinflight.hpp"
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

    bool SceneUploader::isCooling(const MeshIndex mesh) const
    {
        return std::ranges::any_of(mCooling, [&](const WantCooling& cooling) { return cooling.mMesh == mesh; });
    }

    void SceneUploader::serveWants(SceneDesc& scene)
    {
        // The last answer's, served ahead of the placement that reports what goes, as the device reads freed slots
        // there; its room comes back once the frames in flight past it are done, and the device asks again at the
        // placement after that, whose answer the hand-over after reads.
        for (const RoomWant& want : mAnswer.mWants)
        {
            if (isCooling(want.mWaiter))
                continue;
            scene.evictFor(std::span(&want, 1));
            mCooling.push_back(WantCooling{ .mMesh = want.mWaiter, .mUntil = mHandOvers + sFramesInFlight + 1 });
        }
    }

    void SceneUploader::takeAnswer(SceneHolder& scenes, const SceneSlot slot, SceneDesc& scene)
    {
        mAnswer.clear();
        scenes.takeAnswer(slot, mAnswer);
        scene.refusals().refuse(mAnswer.mRefused);

        // **And what rested beside a mesh the device left out**, the room an eviction had to give. At Info, beside the
        // refusal's own record.
        if (std::ranges::any_of(
                mAnswer.mRefused, [](const Refusal& refusal) { return refusal.mKind == Refused::Mesh; }))
            Log(Debug::Info) << std::format("Ray tracing refused content beside {} resting meshes of {:.1f} MiB",
                scene.meshes().getResting().getCount(), mebibytes(scene.meshes().getResting().getBytes()));
        for (const StoodMesh& stood : mAnswer.mMeshes)
            scene.standMesh(stood.mMesh, stood.mStructureBytes);
        for (const FitBytes<MeshIndex>& fit : mAnswer.mMeshFits)
            scene.fitMesh(fit.mSlot, fit.mBytes, fit.mAlignment);
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

        // A hand-over more on the clock a want cools by, and a want whose room is the device's again cools no more.
        ++mHandOvers;
        std::erase_if(mCooling, [&](const WantCooling& cooling) { return cooling.mUntil <= mHandOvers; });

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

        // **The meshes that rest give their room back before anything arrives**: all of it ahead of a rebuild, and
        // otherwise as much as keeps room for an arrival's structures as large as any so far, and for every structure
        // that waits for room it found none for.
        const std::optional<ArrivalRoom> room = mine ? scenes.getArrivalRoom(slot) : std::nullopt;
        if (!mine)
            scene.evictResting(std::numeric_limits<std::uint64_t>::max());
        else if (room.has_value())
        {
            // **And never past their limit, coming down to it a step a hand-over** (`docs/architecture.md`, "One
            // residency policy for the content").
            scene.setRestLimit(RestLimit{
                .mBytes = restLimitOf(*room, scene.getRestingBytes()), .mStep = mArrivals.largest().mStructures });
            serveWants(scene);
            const std::uint64_t structures = mArrivals.largest().mStructures;
            scene.evictResting(room->mStructureFree < structures ? structures - room->mStructureFree : 0);
        }

        // After the calls above, because they rewrite what the spans reach.
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

        // What the device made of the meshes, reported and stood as the scene's.
        takeAnswer(scenes, slot, scene);

        // An extension's structures, and not a rebuild's, which hands over the whole world and is no arrival a
        // crossing makes.
        if (mine && arrived)
        {
            std::uint64_t structures = 0;
            for (const StoodMesh& stood : mAnswer.mMeshes)
                structures += stood.mStructureBytes;
            mArrivals.note(ArrivalBytes{ .mAll = structures, .mStructures = structures });
        }
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
