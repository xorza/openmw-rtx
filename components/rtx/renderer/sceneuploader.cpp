#include "sceneuploader.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <components/rtx/common/clock.hpp>
#include <components/rtx/preprocess/preparedimages.hpp>
#include <components/rtx/scene/compositequeue.hpp>
#include <components/rtx/scene/deviceanswer.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/scene/refusals.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/texturetable.hpp>

#include "framespend.hpp"
#include "roomwaits.hpp"
#include "sceneholder.hpp"
#include "slot.hpp"

namespace Rtx
{
    bool SceneUploader::isWaiting(const Index slot) const
    {
        return std::ranges::any_of(mTextureWaits, [&](const TextureWait& wait) { return wait.mSlot == slot; });
    }

    void SceneUploader::serveWaits(SceneDesc& scene, PreparedImages* const prepared, std::uint64_t free)
    {
        // **Each hand-over is a wait more**, whether the room came or not, so a room that never comes ends in a refusal
        // and not in a stand-in for good. One that waited its most is refused, and draws the stand-in for as long as it
        // stands; one whose slot the scene gave back, or gave another file, waits no more. Either lets go of what was
        // read for it, which a wait kept for its next hand-over.
        std::erase_if(mTextureWaits, [&](TextureWait& wait) {
            const bool same
                = scene.textures().isLive(wait.mSlot) && scene.textures().getRows()[wait.mSlot].mImage == wait.mSource;
            const bool waitedMost = same && ++wait.mWaited >= sMostRoomWaits;
            if (waitedMost)
                scene.refusals().refuse(Refused::Texture, scene.textures().getRows()[wait.mSlot].getName(),
                    "no device memory is left for it");
            if (same && !waitedMost)
                return false;
            if (prepared != nullptr)
                prepared->letGo(*wait.mSource);
            return true;
        });

        // **Handed over again once the room is there**: what rested went for it a hand-over or more before, and its
        // memory is back once the frames that held it are done.
        for (const TextureWait& wait : mTextureWaits)
            if (wait.mBytes <= free)
            {
                scene.textures().arriveAgain(wait.mSlot);
                free -= wait.mBytes;
            }
    }

    void SceneUploader::waitForRoom(const TextureTable& textures)
    {
        const auto roomless
            = [&](const Index slot) { return std::ranges::find(mAnswer.mRoomless, slot) != mAnswer.mRoomless.end(); };
        std::erase_if(mTextureWaits, [&](const TextureWait& wait) {
            return !roomless(wait.mSlot)
                && std::ranges::find(mTextures.getStood(), wait.mSlot, &SceneTextures::Stood::mSlot)
                != mTextures.getStood().end();
        });

        // A new one waits from nothing, for the bytes it was handed in, with the file its slot holds.
        for (const Index slot : mAnswer.mRoomless)
        {
            if (isWaiting(slot))
                continue;
            const auto stood = std::ranges::find(mTextures.getStood(), slot, &SceneTextures::Stood::mSlot);
            assert(stood != mTextures.getStood().end() && "a slot waiting for room this hand-over did not describe");
            const osg::Image* const file = textures.getRows()[slot].mImage.get();
            assert(file != nullptr && "a slot waiting for room with no file to hand over again");
            mTextureWaits.push_back(TextureWait{ .mSlot = slot, .mSource = file, .mBytes = stood->mBytes });
        }
    }

    std::span<const Refusal> SceneUploader::takeAnswer(SceneHolder& scenes, const SceneSlot slot, SceneDesc& scene)
    {
        mAnswer.clear();
        scenes.takeAnswer(slot, mAnswer);
        scene.refusals().refuse(mAnswer.mRefused);
        for (const StoodMesh& stood : mAnswer.mMeshes)
            scene.standMesh(stood.mMesh, stood.mStructureBytes);
        return mAnswer.mRefused;
    }

