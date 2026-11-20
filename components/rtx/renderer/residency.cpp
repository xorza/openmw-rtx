#include "residency.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include <components/rtx/image/imagedescription.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/meshtable.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/texturetable.hpp>

namespace Rtx
{
    namespace
    {
        constexpr std::uint64_t ceilDiv(const std::uint64_t value, const std::uint64_t by)
        {
            return (value + by - 1) / by;
        }

        /// What `level` of a chain of `shape` is, and the levels under it.
        ImageShape shapeAt(const ImageShape& shape, const std::uint8_t level)
        {
            return ImageShape{ .mFormat = shape.mFormat,
                .mWidth = std::max(shape.mWidth >> level, 1u),
                .mHeight = std::max(shape.mHeight >> level, 1u),
                .mLevels = static_cast<std::uint8_t>(shape.mLevels - level) };
        }

        /// The first level whose longer side is `side` texels or fewer, and the coarsest the chain has where none is.
        std::uint8_t floorLevelOf(const ImageShape& shape, const std::uint32_t side)
        {
            std::uint8_t level = 0;
            std::uint32_t longer = std::max(shape.mWidth, shape.mHeight);
            while (longer > side && level + 1 < shape.mLevels)
            {
                longer = std::max(longer >> 1, 1u);
                ++level;
            }
            return level;
        }

        bool isDemanded(const HoldTier tier)
        {
            return tier < HoldTier::Kept;
        }

        ClassIndex classAt(const TextureResident& texture, const std::uint8_t level)
        {
            assert(level < texture.mShape.mLevels && "a level past the chain");
            return texture.mClassAt[level];
        }

        /// The slot a texture stands in and its level: what stands while texels for another are on their way too.
        std::optional<TextureStands> standingOf(const TextureResident& texture)
        {
            if (const auto* stands = std::get_if<TextureStands>(&texture.mStanding))
                return *stands;
            if (const auto* arriving = std::get_if<TextureArriving>(&texture.mStanding))
                return arriving->mNow;
            return std::nullopt;
        }

        std::optional<std::uint8_t> standingLevelOf(const TextureResident& texture)
        {
            const std::optional<TextureStands> stands = standingOf(texture);
            return stands.has_value() ? std::optional(stands->mLevel) : std::nullopt;
        }

        std::optional<Slot> slotOf(const StructureResident& structure)
        {
            return std::visit(
                []<class S>(const S& standing) -> std::optional<Slot> {
                    if constexpr (std::is_same_v<S, NotStanding> || std::is_same_v<S, StandingFailed>)
                        return std::nullopt;
                    else
                        return standing.mSlot;
                },
                structure.mStanding);
        }

        ClassIndex structureClassOf(const StructureResident& structure)
        {
            return std::holds_alternative<StructureTight>(structure.mStanding) ? structure.mTightClass
                                                                               : structure.mLooseClass;
        }

        /// Whether what stands is not what the plan gives: an arrival, a raise, a lowering or a drop to come.
        bool textureWaits(const TextureResident& texture)
        {
            return texture.mPlannedLevel != standingLevelOf(texture);
        }

        bool structureWaits(const StructureResident& structure)
        {
            return structure.mPlanned != slotOf(structure).has_value();
        }

        /// Whether a demanded structure's tight copy is still to be made.
        bool structureTightens(const StructureResident& structure)
        {
            const auto* loose = std::get_if<StructureLoose>(&structure.mStanding);
            return loose != nullptr && loose->mTightBytes.has_value() && isDemanded(structure.mTier);
        }

        /// The owner a slot's ledger entry packs: the scene's place, whether a structure, and the row.
        struct OwnerParts
        {
            std::uint8_t mScene = 0;
            bool mStructure = false;
            std::uint32_t mRow = 0;
        };

        OwnerParts partsOf(const SlotOwner owner)
        {
            const auto packed = static_cast<std::uint32_t>(owner);
            return OwnerParts{ .mScene = static_cast<std::uint8_t>(packed >> 24),
                .mStructure = ((packed >> 23) & 1u) != 0,
                .mRow = packed & ((1u << 23) - 1) };
        }
    }

    void Residency::PlanCounts::clear()
    {
        std::ranges::fill(mCounts, 0u);
        mArenas = {};
    }

    Residency::PlanCounts::ArenaTotal& Residency::PlanCounts::arenaOf(const ClassIndex of)
    {
        return mArenas[static_cast<std::size_t>(mLedger->getArena(of))];
    }

    void Residency::PlanCounts::add(const ClassIndex of)
    {
        if (of.at() >= mCounts.size())
            mCounts.resize(std::size_t{ of.at() } + 1, 0);
        const std::uint32_t was = mCounts[of.at()]++;
        const std::uint32_t perSlab = mLedger->getSlotsPerSlab(of);
        ArenaTotal& arena = arenaOf(of);
        switch (mLedger->getTier(of))
        {
            case SlabTier::Small:
                arena.mSmallSlabs += ceilDiv(was + 1, perSlab) - ceilDiv(was, perSlab);
                break;
            case SlabTier::Medium:
                arena.mMediumBytes += (ceilDiv(was + 1, perSlab) - ceilDiv(was, perSlab)) * mChunkBytes;
                break;
            case SlabTier::Large:
                arena.mLargeBytes += mLedger->getStrideBytes(of);
                break;
        }
    }

    void Residency::PlanCounts::remove(const ClassIndex of)
    {
        assert(of.at() < mCounts.size() && "a class's slot taken off a plan that never held one");
        assert(mCounts[of.at()] > 0 && "a class's slot taken off a plan that holds none");
        const std::uint32_t was = mCounts[of.at()]--;
        const std::uint32_t perSlab = mLedger->getSlotsPerSlab(of);
        ArenaTotal& arena = arenaOf(of);
        switch (mLedger->getTier(of))
        {
            case SlabTier::Small:
                arena.mSmallSlabs -= ceilDiv(was, perSlab) - ceilDiv(was - 1, perSlab);
                break;
            case SlabTier::Medium:
                arena.mMediumBytes -= (ceilDiv(was, perSlab) - ceilDiv(was - 1, perSlab)) * mChunkBytes;
                break;
            case SlabTier::Large:
                arena.mLargeBytes -= mLedger->getStrideBytes(of);
                break;
        }
    }

