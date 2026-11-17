#include "sceneuploader.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <components/debug/debuglog.hpp>
#include <components/rtx/common/clock.hpp>
#include <components/rtx/common/mebibytes.hpp>
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

    void SceneUploader::serveWaits(
        SceneDesc& scene, PreparedImages* const prepared, const bool settled, std::uint64_t free)
    {
        // **Each hand-over is a wait more for a stand-in**, whether the room came or not, so a room that never comes
        // ends in a refusal and not in a stand-in for good. A smaller side draws its texture, and waits until it was
        // handed over and stood short again its most (`waitForRoom`), then stands as it is. One whose slot the scene
        // gave back, or gave another file, waits no more. Each lets go of what was read for it.
        std::erase_if(mTextureWaits, [&](TextureWait& wait) {
            const TextureTable& textures = scene.textures();
            const bool same = textures.isLive(wait.mSlot) && textures.getRows()[wait.mSlot].mImage == wait.mSource;
            const bool standIn = wait.mStoodBytes == 0;
            if (same && standIn)
                ++wait.mWaited;
            const bool waitedMost = same && wait.mWaited >= sMostRoomWaits;
            if (waitedMost && standIn)
                scene.refusals().refuse(
                    Refused::Texture, textures.getRows()[wait.mSlot].getName(), "no device memory is left for it");
            if (same && !waitedMost)
                return false;
            if (prepared != nullptr && wait.mReading)
                prepared->forget(*wait.mSource, wait.mEncoding, wait.mWrap, wait.mPartner.get());
            return true;
        });

        // **Handed over again once the room holds the file and its texels are read**: the host let go of them once the
        // device had its short copy, so they are read again first, where something reads them (`PreparedImages`),
        // and the slot keeps what it stands meanwhile. A settled hand-over reads them as it describes, and waits for
        // them, so two runs hand the slot over on one hand-over. The room is what rested went for, a hand-over or
        // more before, back once the frames that held it are done.
        for (TextureWait& wait : mTextureWaits)
        {
            // Not while what was evicted for it is coming back: handed over first, it stands short again.
            const std::uint64_t lacking = wait.getLacking();
            if (!wait.mReading && (lacking > free || isCooling(RestKind::Texture, wait.mSlot)))
                continue;
            const TextureRow& row = scene.textures().getRows()[wait.mSlot];
            if (prepared != nullptr && !settled)
            {
                if (!wait.mReading)
                {
                    wait.mReading = true;
                    wait.mEncoding = row.mEncoding;
                    wait.mWrap = row.mWrap;
                    wait.mPartner = row.mPartner;
                    free -= lacking;
                    // Held until the hand-over describes it: what is read is the asker's to hold once handed out.
                    wait.mRead = prepared->find(*wait.mSource, row.mEncoding, row.mWrap, row.mPartner.get());
                    if (wait.mRead.mPending)
                        continue;
                }
                else if (!prepared->isReady(*wait.mSource, row.mEncoding, row.mWrap, row.mPartner.get()))
                    continue;
            }
            else
                free -= lacking;
            scene.textures().arriveAgain(wait.mSlot);
        }
    }

    void SceneUploader::evictForRoom(SceneDesc& scene, const ArrivalRoom& room, const std::uint64_t arriving)
    {
        // **Each against its own room**: the textures against the pools', the structures an arrival may build against
        // the storages'; what rests gives back to either.
        const std::uint64_t structures = mArrivals.largest().mStructures;
        const std::uint64_t textureShort = room.mFree < arriving ? arriving - room.mFree : 0;
        const std::uint64_t structureShort = room.mStructureFree < structures ? structures - room.mStructureFree : 0;
        scene.evictResting(textureShort + structureShort);
    }

    bool SceneUploader::isCooling(const RestKind kind, const Index slot) const
    {
        return std::ranges::any_of(
            mCooling, [&](const WantCooling& cooling) { return cooling.mKind == kind && cooling.mSlot == slot; });
    }

    void SceneUploader::serveWants(SceneDesc& scene)
    {
        // The last answer's, served ahead of the placement that reports what goes, as the device reads freed slots
        // there; its room comes back once the frames in flight past it are done, and the device asks again at the
        // placement after that, whose answer the hand-over after reads.
        for (const RoomWant& want : mAnswer.mWants)
        {
            if (isCooling(want.mKind, want.mSlot))
                continue;
            scene.evictFor(std::span(&want, 1));
            mCooling.push_back(
                WantCooling{ .mKind = want.mKind, .mSlot = want.mSlot, .mUntil = mHandOvers + sFramesInFlight + 1 });
        }
    }

    void SceneUploader::waitForRoom(SceneDesc& scene, const std::span<const SceneTextures::Stood> described)
    {
        const TextureTable& textures = scene.textures();
        const auto shortOf = [&](const Index slot) {
            return std::ranges::find(mAnswer.mShort, slot, &ShortTexture::mSlot) != mAnswer.mShort.end();
        };
        std::erase_if(mTextureWaits, [&](const TextureWait& wait) {
            return !shortOf(wait.mSlot)
                && std::ranges::find(described, wait.mSlot, &SceneTextures::Stood::mSlot) != described.end();
        });

        // A new one waits from nothing, for its file's bytes, with the file its slot holds; one stood short again
        // stands what the device stood now, and its texels are read again when the room comes back.
        for (const ShortTexture& stood : mAnswer.mShort)
        {
            // **Moved down where the host holds no file to hand over again**: a slot the scene let go of since, which
            // the next placement drops, and one that rests, which a rest holds whole and so goes at the next hand-over
            // (`settleMovedResting`): freed now, it would be freed after this one's placement and never dropped.
            if (!textures.isLive(stood.mSlot))
                continue;
            if (textures.getRows()[stood.mSlot].mRest.mResting)
            {
                mMovedResting.push_back(stood);
                continue;
            }
            fileWait(scene, stood, described);
        }
    }

    void SceneUploader::settleMovedResting(SceneDesc& scene)
    {
        // Still at rest, it goes; taken again since, it stands short of the file its taker holds, and waits for it.
        for (const ShortTexture& stood : mMovedResting)
        {
            if (!scene.textures().isLive(stood.mSlot))
                continue;
            const TextureRow& row = scene.textures().getRows()[stood.mSlot];
            if (row.mRest.mResting)
                scene.evictRestingTexture(stood.mSlot);
            else if (row.mImage != nullptr)
                fileWait(scene, stood, {});
        }
        mMovedResting.clear();
    }

    void SceneUploader::fileWait(
        SceneDesc& scene, const ShortTexture& stood, const std::span<const SceneTextures::Stood> described)
    {
        const TextureTable& textures = scene.textures();

        const auto known = std::ranges::find(mTextureWaits, stood.mSlot, &TextureWait::mSlot);
        if (known != mTextureWaits.end())
        {
            // Handed over again and stood short again: a smaller side's wait more, which bounds its tries.
            if (known->mStoodBytes > 0
                && std::ranges::find(described, stood.mSlot, &SceneTextures::Stood::mSlot) != described.end())
                ++known->mWaited;
            known->mStoodBytes = stood.mStoodBytes;
            known->mReading = false;
            known->mRead = PreparedImage{};
            return;
        }
        // What the file asks of the room: as this hand-over described it, or, for a texture the device moved down
        // to make room, the bytes it stood before (`DeviceAnswer::mShort`).
        const auto whole = std::ranges::find(described, stood.mSlot, &SceneTextures::Stood::mSlot);
        assert((whole != described.end() || stood.mWholeBytes > 0)
            && "a texture stood short this hand-over did not describe and did not move");
        const osg::Image* const file = textures.getRows()[stood.mSlot].mImage.get();
        assert(file != nullptr && "a texture stood short with no file to hand over again");
        mTextureWaits.push_back(TextureWait{ .mSlot = stood.mSlot,
            .mSource = file,
            .mFileBytes = whole != described.end() ? whole->mBytes : stood.mWholeBytes,
            .mStoodBytes = stood.mStoodBytes,
            .mWaited = 0,
            .mReading = false,
            .mEncoding = {},
            .mWrap = {},
            .mPartner = nullptr,
            .mRead = {} });

        // **A file short of its whole rests on nothing**, as one the hand-over stood short does
        // (`TextureTable::stand`): moved down, it stood its whole until now.
        if (whole == described.end())
            scene.textures().stand(stood.mSlot, 0, textures.getRows()[stood.mSlot].mStoodMade, 0);
    }

    std::span<const Refusal> SceneUploader::takeAnswer(SceneHolder& scenes, const SceneSlot slot, SceneDesc& scene)
    {
        mAnswer.clear();
        scenes.takeAnswer(slot, mAnswer);
        scene.refusals().refuse(mAnswer.mRefused);

        // **And what rested beside a mesh the device left out**, the room an eviction had to give: textures and meshes
        // apart, since the two stand in memory one cannot always take of the other. A mesh is left out for room
        // alone; a texture may be refused for its side, which rest has nothing to do with.
        if (std::ranges::any_of(
                mAnswer.mRefused, [](const Refusal& refusal) { return refusal.mKind == Refused::Mesh; }))
            Log(Debug::Warning) << std::format(
                "Ray tracing refused content beside {} resting textures of {:.1f} MiB "
                "and {} resting meshes of {:.1f} MiB",
                scene.textures().getResting().getCount(), mebibytes(scene.textures().getResting().getBytes()),
                scene.meshes().getResting().getCount(), mebibytes(scene.meshes().getResting().getBytes()));
        for (const StoodMesh& stood : mAnswer.mMeshes)
            scene.standMesh(stood.mMesh, stood.mStructureBytes);
        // A slot the scene let go of before the device moved it down fits nothing.
        for (const FitBytes& fit : mAnswer.mTextureFits)
            if (scene.textures().isLive(fit.mSlot))
                scene.textures().fit(fit.mSlot, fit.mBytes);
        for (const FitBytes& fit : mAnswer.mMeshFits)
            scene.fitMesh(fit.mSlot, fit.mBytes);

        // **What moved down and stands no coarser than its readers want stands as it is now**: nothing is handed over
        // again, and the slot says what the device holds of it. One let go of since, or laid to rest, says nothing.
        TextureTable& textures = scene.textures();
        for (const MovedTexture& moved : mAnswer.mMoved)
            if (textures.isLive(moved.mSlot) && !textures.getRows()[moved.mSlot].mRest.mResting)
                textures.stand(moved.mSlot, moved.mStoodBytes, textures.getRows()[moved.mSlot].mStoodMade, moved.mFrom);
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

        // What the device moved down while it rested goes ahead of the placement that drops what the scene let go of.
        settleMovedResting(scene);

        // A hand-over more on the clock a want cools by, and a want whose room is the device's again cools no more.
        ++mHandOvers;
        std::erase_if(mCooling, [&](const WantCooling& cooling) { return cooling.mUntil <= mHandOvers; });

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
                    handing.mPrepared->forget(
                        *pending.mSource, pending.mEncoding, pending.mWrap, pending.mPartner.get());
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
        const std::optional<ArrivalRoom> room = mine ? scenes.getArrivalRoom(slot) : std::nullopt;
        if (!mine)
            scene.evictResting(std::numeric_limits<std::uint64_t>::max());
        else if (room.has_value())
        {
            // **And never past their limit, coming down to it a step a hand-over** (`docs/architecture.md`, "One
            // residency policy for the content").
            scene.setRestLimit(
                RestLimit{ .mBytes = restLimitOf(*room, scene.getRestingBytes()), .mStep = mArrivals.largest().mAll });
            serveWants(scene);
            serveWaits(scene, handing.mPrepared, handing.mSettled, room->mFree);
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
            if (room.has_value())
                evictForRoom(scene, *room, 0);
            done.mDropped = tables.textures().getFreed().size();
            scenes.placeScene(slot, tables);

            // What a placement could not stand: a structure that waited for room it found is not coming. And what
            // moved down for room since the last hand-over, which waits to be handed over whole again.
            takeAnswer(scenes, slot, scene);
            waitForRoom(scene, {});

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
                // **What rests gives room to the arrival at hand**: its textures' whole files, as described, beside
                // what its structures may take. The device takes a freed image back only once the frames that may
                // name it are done, so what still does not fit stands short and is handed over again (`serveWaits`).
                if (room.has_value())
                {
                    std::uint64_t arriving = 0;
                    for (const SceneTextures::Stood& stood : mTextures.getStood())
                        arriving += stood.mBytes;
                    evictForRoom(scene, *room, arriving);
                }

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
            waitForRoom(scene, mTextures.getStood());

            // An extension's, and not a rebuild's, which hands over the whole table and is no arrival a crossing
            // makes: its textures and the structures the device built, as its answer says.
            if (mine)
            {
                std::uint64_t textures = 0;
                for (const SceneTextures::Stood& stood : mTextures.getStood())
                    textures += stood.mBytes;
                std::uint64_t structures = 0;
                for (const StoodMesh& stood : mAnswer.mMeshes)
                    structures += stood.mStructureBytes;
                mArrivals.note(ArrivalBytes{ .mAll = textures + structures, .mStructures = structures });
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
                scene.textures().stand(stood.mSlot, stands ? stood.mBytes : 0, stands && stood.mMade,
                    stands ? stood.mFrom : std::uint8_t{ 0 });
            }

            // **The host lets go of the texels and chains the device now holds**: what the descriptions held, and
            // what the service read and made of each file handed over whole, whose header's manager drops its
            // texels too. A file stood short lets go as well, and is read again for its next hand-over.
            mTextures.letGo();
            if (handing.mPrepared != nullptr)
                for (const SceneTextures::Stood& stood : mTextures.getStood())
                    if (const osg::Image* const file = scene.textures().getRows()[stood.mSlot].mImage.get();
                        file != nullptr && stood.mBytes > 0)
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