    SceneUpload SceneUploader::hand(SceneHolder& scenes, const Handing& handing)
    {
        const SceneSlot slot = handing.mSlot;
        SceneDesc& scene = handing.mScene;
        CompositeQueue* const composites = handing.mComposites;
        FrameSpend* const spend = handing.mSpend;

        // Timed into a row nobody reads where the caller handed none, so the three stretches below
        // are written once each rather than guarded three times.
        FrameSpend unread;
        FrameSpend& timed = spend != nullptr ? *spend : unread;

        // Whether the backend holds this scene in this slot: appending onto a slot something else
        // filled would begin the descriptions past the end of this scene's own table.
        const SceneHeld held = scenes.describeHeld(slot);
        const bool mine = held.mIdentity == scene.getIdentity();

        // Here rather than where a walk ends, because a scene can be walked more than once. The
        // game walks its precipitation beside its world, and a light met by the second walk would be
        // outside an order the first had settled. This is the one point every path passes and the
        // last before anything reads them. `SceneDesc::orderLights` says what depends on it.
        scene.orderLights();

        SceneUpload done;

        // The three halves of the hand-over are timed apart, because a frame that stalls stalls
        // in one of them and the profile cannot say which: an arrival frame's time is in the driver,
        // which carries no frame pointer. `Rtx::Timing::Bake` says the rest.
        const std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();

        const std::size_t baked = composites != nullptr ? composites->advance(scene) : 0;

        const std::chrono::steady_clock::time_point gathered = std::chrono::steady_clock::now();
        timed.at(Timing::Bake) = since(began, gathered);

        // **A slot whose texels were read and chain made since it stood in arrives again**, before the
        // arrivals are read, so this hand-over hands it over. One the scene gave back, or gave another file,
        // lets go of what was read for it.
        if (handing.mPrepared != nullptr)
            std::erase_if(mPending, [&](const SceneTextures::Pending& pending) {
                const bool same = scene.textures().isLive(pending.mSlot)
                    && scene.textures().getRows()[pending.mSlot].mImage == pending.mSource;
                if (!same)
                {
                    handing.mPrepared->letGo(*pending.mSource);
                    return true;
                }
                if (!handing.mPrepared->isReady(
                        *pending.mSource, pending.mEncoding, pending.mWrap, pending.mPartner.get()))
                    return false;
                scene.textures().arriveAgain(pending.mSlot);
                return true;
            });

        // **The textures that rest give their room back before anything arrives**: all of it ahead of a
        // rebuild, which describes every slot from the host's images and a slot that rests has none;
        // otherwise as much as keeps room for an arrival as large as any so far, textures and structures
        // together, and for every structure that waits for room it found none for. The device takes a
        // dropped image's memory back only once the frames that may name it are done, a frame or two
        // after the drop: an arrival that finds no room stands its files smaller, and a structure waits.
        if (!mine)
            scene.evictResting(std::numeric_limits<std::uint64_t>::max());
        else if (const std::optional<ArrivalRoom> room = scenes.getArrivalRoom(slot); room.has_value())
        {
            // **And never past their limit, coming down to it a step a hand-over** (`docs/architecture.md`, "One
            // residency policy for the content").
            scene.setRestLimit(
                RestLimit{ .mBytes = restLimitOf(*room, scene.getRestingBytes()), .mStep = mLargestArrival });
            std::uint64_t waiting = 0;
            for (const TextureWait& wait : mTextureWaits)
                waiting += wait.mBytes;
            const std::uint64_t kept = mLargestArrival + room->mWanted + waiting;
            scene.evictResting(room->mFree < kept ? kept - room->mFree : 0);

            serveWaits(scene, handing.mPrepared, room->mFree);
        }

        // After the calls above, because they rewrite what the spans reach.
        scene.compact();
        const SceneDesc& tables = scene;

        // Geometry the walk has not met before has no bottom-level structure and no uploaded
        // texture, which is a cell change and not a frame. A revision and not a set of table sizes,
        // because a crossing loses one cell as it gains another. A frame that only finished a bake
        // is an arrival for everything below even though nothing was walked.
        const bool arrived = !mine || tables.getStructureRevision() != held.mStructureRevision || baked > 0;

        if (!arrived)
        {
            const std::chrono::steady_clock::time_point told = std::chrono::steady_clock::now();

            // Placed before the lists are forgotten, because placing is what consumes the meshes and the
            // textures that went: their structures and images are destroyed there. A departure with
            // nothing arriving is the ordinary way to leave a region, and it is the frame that must not
            // wait for an arrival to give the memory back: walking away from a ring frees its slots and
            // nothing takes them over until the walk reaches the far side of the next one.
            done.mDropped = tables.textures().getFreed().size();
            scenes.placeScene(slot, tables);

            // What a placement could not stand: a structure that waited for room it found is not coming.
            takeAnswer(scenes, slot, scene);

            timed.at(Timing::Upload) = since(told, std::chrono::steady_clock::now());
            done.mKind = SceneUpload::Kind::Placed;
        }
        else
        {
            // Read only across the call below: `TextureData` carries spans into `mTextures`. The
            // whole table where there is nothing to append to, and the arrivals otherwise.
            if (!mine)
                mTextures.describeAll(tables, handing.mPrepared, handing.mSettled);
            else
                mTextures.describe(tables, tables.textures().getArrived(), handing.mPrepared, handing.mSettled);
            if (handing.mSettled)
                assert(mTextures.getPending().empty() && "a settled hand-over left an image to be made");
            mPending.insert(mPending.end(), mTextures.getPending().begin(), mTextures.getPending().end());
            scene.refusals().refuse(mTextures.getRefusals());

            const std::chrono::steady_clock::time_point described = std::chrono::steady_clock::now();
            timed.at(Timing::Textures) = since(gathered, described);

            done.mDescribed = mTextures.getDescriptions().size();
            done.mArrivedMeshes = static_cast<std::uint32_t>(
                mine ? tables.meshes().getArrived().size() : tables.meshes().getLiveCount());

            if (!mine)
            {
                scenes.setScene(slot, tables, mTextures.getDescriptions());
                done.mKind = SceneUpload::Kind::Rebuilt;
            }
            else
            {
                // The extension gives back what went before it stands what arrived: `SceneDesc` keeps the
                // two lists disjoint, and first is where the memory is given back soonest. A build from
                // nothing needs none of this: the array holds no image of what went.
                done.mDropped = tables.textures().getFreed().size();
                scenes.extendScene(slot, tables, mTextures.getDescriptions());
                scenes.placeScene(slot, tables);
                done.mKind = SceneUpload::Kind::Extended;
            }

            // What the device could not stand, reported as what the describe could not: the scene
            // is written here, and the backend holds it const.
            const std::span<const Refusal> refused = takeAnswer(scenes, slot, scene);
            waitForRoom(scene.textures());

            // An extension's, and not a rebuild's, which hands over the whole table and is no arrival a crossing
            // makes: its textures and the structures the device built, as its answer says.
            if (mine)
            {
                std::uint64_t arriving = 0;
                for (const SceneTextures::Stood& stood : mTextures.getStood())
                    arriving += stood.mBytes;
                for (const StoodMesh& stood : mAnswer.mMeshes)
                    arriving += stood.mStructureBytes;
                mLargestArrival = std::max(mLargestArrival, arriving);
            }

            // What the device now has of each slot described, which a slot that comes to rest keeps: nought
            // for one it stood in for, for one that waits for room, and for every slot of an arrival the device
            // refused a texture of, which it names and does not number, so that none rests and wakes as the stand-in
            // it drew.
            const bool refusedTexture = std::ranges::any_of(
                refused, [](const Refusal& refusal) { return refusal.mKind == Refused::Texture; });
            for (const SceneTextures::Stood& stood : mTextures.getStood())
            {
                const bool stands = !refusedTexture && !isWaiting(stood.mSlot);
                scene.textures().stand(stood.mSlot, stands ? stood.mBytes : 0, stands && stood.mMade);
            }

            // **The host lets go of the texels and chains the device now holds**: what the descriptions held, and
            // what the service read and made of each file handed over whole, whose header's manager drops its
            // texels too. A file that waits for room keeps them for its next hand-over.
            mTextures.letGo();
            if (handing.mPrepared != nullptr)
                for (const SceneTextures::Stood& stood : mTextures.getStood())
                    if (const osg::Image* const file = scene.textures().getRows()[stood.mSlot].mImage.get();
                        file != nullptr && stood.mBytes > 0 && !isWaiting(stood.mSlot))
                        handing.mPrepared->letGo(*file);

            timed.at(Timing::Upload) = since(described, std::chrono::steady_clock::now());
        }

        // **The array's own limit, met where a texture was added and not where one is described**:
        // one refusal for what this hand-over could not take, on whichever branch it went, since
        // an add the table refused arrives nowhere and a placement describes nothing. The text
        // built once, and not on every hand-over of a table full for the session.
        if (tables.textures().getRefusedArrivals() > 0)
        {
            static const std::string past
                = "past the " + std::to_string(TextureTable::sCapacity) + " textures the array holds";
            scene.refusals().refuse(Refused::Texture, {}, past);
        }

        // One tail, because all three hand-overs end the same way: each has uploaded, so each is
        // done with the scene's arrivals. Said per branch instead, it is owed by no one branch in
        // particular, so a branch written without it looks finished.
        scene.clearArrivals();

        // Last, after every branch has read what moved: a walk that handed nothing over keeps its
        // lists for the hand-over that will. Every hand-over, a picture's too, whose scene would
        // otherwise hold every change it ever had; a picture reads no motion, because its trace
        // has no past to reproject.
        scene.placements().advance();

        return done;
    }
}