    std::uint64_t Residency::PlanCounts::chunkBytesIn(const ArenaIndex arena) const
    {
        const ArenaTotal& held = mArenas[static_cast<std::size_t>(arena)];
        // Sixteen small slabs a chunk, as the ledger cuts them (`SlotLedger::bytesFor`).
        return ceilDiv(held.mSmallSlabs, 16) * mChunkBytes + held.mMediumBytes;
    }

    std::uint64_t Residency::PlanCounts::totalIn(const ArenaIndex arena) const
    {
        return chunkBytesIn(arena) + mArenas[static_cast<std::size_t>(arena)].mLargeBytes;
    }

    std::uint64_t Residency::PlanCounts::total() const
    {
        std::uint64_t bytes = 0;
        for (std::size_t arena = 0; arena < mArenas.size(); ++arena)
            bytes += totalIn(static_cast<ArenaIndex>(arena));
        return bytes;
    }

    std::uint32_t Residency::PlanCounts::countOf(const ClassIndex of) const
    {
        return of.at() < mCounts.size() ? mCounts[of.at()] : 0;
    }

    Residency::Residency(const ResidencyDevice& device, const SlotLayout& layout)
        : mLedger([&] {
            SlotLayout arenas = layout;
            arenas.mArenaOf = { device.arenaOf(SlotKind::Image), device.arenaOf(SlotKind::Buffer) };
            assert(arenas.mChunkBytes / arenas.mSlabBytes == 16 && "a plan counts sixteen small slabs a chunk");
            return arenas;
        }())
        , mCounts(mLedger)
    {
        mScenes.reserve(sScenesMost);
    }

    Residency::Scene* Residency::findScene(const SceneSlot slot)
    {
        const auto found
            = std::ranges::find_if(mScenes, [&](const Scene& scene) { return scene.mActive && scene.mSlot == slot; });
        return found != mScenes.end() ? &*found : nullptr;
    }

    const Residency::Scene* Residency::findScene(const SceneSlot slot) const
    {
        const auto found
            = std::ranges::find_if(mScenes, [&](const Scene& scene) { return scene.mActive && scene.mSlot == slot; });
        return found != mScenes.end() ? &*found : nullptr;
    }

    Residency::Scene& Residency::sceneOf(const SceneSlot slot)
    {
        Scene* const scene = findScene(slot);
        assert(scene != nullptr && "a scene residency was never told of");
        return *scene;
    }

    std::uint8_t Residency::indexOf(const Scene& scene) const
    {
        return static_cast<std::uint8_t>(&scene - mScenes.data());
    }

    SlotOwner Residency::ownerOf(const std::uint8_t scene, const bool structure, const std::uint32_t row)
    {
        assert(row < (1u << 23) && "a row past what an owner packs");
        return SlotOwner{ (std::uint32_t{ scene } << 24) | (structure ? 1u << 23 : 0u) | row };
    }

    void Residency::addScene(const SceneSlot slot)
    {
        assert(findScene(slot) == nullptr && "a scene added twice");
        const auto free = std::ranges::find_if(mScenes, [](const Scene& scene) { return !scene.mActive; });
        if (free != mScenes.end())
        {
            *free = Scene{ .mSlot = slot, .mActive = true };
            return;
        }
        assert(mScenes.size() < sScenesMost && "more scenes than residency stands");
        mScenes.push_back(Scene{ .mSlot = slot, .mActive = true });
    }

    void Residency::dropScene(const HandOver& handing)
    {
        Scene& scene = sceneOf(handing.mSlot);
        mOps.clear();
        mFreed.clear();
        for (std::uint32_t at = 0; at < scene.mTextures.size(); ++at)
            resetTexture(scene, TextureIndex(at), handing.mFeed);
        for (std::uint32_t at = 0; at < scene.mStructures.size(); ++at)
            resetStructure(scene, MeshIndex(at));
        for (const OwedDrop& owed : scene.mDropsOwed)
        {
            mOps.push_back(DropImage{ .mTexture = owed.mTexture, .mFrom = owed.mSlot });
            retireAfter(owed.mSlot);
        }
        const DeviceEpoch epoch = handing.mDevice.handOver(handing.mSlot, handing.mScene, mOps);
        for (const Slot freed : mFreed)
            mLedger.retire(freed, epoch);
        scene = Scene{};
        mPlanStale = true;
    }

    void Residency::openFrame(ResidencyDevice& device, TextureFeed& feed)
    {
        mBudget = device.openFrame(mEvents);
        mLedger.collect(mEvents.mFinished);
        for (const BlockIndex block : mEvents.mBlocksArrived)
            mLedger.blockArrived(block);
        for (const BlockIndex block : mEvents.mBlocksRefused)
            mLedger.blockRefused(block);

        for (const TightSize& tight : mEvents.mTightSizes)
        {
            Scene* const scene = findScene(tight.mScene);
            if (scene == nullptr || !scene->mStructures.holds(tight.mMesh))
                continue;
            StructureResident& structure = scene->mStructures[tight.mMesh];
            auto* loose = std::get_if<StructureLoose>(&structure.mStanding);
            if (structure.mGeneration != tight.mGeneration || loose == nullptr)
                continue;
            loose->mTightBytes = tight.mBytes;
            structure.mTightClass = mLedger.classOf(SlotKind::Buffer, tight.mBytes, structure.mLoosePrice.mAlignment);
            mPlanStale = true;
        }

        // **What arrives for a tenant that left is dropped in one place**: a ticket is never issued twice, and a row's
        // arrival keeps the ticket it waits on.
        while (const std::optional<FeedArrival> arrival = feed.take())
        {
            const FeedRequest& request = arrival->mRequest;
            Scene* const scene = findScene(request.mScene);
            if (scene == nullptr || !scene->mTextures.holds(request.mTexture))
                continue;
            TextureResident& texture = scene->mTextures[request.mTexture];
            auto* arriving = std::get_if<TextureArriving>(&texture.mStanding);
            if (arriving == nullptr || arriving->mTicket != request.mTicket)
                continue;
            assert(texture.mGeneration == request.mGeneration && "a ticket outlived its row's tenant");
            if (const auto* failed = std::get_if<FeedFailed>(&arrival->mOutcome))
            {
                if (arriving->mNow.has_value())
                    scene->mDropsOwed.push_back(
                        OwedDrop{ .mTexture = request.mTexture, .mSlot = arriving->mNow->mSlot });
                texture.mStanding = StandingFailed{ .mCause = failed->mCause };
                mPlanStale = true;
                continue;
            }
            arriving->mArrived = true;
        }

        mSpent = Shares{ .mUploadBytes = 0, .mCopyBytes = 0, .mBuildTriangles = 0, .mTightCopies = 0 };
        mReport.mOpsIssued = 0;
        mReport.mBytesUploaded = 0;
        mReport.mBytesCopied = 0;
        mReport.mTrianglesBuilt = 0;
        mReport.mTightCopies = 0;
        // **The plan is run again where an input moved**: the cap here; a row's tier, want or tenant as a scene is
        // handed over; a tight size or a failure above. What arrived or stood moves no input.
        if (mReport.mCapBytes != mBudget.mCapBytes)
            mPlanStale = true;
        mReport.mCapBytes = mBudget.mCapBytes;
    }

    void Residency::retireAfter(const Slot slot)
    {
        mFreed.push_back(slot);
    }

    void Residency::resetTexture(Scene& scene, const TextureIndex row, TextureFeed& feed)
    {
        TextureResident& texture = scene.mTextures[row];
        if (const auto* arriving = std::get_if<TextureArriving>(&texture.mStanding))
            feed.forget(arriving->mTicket);
        if (const std::optional<TextureStands> stands = standingOf(texture))
        {
            mOps.push_back(DropImage{ .mTexture = row, .mFrom = stands->mSlot });
            retireAfter(stands->mSlot);
        }
        texture = TextureResident{};
        mPlanStale = true;
    }

    void Residency::resetStructure(Scene& scene, const MeshIndex row)
    {
        StructureResident& structure = scene.mStructures[row];
        if (const std::optional<Slot> slot = slotOf(structure))
        {
            mOps.push_back(DropStructure{ .mMesh = row, .mFrom = *slot });
            retireAfter(*slot);
        }
        structure = StructureResident{};
        mPlanStale = true;
    }

    void Residency::syncTextures(Scene& scene, const HandOver& handing)
    {
        const TextureTable& table = handing.mScene.textures();
        const auto rows = table.getRows();
        if (rows.size() > scene.mTextures.size())
            scene.mTextures.resize(rows.size());
        for (std::uint32_t at = 0; at < scene.mTextures.size(); ++at)
        {
            const TextureIndex row(at);
            TextureResident& texture = scene.mTextures[row];
            const bool live = at < rows.size() && table.isLive(row);
            if (texture.mLive && (!live || table.generationOf(row) != texture.mGeneration))
                resetTexture(scene, row, handing.mFeed);
            if (!live)
                continue;

            if (!texture.mLive)
            {
                texture.mLive = true;
                texture.mGeneration = table.generationOf(row);
                const TextureRow& described = rows[row];
                // A bake has no file's image: what stands it is a bake's own operation, which this does not issue yet.
                if (described.mImage == nullptr || described.mFormat == TextureFormat::Unnamed)
                {
                    texture.mStanding = StandingFailed{ .mCause = Refused::Texture };
                    continue;
                }
                const osg::Image& image = *described.mImage;
                texture.mShape = ImageShape{ .mFormat = described.mFormat,
                    .mWidth = static_cast<std::uint32_t>(std::max(image.s(), 1)),
                    .mHeight = static_cast<std::uint32_t>(std::max(image.t(), 1)),
                    .mLevels = static_cast<std::uint8_t>(std::clamp<std::uint32_t>(
                        levelCountOf(image), 1, static_cast<std::uint32_t>(texture.mClassAt.size()))) };
                texture.mFloorLevel = floorLevelOf(texture.mShape, sFloorSide);
                for (std::uint8_t level = 0; level < texture.mShape.mLevels; ++level)
                {
                    const std::optional<ContentPrice> price
                        = handing.mDevice.priceImage(shapeAt(texture.mShape, level));
                    texture.mClassAt[level] = price.has_value()
                        ? mLedger.classOf(SlotKind::Image, price->mBytes, price->mAlignment)
                        : sNoRow;
                }
                if (texture.mClassAt[texture.mFloorLevel].isNone())
                {
                    texture.mStanding = StandingFailed{ .mCause = Refused::Texture };
                    continue;
                }
                // The coarsest, so the want below is taken at once, as a want that rises is.
                texture.mWantedLevel = texture.mShape.mLevels - 1;
                mPlanStale = true;
            }
            if (std::holds_alternative<StandingFailed>(texture.mStanding))
                continue;

            const HoldTier tier = table.getTier(row);
            if (tier != texture.mTier)
            {
                texture.mTier = tier;
                mPlanStale = true;
            }
            if (tier != HoldTier::Unheld)
                texture.mLastHeld = scene.mHandOvers;

            // A want rises at once and falls only once it has stood.
            const auto want
                = static_cast<std::uint8_t>(std::min<std::size_t>(table.getWant(row), texture.mShape.mLevels - 1u));
            if (want < texture.mWantedLevel)
            {
                texture.mWantedLevel = want;
                texture.mFallingTo.reset();
                mPlanStale = true;
            }
            else if (want > texture.mWantedLevel)
            {
                if (texture.mFallingTo != want)
                {
                    texture.mFallingTo = want;
                    texture.mFellAt = scene.mHandOvers;
                }
                if (scene.mHandOvers - texture.mFellAt >= sWantHoldHandOvers)
                {
                    texture.mWantedLevel = want;
                    texture.mFallingTo.reset();
                    mPlanStale = true;
                }
            }
            else
                texture.mFallingTo.reset();
        }
    }

    void Residency::syncStructures(Scene& scene, const HandOver& handing)
    {
        const MeshTable& table = handing.mScene.meshes();
        const auto rows = table.getRows();
        if (rows.size() > scene.mStructures.size())
            scene.mStructures.resize(rows.size());
        for (std::uint32_t at = 0; at < scene.mStructures.size(); ++at)
        {
            const MeshIndex row(at);
            StructureResident& structure = scene.mStructures[row];
            const bool live = at < rows.size() && table.isLive(row);
            if (structure.mLive && (!live || table.generationOf(row) != structure.mGeneration))
                resetStructure(scene, row);
            if (!live)
                continue;

            if (!structure.mLive)
            {
                structure.mLive = true;
                structure.mGeneration = table.generationOf(row);
                const MeshRange& mesh = rows[row];
                structure.mShape = StructureShape{
                    .mTriangles = mesh.getTriangleCount(), .mVertices = mesh.mVertices.mCount, .mRefits = mesh.deforms()
                };
                if (structure.mShape.mTriangles > 0)
                {
                    structure.mLoosePrice = handing.mDevice.priceStructure(structure.mShape);
                    structure.mLooseClass = mLedger.classOf(
                        SlotKind::Buffer, structure.mLoosePrice.mBytes, structure.mLoosePrice.mAlignment);
                }
                mPlanStale = true;
            }

            const HoldTier tier = table.getTier(row);
            if (tier != structure.mTier)
            {
                structure.mTier = tier;
                mPlanStale = true;
            }
            if (tier != HoldTier::Unheld)
                structure.mLastHeld = scene.mHandOvers;
        }
    }

    void Residency::plan()
    {
        mCounts.clear();
        const std::uint64_t cap = mBudget.mCapBytes;
        mReport.mStarvedTextures = 0;
        mReport.mStarvedStructures = 0;

        // 1. What is demanded, at what it wants: a texture at its want, or the first level under it the device prices;
        // a structure in the class it stands or will stand in.
        for (Scene& scene : mScenes)
        {
            for (TextureResident& texture : scene.mTextures)
            {
                texture.mPlannedLevel.reset();
                if (!texture.mLive || std::holds_alternative<StandingFailed>(texture.mStanding)
                    || !isDemanded(texture.mTier))
                    continue;
                std::uint8_t level = texture.mWantedLevel;
                while (level < texture.mFloorLevel && classAt(texture, level).isNone())
                    ++level;
                texture.mPlannedLevel = level;
                mCounts.add(classAt(texture, level));
            }
            for (StructureResident& structure : scene.mStructures)
            {
                structure.mPlanned = structure.mLive && structure.mShape.mTriangles > 0
                    && !std::holds_alternative<StandingFailed>(structure.mStanding) && isDemanded(structure.mTier);
                if (structure.mPlanned)
                    mCounts.add(structureClassOf(structure));
            }
        }

        // 2. What does not fit is made to, and what still does not is starved.
        if (mCounts.total() > cap)
            planLowerings(cap);
        if (mCounts.total() > cap)
            planStarving(cap);

        // **A texture is raised only where the plan fits a chunk under the cap**, so a cap that moves a little does
        // not raise and lower it in turn: what would rise stands as it is.
        if (mCounts.total() + mLedger.getLayout().mChunkBytes > cap)
            for (Scene& scene : mScenes)
                for (TextureResident& texture : scene.mTextures)
                {
                    const std::optional<std::uint8_t> standing = standingLevelOf(texture);
                    if (!texture.mPlannedLevel.has_value() || !standing.has_value()
                        || *texture.mPlannedLevel >= *standing)
                        continue;
                    mCounts.remove(classAt(texture, *texture.mPlannedLevel));
                    mCounts.add(classAt(texture, *standing));
                    texture.mPlannedLevel = standing;
                }

        mReport.mDemandedBytes = mCounts.total();
        planCache(cap);

        mReport.mPlannedBytes = mCounts.total();
        mReport.mBelowWant = 0;
        for (const Scene& scene : mScenes)
            for (const TextureResident& texture : scene.mTextures)
                if (texture.mPlannedLevel.has_value() && isDemanded(texture.mTier)
                    && *texture.mPlannedLevel > texture.mWantedLevel)
                    ++mReport.mBelowWant;
        mPlanStale = false;
    }

    void Residency::planLowerings(const std::uint64_t cap)
    {
        // The least protected a level coarser, the most bytes first, the lowest scene and row among equals, each to
        // its floor and no further: a heap, so a step costs its own lowering.
        const auto lessLowered = [](const Weighed& a, const Weighed& b) {
            return std::tie(a.mTier, a.mKey, b.mScene, b.mRow) < std::tie(b.mTier, b.mKey, a.mScene, a.mRow);
        };
        mWeighed.clear();
        const auto offer = [&](const std::uint8_t sceneAt, const std::uint32_t row) {
            const TextureResident& texture = mScenes[sceneAt].mTextures[TextureIndex(row)];
            if (!texture.mPlannedLevel.has_value() || *texture.mPlannedLevel >= texture.mFloorLevel)
                return;
            mWeighed.push_back(Weighed{ .mTier = texture.mTier,
                .mKey = mLedger.getStrideBytes(classAt(texture, *texture.mPlannedLevel)),
                .mScene = sceneAt,
                .mRow = row });
            std::ranges::push_heap(mWeighed, lessLowered);
        };
        for (std::size_t sceneAt = 0; sceneAt < mScenes.size(); ++sceneAt)
            for (std::uint32_t row = 0; row < mScenes[sceneAt].mTextures.size(); ++row)
                offer(static_cast<std::uint8_t>(sceneAt), row);

        while (mCounts.total() > cap && !mWeighed.empty())
        {
            std::ranges::pop_heap(mWeighed, lessLowered);
            const Weighed victim = mWeighed.back();
            mWeighed.pop_back();
            TextureResident& texture = mScenes[victim.mScene].mTextures[TextureIndex(victim.mRow)];
            const std::uint8_t from = *texture.mPlannedLevel;
            std::uint8_t to = from + 1;
            while (to < texture.mFloorLevel && classAt(texture, to).isNone())
                ++to;
            mCounts.remove(classAt(texture, from));
            mCounts.add(classAt(texture, to));
            texture.mPlannedLevel = to;
            offer(victim.mScene, victim.mRow);
        }
    }

    void Residency::planStarving(const std::uint64_t cap)
    {
        // Whatever floors do not fit is planned at nothing and named, the least protected first, the most bytes first.
        mWeighed.clear();
        for (std::size_t sceneAt = 0; sceneAt < mScenes.size(); ++sceneAt)
        {
            const Scene& scene = mScenes[sceneAt];
            const auto at = static_cast<std::uint8_t>(sceneAt);
            for (std::uint32_t row = 0; row < scene.mTextures.size(); ++row)
                if (const TextureResident& texture = scene.mTextures[TextureIndex(row)];
                    texture.mPlannedLevel.has_value())
                    mWeighed.push_back(Weighed{ .mTier = texture.mTier,
                        .mKey = mLedger.getStrideBytes(classAt(texture, *texture.mPlannedLevel)),
                        .mScene = at,
                        .mRow = row });
            for (std::uint32_t row = 0; row < scene.mStructures.size(); ++row)
                if (const StructureResident& structure = scene.mStructures[MeshIndex(row)]; structure.mPlanned)
                    mWeighed.push_back(Weighed{ .mTier = structure.mTier,
                        .mKey = mLedger.getStrideBytes(structureClassOf(structure)),
                        .mScene = at,
                        .mStructure = true,
                        .mRow = row });
        }
        std::ranges::sort(mWeighed, [](const Weighed& a, const Weighed& b) {
            return std::tie(b.mTier, b.mKey, a.mScene, a.mStructure, a.mRow)
                < std::tie(a.mTier, a.mKey, b.mScene, b.mStructure, b.mRow);
        });
        for (const Weighed& starved : mWeighed)
        {
            if (mCounts.total() <= cap)
                break;
            Scene& scene = mScenes[starved.mScene];
            if (starved.mStructure)
            {
                StructureResident& structure = scene.mStructures[MeshIndex(starved.mRow)];
                mCounts.remove(structureClassOf(structure));
                structure.mPlanned = false;
                ++mReport.mStarvedStructures;
                continue;
            }
            TextureResident& texture = scene.mTextures[TextureIndex(starved.mRow)];
            mCounts.remove(classAt(texture, *texture.mPlannedLevel));
            texture.mPlannedLevel.reset();
            ++mReport.mStarvedTextures;
        }
    }

    void Residency::planCache(const std::uint64_t cap)
    {
        // What nothing demands is kept while it fits the cache's share of what the demand leaves: the most lately
        // held first, at the level it stands, else a level coarser, else dropped.
        const std::uint64_t demanded = mCounts.total();
        const std::uint64_t limit
            = demanded + (cap > demanded ? (cap - demanded) * sCacheNumerator / sCacheDenominator : 0);
        mWeighed.clear();
        for (std::size_t sceneAt = 0; sceneAt < mScenes.size(); ++sceneAt)
        {
            const Scene& scene = mScenes[sceneAt];
            const auto at = static_cast<std::uint8_t>(sceneAt);
            for (std::uint32_t row = 0; row < scene.mTextures.size(); ++row)
                if (const TextureResident& texture = scene.mTextures[TextureIndex(row)];
                    texture.mLive && !isDemanded(texture.mTier) && standingLevelOf(texture).has_value())
                    mWeighed.push_back(
                        Weighed{ .mTier = texture.mTier, .mKey = texture.mLastHeld, .mScene = at, .mRow = row });
            for (std::uint32_t row = 0; row < scene.mStructures.size(); ++row)
                if (const StructureResident& structure = scene.mStructures[MeshIndex(row)];
                    structure.mLive && !isDemanded(structure.mTier) && slotOf(structure).has_value())
                    mWeighed.push_back(Weighed{ .mTier = structure.mTier,
                        .mKey = structure.mLastHeld,
                        .mScene = at,
                        .mStructure = true,
                        .mRow = row });
        }
        std::ranges::sort(mWeighed, [](const Weighed& a, const Weighed& b) {
            return std::tie(a.mTier, b.mKey, a.mScene, a.mStructure, a.mRow)
                < std::tie(b.mTier, a.mKey, b.mScene, b.mStructure, b.mRow);
        });
        for (const Weighed& kept : mWeighed)
        {
            Scene& scene = mScenes[kept.mScene];
            if (kept.mStructure)
            {
                StructureResident& structure = scene.mStructures[MeshIndex(kept.mRow)];
                const ClassIndex of = structureClassOf(structure);
                mCounts.add(of);
                if (mCounts.total() <= limit)
                    structure.mPlanned = true;
                else
                    mCounts.remove(of);
                continue;
            }
            TextureResident& texture = scene.mTextures[TextureIndex(kept.mRow)];
            const std::uint8_t standing = *standingLevelOf(texture);
            const std::uint8_t coarsest = std::min<std::uint8_t>(standing + 1, texture.mShape.mLevels - 1);
            for (std::uint8_t level = standing; level <= coarsest; ++level)
            {
                const ClassIndex of = classAt(texture, level);
                if (of.isNone())
                    continue;
                mCounts.add(of);
                if (mCounts.total() <= limit)
                {
                    texture.mPlannedLevel = level;
                    break;
                }
                mCounts.remove(of);
            }
        }
    }

    bool Residency::ensureRoom(const ClassIndex of)
    {
        if (mLedger.hasRoom(of))
            return true;
        // Where the arena has every chunk the plan stands in, none is asked: content of a class with room to spare
        // moves, out of its emptiest slab (`SlotLedger::drainFor`).
        const ArenaIndex arena = mLedger.getArena(of);
        const SlabTier tier = mLedger.getTier(of);
        const std::uint64_t chunks = std::uint64_t{ mLedger.getChunkCount(arena) } * mLedger.getLayout().mChunkBytes;
        if (tier != SlabTier::Large && chunks >= mCounts.chunkBytesIn(arena) && !mLedger.nextToMove().has_value())
            std::ignore = mLedger.drainFor(arena, tier);
        return false;
    }

    void Residency::issueDrops(Scene& scene, TextureFeed& feed)
    {
        for (std::uint32_t at = 0; at < scene.mTextures.size(); ++at)
        {
            const TextureIndex row(at);
            TextureResident& texture = scene.mTextures[row];
            if (texture.mPlannedLevel.has_value())
                continue;
            if (const auto* arriving = std::get_if<TextureArriving>(&texture.mStanding))
                feed.forget(arriving->mTicket);
            if (const std::optional<TextureStands> stands = standingOf(texture))
            {
                mOps.push_back(DropImage{ .mTexture = row, .mFrom = stands->mSlot });
                retireAfter(stands->mSlot);
            }
            if (!std::holds_alternative<StandingFailed>(texture.mStanding))
                texture.mStanding = NotStanding{};
        }
        for (std::uint32_t at = 0; at < scene.mStructures.size(); ++at)
        {
            const MeshIndex row(at);
            StructureResident& structure = scene.mStructures[row];
            if (structure.mPlanned)
                continue;
            if (const std::optional<Slot> slot = slotOf(structure))
            {
                mOps.push_back(DropStructure{ .mMesh = row, .mFrom = *slot });
                retireAfter(*slot);
                structure.mStanding = NotStanding{};
            }
        }
    }

    void Residency::issueLowerings(Scene& scene, TextureFeed& feed, const Shares& shares)
    {
        // A plan that moved while texels were on their way asks again for what it gives now; what stands meanwhile
        // stands on.
        for (TextureResident& texture : scene.mTextures)
        {
            const auto* arriving = std::get_if<TextureArriving>(&texture.mStanding);
            if (arriving == nullptr || texture.mPlannedLevel == arriving->mToLevel)
                continue;
            feed.forget(arriving->mTicket);
            texture.mStanding
                = arriving->mNow.has_value() ? TextureStanding(*arriving->mNow) : TextureStanding(NotStanding{});
        }

        // Lowerings, which make room: the coarser levels copied on the device into a slot of their class.
        for (std::uint32_t at = 0; at < scene.mTextures.size(); ++at)
        {
            const TextureIndex row(at);
            TextureResident& texture = scene.mTextures[row];
            auto* stands = std::get_if<TextureStands>(&texture.mStanding);
            if (stands == nullptr || !texture.mPlannedLevel.has_value() || *texture.mPlannedLevel <= stands->mLevel)
                continue;
            const ClassIndex of = classAt(texture, *texture.mPlannedLevel);
            if (mSpent.mCopyBytes >= shares.mCopyBytes || !ensureRoom(of))
                continue;
            const Slot to = mLedger.reserve(of, ownerOf(indexOf(scene), false, at));
            mLedger.commit(to);
            mOps.push_back(StandImage{ .mTexture = row,
                .mGeneration = texture.mGeneration,
                .mTo = to,
                .mLevel = *texture.mPlannedLevel,
                .mFrom = stands->mSlot,
                .mFromLevel = stands->mLevel });
            retireAfter(stands->mSlot);
            *stands = TextureStands{ .mLevel = *texture.mPlannedLevel, .mSlot = to };
            mSpent.mCopyBytes += mLedger.getStrideBytes(of);
            mReport.mBytesCopied += mLedger.getStrideBytes(of);
        }
    }

    void Residency::issueMoves(Scene& scene, const Shares& shares)
    {
        // What a drain empties, under the copy share: the same level into a slot of the same class elsewhere. A slot
        // of another scene waits for that scene's hand-over, which `owes` asks for.
        while (const std::optional<Slot> moving = mLedger.nextToMove())
        {
            const OwnerParts owner = partsOf(mLedger.getOwnerOf(*moving));
            const ClassIndex of = mLedger.getClassOf(*moving);
            if (owner.mScene != indexOf(scene) || mSpent.mCopyBytes >= shares.mCopyBytes || !mLedger.hasRoom(of))
                return;
            const Slot to = mLedger.reserve(of, mLedger.getOwnerOf(*moving));
            mLedger.commit(to);
            if (owner.mStructure)
            {
                StructureResident& structure = scene.mStructures[MeshIndex(owner.mRow)];
                mOps.push_back(CopyStructure{ .mMesh = MeshIndex(owner.mRow),
                    .mGeneration = structure.mGeneration,
                    .mFrom = *moving,
                    .mTo = to,
                    .mKind = StructureCopy::Clone });
                std::visit(
                    [&]<class S>(S& standing) {
                        if constexpr (!std::is_same_v<S, NotStanding> && !std::is_same_v<S, StandingFailed>)
                            standing.mSlot = to;
                    },
                    structure.mStanding);
            }
            else
            {
                TextureResident& texture = scene.mTextures[TextureIndex(owner.mRow)];
                auto* arriving = std::get_if<TextureArriving>(&texture.mStanding);
                TextureStands& stands
                    = arriving != nullptr ? *arriving->mNow : std::get<TextureStands>(texture.mStanding);
                mOps.push_back(StandImage{ .mTexture = TextureIndex(owner.mRow),
                    .mGeneration = texture.mGeneration,
                    .mTo = to,
                    .mLevel = stands.mLevel,
                    .mFrom = moving,
                    .mFromLevel = stands.mLevel });
                stands.mSlot = to;
            }
            retireAfter(*moving);
            mSpent.mCopyBytes += mLedger.getStrideBytes(of);
            mReport.mBytesCopied += mLedger.getStrideBytes(of);
        }
    }

    void Residency::issueArrivals(Scene& scene, TextureFeed& feed, const Shares& shares)
    {
        // Arrivals and raisings, the most protected first: texels asked of the feed, then stood once they came.
        for (const HoldTier tier : { HoldTier::Forced, HoldTier::Shown, HoldTier::Hidden })
            for (std::uint32_t at = 0; at < scene.mTextures.size(); ++at)
            {
                const TextureIndex row(at);
                TextureResident& texture = scene.mTextures[row];
                if (texture.mTier != tier || !texture.mPlannedLevel.has_value())
                    continue;
                const std::uint8_t planned = *texture.mPlannedLevel;
                const std::optional<TextureStands> now = standingOf(texture);
                if (now.has_value() && now->mLevel <= planned)
                    continue;
                const std::uint8_t end = now.has_value() ? now->mLevel : texture.mShape.mLevels;

                if (const auto* arriving = std::get_if<TextureArriving>(&texture.mStanding))
                {
                    const ClassIndex of = classAt(texture, planned);
                    if (!arriving->mArrived || mSpent.mUploadBytes >= shares.mUploadBytes || !ensureRoom(of))
                        continue;
                    const Slot to = mLedger.reserve(of, ownerOf(indexOf(scene), false, at));
                    mLedger.commit(to);
                    StandImage stand{ .mTexture = row,
                        .mGeneration = texture.mGeneration,
                        .mTo = to,
                        .mLevel = planned,
                        .mUpload = LevelSpan{ .mFirst = planned, .mEnd = end },
                        .mFeed = arriving->mTicket };
                    if (now.has_value())
                    {
                        stand.mFrom = now->mSlot;
                        stand.mFromLevel = now->mLevel;
                        retireAfter(now->mSlot);
                    }
                    mOps.push_back(stand);
                    texture.mStanding = TextureStands{ .mLevel = planned, .mSlot = to };
                    mSpent.mUploadBytes += mLedger.getStrideBytes(of);
                    mReport.mBytesUploaded += mLedger.getStrideBytes(of);
                    continue;
                }

                if (!feed.hasCredit())
                    continue;
                const FeedTicket ticket{ mNextTicket++ };
                feed.ask(FeedRequest{ .mTicket = ticket,
                    .mScene = scene.mSlot,
                    .mTexture = row,
                    .mGeneration = texture.mGeneration,
                    .mLevels = LevelSpan{ .mFirst = planned, .mEnd = end } });
                texture.mStanding = TextureArriving{ .mToLevel = planned, .mTicket = ticket, .mNow = now };
            }
    }

    void Residency::issueStructures(Scene& scene, const Shares& shares)
    {
        for (const HoldTier tier : { HoldTier::Forced, HoldTier::Shown, HoldTier::Hidden })
            for (std::uint32_t at = 0; at < scene.mStructures.size(); ++at)
            {
                const MeshIndex row(at);
                StructureResident& structure = scene.mStructures[row];
                if (structure.mTier != tier || !structure.mPlanned)
                    continue;

                if (std::holds_alternative<NotStanding>(structure.mStanding))
                {
                    if (mSpent.mBuildTriangles >= shares.mBuildTriangles || !ensureRoom(structure.mLooseClass))
                        continue;
                    const Slot loose = mLedger.reserve(structure.mLooseClass, ownerOf(indexOf(scene), true, at));
                    mLedger.commit(loose);
                    mOps.push_back(BuildStructure{ .mMesh = row,
                        .mGeneration = structure.mGeneration,
                        .mLoose = loose,
                        .mBytes = structure.mLoosePrice.mBytes });
                    structure.mStanding = structure.mShape.mRefits
                        ? StructureStanding(StructureKept{ .mSlot = loose })
                        : StructureStanding(StructureLoose{ .mSlot = loose });
                    mSpent.mBuildTriangles += structure.mShape.mTriangles;
                    mReport.mTrianglesBuilt += structure.mShape.mTriangles;
                    continue;
                }

                // A tight copy where the device answered a smaller size; where it is no smaller, the loose slot is
                // where it stays.
                auto* loose = std::get_if<StructureLoose>(&structure.mStanding);
                if (loose == nullptr || !loose->mTightBytes.has_value() || mSpent.mTightCopies >= shares.mTightCopies)
                    continue;
                if (mLedger.getStrideBytes(structure.mTightClass) >= mLedger.getStrideBytes(structure.mLooseClass))
                {
                    structure.mTightClass = structure.mLooseClass;
                    structure.mStanding = StructureTight{ .mSlot = loose->mSlot };
                    continue;
                }
                if (!ensureRoom(structure.mTightClass))
                    continue;
                const Slot tight = mLedger.reserve(structure.mTightClass, ownerOf(indexOf(scene), true, at));
                mLedger.commit(tight);
                mOps.push_back(CopyStructure{ .mMesh = row,
                    .mGeneration = structure.mGeneration,
                    .mFrom = loose->mSlot,
                    .mTo = tight,
                    .mKind = StructureCopy::Compact });
                retireAfter(loose->mSlot);
                structure.mStanding = StructureTight{ .mSlot = tight };
                ++mSpent.mTightCopies;
                ++mReport.mTightCopies;
            }
    }

    DeviceEpoch Residency::handOver(const HandOver& handing)
    {
        Scene& scene = sceneOf(handing.mSlot);
        mOps.clear();
        mFreed.clear();
        ++scene.mHandOvers;
        for (const OwedDrop& owed : scene.mDropsOwed)
        {
            mOps.push_back(DropImage{ .mTexture = owed.mTexture, .mFrom = owed.mSlot });
            retireAfter(owed.mSlot);
        }
        scene.mDropsOwed.clear();
        syncTextures(scene, handing);
        syncStructures(scene, handing);
        if (mPlanStale)
            plan();

        issueDrops(scene, handing.mFeed);
        issueLowerings(scene, handing.mFeed, handing.mShares);
        issueMoves(scene, handing.mShares);
        issueArrivals(scene, handing.mFeed, handing.mShares);
        issueStructures(scene, handing.mShares);

        const DeviceEpoch epoch = handing.mDevice.handOver(handing.mSlot, handing.mScene, mOps);
        for (const Slot freed : mFreed)
            mLedger.retire(freed, epoch);
        mReport.mOpsIssued += static_cast<std::uint32_t>(mOps.size());
        mReport.mSlots = mLedger.count();

        std::uint32_t waiting = 0;
        for (const Scene& each : mScenes)
        {
            waiting += static_cast<std::uint32_t>(std::ranges::count_if(each.mTextures, textureWaits));
            waiting += static_cast<std::uint32_t>(std::ranges::count_if(each.mStructures, structureWaits));
        }
        mReport.mWaiting = waiting;
        return epoch;
    }

    void Residency::closeFrame(ResidencyDevice& device)
    {
        // **A chunk is asked as the plan needs it**, where the plan stands in more chunks than the arena holds and has
        // asked, and a block of its own for each large slot the plan gives that has none; a free block is given back
        // only while what is held stands past the cap and a chunk past the plan.
        const std::uint64_t chunkBytes = mLedger.getLayout().mChunkBytes;
        std::array<bool, 256> seen{};
        for (std::uint32_t at = 0; at < mLedger.getClassCount(); ++at)
        {
            const ArenaIndex arena = mLedger.getArena(ClassIndex(at));
            if (std::exchange(seen[static_cast<std::size_t>(arena)], true))
                continue;
            const std::uint64_t chunks = std::uint64_t{ mLedger.getChunkCount(arena) } * chunkBytes;
            if (mCounts.chunkBytesIn(arena) > chunks)
            {
                const BlockIndex block = mLedger.askChunk(arena);
                device.acquireBlock(block, arena, chunkBytes);
            }
            else if (mCounts.chunkBytesIn(arena) + chunkBytes <= chunks
                && mLedger.count().mBytesHeld > mBudget.mCapBytes)
                if (const std::optional<BlockIndex> free = mLedger.freeBlock(arena))
                {
                    device.releaseBlock(*free);
                    mLedger.blockReleased(*free);
                }
        }

        for (std::uint32_t at = 0; at < mLedger.getClassCount(); ++at)
        {
            const ClassIndex of(at);
            if (mLedger.getTier(of) != SlabTier::Large)
                continue;
            for (std::uint32_t asked = mLedger.getLargeBlockCount(of); asked < mCounts.countOf(of); ++asked)
            {
                const BlockIndex block = mLedger.askLarge(of);
                device.acquireBlock(block, mLedger.getArena(of), mLedger.getBlockBytes(block));
            }
        }
    }

    bool Residency::owes(const SceneSlot slot) const
    {
        const Scene* const scene = findScene(slot);
        if (scene == nullptr)
            return false;
        if (const std::optional<Slot> moving = mLedger.nextToMove();
            moving.has_value() && partsOf(mLedger.getOwnerOf(*moving)).mScene == indexOf(*scene))
            return true;
        return !scene->mDropsOwed.empty() || std::ranges::any_of(scene->mTextures, textureWaits)
            || std::ranges::any_of(scene->mStructures, structureWaits)
            || std::ranges::any_of(scene->mStructures, structureTightens);
    }

    bool Residency::isSettled(const HoldTier atLeast) const
    {
        for (const Scene& scene : mScenes)
        {
            if (std::ranges::any_of(scene.mTextures,
                    [&](const TextureResident& texture) { return texture.mTier <= atLeast && textureWaits(texture); }))
                return false;
            if (std::ranges::any_of(scene.mStructures, [&](const StructureResident& structure) {
                    return structure.mTier <= atLeast && structureWaits(structure);
                }))
                return false;
        }
        return true;
    }

    bool Residency::standsFrom(const SceneSlot slot, const TextureIndex texture, const std::uint8_t level) const
    {
        const Scene* const scene = findScene(slot);
        if (scene == nullptr || !scene->mTextures.holds(texture))
            return false;
        const std::optional<std::uint8_t> standing = standingLevelOf(scene->mTextures[texture]);
        return standing.has_value() && *standing <= level;
    }

    const TextureResident& Residency::getTexture(const SceneSlot slot, const TextureIndex texture) const
    {
        const Scene* const scene = findScene(slot);
        assert(scene != nullptr && "a texture of a scene residency was never told of");
        assert(scene->mTextures.holds(texture) && "a texture residency holds no record of");
        return scene->mTextures[texture];
    }

    const StructureResident& Residency::getStructure(const SceneSlot slot, const MeshIndex mesh) const
    {
        const Scene* const scene = findScene(slot);
        assert(scene != nullptr && "a structure of a scene residency was never told of");
        assert(scene->mStructures.holds(mesh) && "a structure residency holds no record of");
        return scene->mStructures[mesh];
    }
}
