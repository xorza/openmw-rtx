#include "residency.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include <components/rtx/image/imagedescription.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/preprocess/texturehands.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/scene/materialtable.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/meshtable.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/texturetable.hpp>
#include <components/rtx/shaders/scene.h>

namespace Rtx
{
    namespace
    {
        constexpr std::uint64_t ceilDiv(const std::uint64_t value, const std::uint64_t by)
        {
            return (value + by - 1) / by;
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

        /// Whether every layer a chunk's composite reads stands: its diffuse slots, the neutral texel aside.
        bool layersStand(const RowVector<TextureResident, TextureIndex>& residents, const SceneDesc& described,
            const MaterialIndex chunk)
        {
            const Material& material = described.materials().getRows()[chunk];
            return std::ranges::all_of(
                material.mLayers.in(described.materials().getLayers()), [&](const MaterialLayer& layer) {
                    return layer.mDiffuse == Shaders::TEXTURE_NEUTRAL
                        || standingOf(residents[TextureIndex(layer.mDiffuse)]).has_value();
                });
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
        , mStandsStructures(device.standsStructures())
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

    void Residency::forgetScene(const SceneSlot slot, TextureFeed& feed)
    {
        Scene& scene = sceneOf(slot);
        for (const TextureResident& texture : scene.mTextures)
        {
            if (const auto* arriving = std::get_if<TextureArriving>(&texture.mStanding))
                feed.forget(arriving->mTicket);
            if (const std::optional<TextureStands> stands = standingOf(texture))
                retireNow(stands->mSlot);
        }
        for (const StructureResident& structure : scene.mStructures)
            if (const std::optional<Slot> standing = slotOf(structure))
                retireNow(*standing);
        for (const OwedDrop& owed : scene.mDropsOwed)
            retireNow(owed.mSlot);
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
        while (std::optional<FeedArrival> arrival = feed.take())
        {
            const FeedRequest& request = arrival->mRequest;
            if (request.mScene >= mScenes.size() || !mScenes[request.mScene].mActive)
                continue;
            Scene& scene = mScenes[request.mScene];
            if (!scene.mTextures.holds(request.mTexture))
                continue;
            TextureResident& texture = scene.mTextures[request.mTexture];
            auto* arriving = std::get_if<TextureArriving>(&texture.mStanding);
            if (arriving == nullptr || arriving->mTicket != request.mTicket)
                continue;
            assert(texture.mGeneration == request.mGeneration && "a ticket outlived its row's tenant");
            if (auto* failed = std::get_if<FeedFailed>(&arrival->mOutcome))
            {
                if (arriving->mNow.has_value())
                    scene.mDropsOwed.push_back(
                        OwedDrop{ .mTexture = request.mTexture, .mSlot = arriving->mNow->mSlot });
                scene.mFailed.push_back(FailedRow{ .mTexture = request.mTexture, .mWhy = std::move(failed->mWhy) });
                texture.mStanding = StandingFailed{ .mCause = failed->mCause };
                mPlanStale = true;
                continue;
            }

            // **Texels made in another shape than planned stand as they are**: the classes are priced again and the
            // plan run again, so the slot reserved holds what arrived.
            auto& made = std::get<FeedMade>(arrival->mOutcome);
            if (made.mShape != texture.mShape)
            {
                texture.mShape = made.mShape;
                priceLevels(texture, device);
                mPlanStale = true;
                if (texture.mClassAt[texture.mFloorLevel].isNone())
                {
                    if (arriving->mNow.has_value())
                        scene.mDropsOwed.push_back(
                            OwedDrop{ .mTexture = request.mTexture, .mSlot = arriving->mNow->mSlot });
                    scene.mFailed.push_back(
                        FailedRow{ .mTexture = request.mTexture, .mWhy = "the device holds none of its shape" });
                    texture.mStanding = StandingFailed{ .mCause = Refused::Texture };
                    continue;
                }
            }
            arriving->mMade = std::move(made);
        }

        mSpent = Shares{ .mUploadBytes = 0, .mCopyBytes = 0, .mBuildTriangles = 0, .mTightCopies = 0 };
        mReport.mOpsIssued = 0;
        mReport.mBytesUploaded = 0;
        mReport.mBytesCopied = 0;
        mReport.mTrianglesBuilt = 0;
        mReport.mTightCopies = 0;
        // **The plan is run again where an input moved**: the cap here; a row's tier, want or tenant as a scene is
        // handed over; a tight size, a shape or a failure above. What arrived or stood moves no input.
        if (mReport.mCapBytes != mBudget.mCapBytes)
            mPlanStale = true;
        mReport.mCapBytes = mBudget.mCapBytes;
    }

    void Residency::priceLevels(TextureResident& texture, const ResidencyDevice& device)
    {
        texture.mClassAt.fill(sNoRow);
        texture.mFloorLevel
            = texture.mShape.mSource == TextureSource::File || texture.mShape.mSource == TextureSource::SpriteBake
            ? floorLevelOf(texture.mShape, sFloorSide)
            : 0;
        for (std::uint8_t level = 0; level < texture.mShape.mLevels; ++level)
        {
            const std::optional<ContentPrice> price = device.priceImage(texture.mShape.from(level));
            texture.mClassAt[level]
                = price.has_value() ? mLedger.classOf(SlotKind::Image, price->mBytes, price->mAlignment) : sNoRow;
        }
        if (texture.mWantedLevel >= texture.mShape.mLevels)
            texture.mWantedLevel = texture.mShape.mLevels - 1;
    }

    SlotPlace Residency::placeOf(const Slot slot) const
    {
        return SlotPlace{ .mSlot = slot, .mBlock = mLedger.getBlockOf(slot), .mOffset = mLedger.getOffsetOf(slot) };
    }

    void Residency::retireAfter(const Slot slot)
    {
        // **Left at once**, so a drain's next move this hand-over does not take the slot again: it stays live in the
        // ledger until the epoch is known, and a move of it was issued once per round of `issueMoves` until the share
        // ran out, every copy after the first from a slot its tenant had left.
        mLedger.leave(slot);
        mFreed.push_back(slot);
    }

    void Residency::retireNow(const Slot slot)
    {
        mLedger.leave(slot);
        mLedger.retire(slot, mLastEpoch);
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

    std::optional<std::string> Residency::describeShape(
        TextureResident& texture, const TextureTable& table, const TextureIndex row, const ResidencyDevice& device)
    {
        const TextureRow& described = table.getRows()[row];
        switch (described.mKind)
        {
            case TextureKind::File:
                if (described.mImage == nullptr)
                {
                    texture.mStanding = StandingFailed{ .mCause = Refused::Texture };
                    return std::string(sNoImage);
                }
                // **A file in the wrong slot says so**, as the upload's own check words it: a normal map's BC5 read
                // as a colour is a known format, not an unreadable file.
                if (described.mFormat == TextureFormat::Unnamed)
                {
                    texture.mStanding = StandingFailed{ .mCause = Refused::Texture };
                    const Result<void, std::string> uploadable
                        = checkUploadable(*described.mImage, described.mEncoding);
                    return uploadable.isOk() ? std::string("its format is not one this renderer uploads")
                                             : uploadable.error();
                }
                texture.mShape = shapeOfFile(*described.mImage, described.mPartner.get(), described.mEncoding);
                break;
            case TextureKind::SpriteLight:
            {
                // Shaped as its source stands, which the source's own resident says once it is synced.
                const TextureIndex source = table.findFile(described.mPath);
                if (source == sNoRow)
                {
                    texture.mStanding = StandingFailed{ .mCause = Refused::Texture };
                    return std::string("no file of its own is held to bake from");
                }
                texture.mBakedFrom = source;
                texture.mBakedFromGeneration = table.generationOf(source);
                return std::nullopt;
            }
            case TextureKind::GroundAlbedo:
            case TextureKind::GroundGloss:
            {
                constexpr std::uint32_t extent = Shaders::GROUND_COMPOSITE_EXTENT;
                const bool gloss = described.mKind == TextureKind::GroundGloss;
                texture.mGroundOf = described.mGroundOf;
                texture.mShape
                    = ImageShape{ .mSource = gloss ? TextureSource::GroundGloss : TextureSource::GroundComposite,
                          .mFormat = gloss ? TextureFormat::Bc7Unorm : TextureFormat::Bc7Srgb,
                          .mWidth = extent,
                          .mHeight = extent,
                          .mLevels = static_cast<std::uint8_t>(levelsTo1x1(extent, extent)) };
                break;
            }
        }
        priceLevels(texture, device);
        if (!texture.mClassAt[texture.mFloorLevel].isNone())
            return std::nullopt;
        texture.mStanding = StandingFailed{ .mCause = Refused::Texture };
        return std::format("it is {} by {} texels, which this device holds no image of", texture.mShape.mWidth,
            texture.mShape.mHeight);
    }

    void Residency::syncTextures(Scene& scene, const HandOver& handing)
    {
        const TextureTable& table = handing.mScene.textures();
        const auto rows = table.getRows();
        if (rows.size() > scene.mTextures.size())
            scene.mTextures.resize(rows.size());

        // **A ground composite waiting to be made holds its layers at its own tier**: it is made of them, and a layer
        // held at the cache's tier alone was dropped where the cap was tight, which left the composite waiting for
        // good, "not yet asked" at every stop of the handheld's tour. Gathered first, so a layer's tier is taken once.
        mLayerTiers.assign(scene.mTextures.size(), HoldTier::Unheld);
        const MaterialTable& materials = handing.mScene.materials();
        for (std::uint32_t at = 0; at < rows.size(); ++at)
        {
            // As the table describes the row, which a composite taken this hand-over is not yet synced as.
            const TextureIndex row(at);
            if (!table.isLive(row) || rows[row].mGroundOf == sNoRow || !isDemanded(table.getTier(row)))
                continue;
            const TextureResident& composite = scene.mTextures[row];
            const bool same = composite.mLive && composite.mGeneration == table.generationOf(row);
            if (same
                && (std::holds_alternative<StandingFailed>(composite.mStanding) || standingOf(composite).has_value()))
                continue;
            const HoldTier tier = table.getTier(row);
            for (const MaterialLayer& layer :
                materials.getRows()[rows[row].mGroundOf].mLayers.in(materials.getLayers()))
                if (layer.mDiffuse != Shaders::TEXTURE_NEUTRAL && layer.mDiffuse < mLayerTiers.size())
                    mLayerTiers[layer.mDiffuse] = std::min(mLayerTiers[layer.mDiffuse], tier);
        }

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
                if (std::optional<std::string> why = describeShape(texture, table, row, handing.mDevice))
                    scene.mFailed.push_back(FailedRow{ .mTexture = row, .mWhy = std::move(*why) });
                mPlanStale = true;
            }
            if (std::holds_alternative<StandingFailed>(texture.mStanding))
                continue;

            const HoldTier tier = std::min(table.getTier(row), mLayerTiers[at]);
            if (tier != texture.mTier)
            {
                texture.mTier = tier;
                mPlanStale = true;
            }
            if (tier != HoldTier::Unheld)
                texture.mLastHeld = scene.mHandOvers;
            if (texture.mShape.mLevels == 0)
                continue;

            // A want rises at once and falls only once it has stood; a composite stands whole.
            const auto want = texture.mGroundOf != sNoRow
                ? std::uint8_t{ 0 }
                : static_cast<std::uint8_t>(std::min<std::size_t>(table.getWant(row), texture.mShape.mLevels - 1u));
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

        // **A sprite's light bake is shaped as its source stands**, read once every source is synced: a bake whose
        // source went is a bake of nothing.
        for (std::uint32_t at = 0; at < scene.mTextures.size(); ++at)
        {
            TextureResident& bake = scene.mTextures[TextureIndex(at)];
            if (!bake.mLive || bake.mBakedFrom == sNoRow || std::holds_alternative<StandingFailed>(bake.mStanding))
                continue;
            const TextureResident& source = scene.mTextures[bake.mBakedFrom];
            if (!source.mLive || source.mGeneration != bake.mBakedFromGeneration
                || std::holds_alternative<StandingFailed>(source.mStanding))
            {
                resetTexture(scene, TextureIndex(at), handing.mFeed);
                bake.mLive = true;
                bake.mGeneration = table.generationOf(TextureIndex(at));
                bake.mStanding = StandingFailed{ .mCause = Refused::Texture };
                scene.mFailed.push_back(
                    FailedRow{ .mTexture = TextureIndex(at), .mWhy = "the texture it bakes is no longer held" });
                continue;
            }
            ImageShape shape = source.mShape;
            shape.mSource = TextureSource::SpriteBake;
            shape.mFormat = TextureFormat::Rgba8Unorm;
            shape.mCompanion = TextureCompanion::Neutral;
            if (shape == bake.mShape)
                continue;
            bake.mShape = shape;
            bake.mWantedLevel = shape.mLevels - 1;
            priceLevels(bake, handing.mDevice);
            mPlanStale = true;
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

    void Residency::addUnit(const Scene& scene, const TextureResident& texture, const std::uint8_t level)
    {
        mCounts.add(classAt(texture, level));
        if (texture.mBake != sNoRow)
            if (const ClassIndex bake = classAt(scene.mTextures[texture.mBake], level); !bake.isNone())
                mCounts.add(bake);
    }

    void Residency::removeUnit(const Scene& scene, const TextureResident& texture, const std::uint8_t level)
    {
        mCounts.remove(classAt(texture, level));
        if (texture.mBake != sNoRow)
            if (const ClassIndex bake = classAt(scene.mTextures[texture.mBake], level); !bake.isNone())
                mCounts.remove(bake);
    }

    std::uint64_t Residency::unitStride(
        const Scene& scene, const TextureResident& texture, const std::uint8_t level) const
    {
        std::uint64_t stride = mLedger.getStrideBytes(classAt(texture, level));
        if (texture.mBake != sNoRow)
            if (const ClassIndex bake = classAt(scene.mTextures[texture.mBake], level); !bake.isNone())
                stride += mLedger.getStrideBytes(bake);
        return stride;
    }

    void Residency::plan()
    {
        mCounts.clear();
        const std::uint64_t cap = mBudget.mCapBytes;
        mReport.mStarvedTextures = 0;
        mReport.mStarvedStructures = 0;

        // 0. **A bake is planned as one with its source**, made from it at its level: a cap that holds the source and
        // not the bake beside it lowers both, where planning the bake after its source left it starved beside a
        // source that would have stood coarser for it.
        for (Scene& scene : mScenes)
        {
            for (TextureResident& texture : scene.mTextures)
            {
                texture.mPlannedLevel.reset();
                texture.mBake = sNoRow;
            }
            for (std::uint32_t row = 0; row < scene.mTextures.size(); ++row)
            {
                const TextureResident& bake = scene.mTextures[TextureIndex(row)];
                if (!bake.mLive || bake.mBakedFrom == sNoRow || std::holds_alternative<StandingFailed>(bake.mStanding)
                    || !isDemanded(bake.mTier) || bake.mShape.mLevels == 0)
                    continue;
                TextureResident& source = scene.mTextures[bake.mBakedFrom];
                assert(source.mBake == sNoRow && "a file with two bakes");
                assert(bake.mShape.mLevels == source.mShape.mLevels && "a bake shaped apart from its source");
                source.mBake = TextureIndex(row);
            }
        }

        // 1. What is demanded, at what it wants: a texture at its want, or the first level under it the device prices,
        // with its bake; a structure in the class it stands or will stand in.
        for (Scene& scene : mScenes)
        {
            for (TextureResident& texture : scene.mTextures)
            {
                if (!texture.mLive || std::holds_alternative<StandingFailed>(texture.mStanding)
                    || !isDemanded(texture.mTier) || texture.mBakedFrom != sNoRow || texture.mShape.mLevels == 0)
                    continue;
                std::uint8_t level = texture.mWantedLevel;
                while (level < texture.mFloorLevel && classAt(texture, level).isNone())
                    ++level;
                texture.mPlannedLevel = level;
                addUnit(scene, texture, level);
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
                    if (texture.mBakedFrom != sNoRow || !texture.mPlannedLevel.has_value() || !standing.has_value()
                        || *texture.mPlannedLevel >= *standing)
                        continue;
                    removeUnit(scene, texture, *texture.mPlannedLevel);
                    addUnit(scene, texture, *standing);
                    texture.mPlannedLevel = standing;
                }

        // 3. A bake at the level its source is planned at, where the device prices it there.
        for (Scene& scene : mScenes)
            for (const TextureResident& source : scene.mTextures)
                if (source.mBake != sNoRow && source.mPlannedLevel.has_value())
                    if (TextureResident& bake = scene.mTextures[source.mBake];
                        !classAt(bake, *source.mPlannedLevel).isNone())
                        bake.mPlannedLevel = source.mPlannedLevel;

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
            const Scene& scene = mScenes[sceneAt];
            const TextureResident& texture = scene.mTextures[TextureIndex(row)];
            if (!texture.mPlannedLevel.has_value() || *texture.mPlannedLevel >= texture.mFloorLevel)
                return;
            mWeighed.push_back(Weighed{ .mTier = texture.mTier,
                .mKey = unitStride(scene, texture, *texture.mPlannedLevel),
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
            const Scene& scene = mScenes[victim.mScene];
            TextureResident& texture = mScenes[victim.mScene].mTextures[TextureIndex(victim.mRow)];
            const std::uint8_t from = *texture.mPlannedLevel;
            std::uint8_t to = from + 1;
            while (to < texture.mFloorLevel && classAt(texture, to).isNone())
                ++to;
            removeUnit(scene, texture, from);
            addUnit(scene, texture, to);
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
                    texture.mPlannedLevel.has_value() && texture.mBakedFrom == sNoRow)
                    mWeighed.push_back(Weighed{ .mTier = texture.mTier,
                        .mKey = unitStride(scene, texture, *texture.mPlannedLevel),
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
            removeUnit(scene, texture, *texture.mPlannedLevel);
            texture.mPlannedLevel.reset();
            ++mReport.mStarvedTextures;
            if (texture.mBake != sNoRow)
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
        // **A drain that could not move a slot this hand-over is no drain**, and the transit chunk is wanted as though
        // none were: one waiting on another scene's hand-over, or on room its slots' class lost to the arrivals, waited
        // for good beside a composite the handheld's tour never stood, with half its budget free.
        const ArenaIndex arena = mLedger.getArena(of);
        const SlabTier tier = mLedger.getTier(of);
        const std::uint64_t chunks = std::uint64_t{ mLedger.getChunkCount(arena) } * mLedger.getLayout().mChunkBytes;
        if (tier != SlabTier::Large && chunks >= mCounts.chunkBytesIn(arena)
            && (mDrainStalled[static_cast<std::size_t>(arena)]
                || (!mLedger.nextToMove().has_value() && !mLedger.drainFor(arena, tier).has_value())))
            mTransitWanted[static_cast<std::size_t>(arena)] = true;
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
                .mTo = placeOf(to),
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
            if (mSpent.mCopyBytes >= shares.mCopyBytes)
                return;
            if (owner.mScene != indexOf(scene) || !mLedger.hasRoom(of))
            {
                mDrainStalled[static_cast<std::size_t>(mLedger.getArena(of))] = true;
                return;
            }
            const Slot to = mLedger.reserve(of, mLedger.getOwnerOf(*moving));
            mLedger.commit(to);
            if (owner.mStructure)
            {
                StructureResident& structure = scene.mStructures[MeshIndex(owner.mRow)];
                mOps.push_back(CopyStructure{ .mMesh = MeshIndex(owner.mRow),
                    .mGeneration = structure.mGeneration,
                    .mFrom = *moving,
                    .mTo = placeOf(to),
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
                    .mTo = placeOf(to),
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

    std::size_t Residency::describeBake(const SceneDesc& scene, const TextureResident& texture, const TextureIndex row)
    {
        const TextureRow& described = scene.textures().getRows()[row];
        TextureData bake{ .mSlot = row, .mWrap = described.mWrap, .mEncoding = described.mEncoding };
        if (texture.mBakedFrom != sNoRow)
        {
            bake.mSource = TextureSource::SpriteBake;
            bake.mFrom = texture.mBakedFrom;
            bake.mFormat = TextureFormat::Rgba8Unorm;
        }
        else
        {
            // The layers' diffuse slots the bake reads, the neutral texel a full table gave a layer aside.
            const std::size_t at = mBakeReads.size();
            const Material& chunk = scene.materials().getRows()[texture.mGroundOf];
            for (const MaterialLayer& layer : chunk.mLayers.in(scene.materials().getLayers()))
                if (layer.mDiffuse != Shaders::TEXTURE_NEUTRAL)
                    mBakeReads.push_back(TextureIndex(layer.mDiffuse));
            mBakeReadsAt.push_back(ReadsAt{ .mBake = mBakes.size(), .mAt = at, .mCount = mBakeReads.size() - at });
            bake.mSource = texture.mShape.mSource;
            bake.mFrom = texture.mGroundOf;
            bake.mFormat = texture.mShape.mFormat;
        }
        bake.mWidth = texture.mShape.mWidth;
        bake.mHeight = texture.mShape.mHeight;
        mBakes.push_back(bake);
        return mBakes.size() - 1;
    }

    void Residency::pointTexels()
    {
        for (const ReadsAt& reads : mBakeReadsAt)
            mBakes[reads.mBake].mReads = std::span<const TextureIndex>(mBakeReads).subspan(reads.mAt, reads.mCount);
        for (const TexelsAt& texels : mTexelsAt)
            std::get<StandImage>(mOps[texels.mOp]).mTexels
                = texels.mBake ? &mBakes[texels.mAt] : &mIssued[texels.mAt].mTexels;
    }

    void Residency::issueArrivals(Scene& scene, const HandOver& handing)
    {
        const Shares& shares = handing.mShares;
        TextureFeed& feed = handing.mFeed;
        const TextureTable& table = handing.mScene.textures();

        // Arrivals and raisings, the most protected first: texels asked of the feed, then stood once they came; a bake
        // once what it is made of stands.
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
                const ClassIndex of = classAt(texture, planned);

                // A bake stands whole from what it is made of: a sprite's light from its source at the same level, a
                // composite from its layers. **Each read as it stands after this hand-over's drops**, which come
                // first: a composite's queue saw its layers stand before the hand-over that dropped one, and the bake
                // drew the stand-in it read.
                if (texture.mBakedFrom != sNoRow || texture.mGroundOf != sNoRow)
                {
                    if (texture.mBakedFrom != sNoRow)
                    {
                        const auto* source = std::get_if<TextureStands>(&scene.mTextures[texture.mBakedFrom].mStanding);
                        if (source == nullptr || source->mLevel != planned)
                            continue;
                    }
                    else if (!layersStand(scene.mTextures, handing.mScene, texture.mGroundOf))
                        continue;
                    if (!ensureRoom(of))
                        continue;
                    const Slot to = mLedger.reserve(of, ownerOf(indexOf(scene), false, at));
                    mLedger.commit(to);
                    StandImage stand{
                        .mTexture = row, .mGeneration = texture.mGeneration, .mTo = placeOf(to), .mLevel = planned
                    };
                    if (now.has_value())
                    {
                        stand.mFrom = now->mSlot;
                        stand.mFromLevel = now->mLevel;
                        retireAfter(now->mSlot);
                    }
                    mTexelsAt.push_back(TexelsAt{
                        .mOp = mOps.size(), .mBake = true, .mAt = describeBake(handing.mScene, texture, row) });
                    mOps.push_back(stand);
                    texture.mStanding = TextureStands{ .mLevel = planned, .mSlot = to };
                    continue;
                }

                if (auto* arriving = std::get_if<TextureArriving>(&texture.mStanding))
                {
                    if (!arriving->mMade.has_value() || mSpent.mUploadBytes >= shares.mUploadBytes || !ensureRoom(of))
                        continue;
                    const Slot to = mLedger.reserve(of, ownerOf(indexOf(scene), false, at));
                    mLedger.commit(to);
                    StandImage stand{ .mTexture = row,
                        .mGeneration = texture.mGeneration,
                        .mTo = placeOf(to),
                        .mLevel = planned,
                        .mUpload = LevelSpan{ .mFirst = planned, .mEnd = end } };
                    if (now.has_value())
                    {
                        stand.mFrom = now->mSlot;
                        stand.mFromLevel = now->mLevel;
                        retireAfter(now->mSlot);
                    }
                    FeedMade& made = mIssued.emplace_back(std::move(*arriving->mMade));
                    made.mTexels.mSlot = row;
                    made.mTexels.mWrap = table.getRows()[row].mWrap;
                    made.mTexels.mWant = planned;
                    mTexelsAt.push_back(TexelsAt{ .mOp = mOps.size(), .mBake = false, .mAt = mIssued.size() - 1 });
                    mOps.push_back(stand);
                    texture.mStanding = TextureStands{ .mLevel = planned, .mSlot = to };
                    mSpent.mUploadBytes += mLedger.getStrideBytes(of);
                    mReport.mBytesUploaded += mLedger.getStrideBytes(of);
                    continue;
                }

                if (!feed.hasCredit())
                    continue;
                const TextureRow& described = table.getRows()[row];
                const FeedTicket ticket{ mNextTicket++ };
                feed.ask(FeedRequest{ .mTicket = ticket,
                    .mScene = indexOf(scene),
                    .mTexture = row,
                    .mGeneration = texture.mGeneration,
                    .mFile = described.mImage,
                    .mPartner = described.mPartner,
                    .mEncoding = described.mEncoding,
                    .mShape = texture.mShape,
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
                        .mLoose = placeOf(loose),
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
                    .mTo = placeOf(tight),
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
        mIssued.clear();
        mBakes.clear();
        mBakeReads.clear();
        mBakeReadsAt.clear();
        mTexelsAt.clear();
        mRefused.clear();
        mStandingChanges.clear();
        mStructureStandingChanges.clear();
        mDrainStalled.fill(false);
        ++scene.mHandOvers;
        for (const OwedDrop& owed : scene.mDropsOwed)
        {
            mOps.push_back(DropImage{ .mTexture = owed.mTexture, .mFrom = owed.mSlot });
            retireAfter(owed.mSlot);
        }
        scene.mDropsOwed.clear();
        syncTextures(scene, handing);
        if (mStandsStructures)
            syncStructures(scene, handing);
        if (mPlanStale)
            plan();

        issueDrops(scene, handing.mFeed);
        issueLowerings(scene, handing.mFeed, handing.mShares);
        issueMoves(scene, handing.mShares);
        issueArrivals(scene, handing);
        if (mStandsStructures)
            issueStructures(scene, handing.mShares);
        pointTexels();

        // What failed since the scene was last handed over, named as its table names it.
        for (FailedRow& failed : scene.mFailed)
            if (handing.mScene.textures().isLive(failed.mTexture))
                mRefused.push_back(Refusal{ .mKind = Refused::Texture,
                    .mName = handing.mScene.textures().getRows()[failed.mTexture].getName(),
                    .mWhy = std::move(failed.mWhy) });
        scene.mFailed.clear();

        const DeviceEpoch epoch = handing.mDevice.handOver(handing.mSlot, handing.mScene, mOps);
        mLastEpoch = epoch;
        for (const Slot freed : mFreed)
            mLedger.retire(freed, epoch);
        mReport.mOpsIssued += static_cast<std::uint32_t>(mOps.size());
        mReport.mSlots = mLedger.count();

        for (std::uint32_t at = 0; at < scene.mTextures.size(); ++at)
        {
            TextureResident& texture = scene.mTextures[TextureIndex(at)];
            const bool stands = texture.mLive && standingOf(texture).has_value();
            if (stands != texture.mStoodReported && texture.mLive)
                mStandingChanges.push_back(StandingChange{ .mTexture = TextureIndex(at), .mStands = stands });
            texture.mStoodReported = stands;
        }
        for (std::uint32_t at = 0; at < scene.mStructures.size(); ++at)
        {
            StructureResident& structure = scene.mStructures[MeshIndex(at)];
            const bool stands = structure.mLive && slotOf(structure).has_value();
            if (stands != structure.mStoodReported && structure.mLive)
                mStructureStandingChanges.push_back(
                    StructureStandingChange{ .mMesh = MeshIndex(at), .mStands = stands });
            structure.mStoodReported = stands;
        }

        std::uint32_t waiting = 0;
        std::uint32_t demanded = 0;
        MissingDemanded missing;
        for (const Scene& each : mScenes)
        {
            for (const TextureResident& texture : each.mTextures)
            {
                if (!isDemanded(texture.mTier) || !texture.mPlannedLevel.has_value()
                    || standingLevelOf(texture).has_value())
                    continue;
                const auto* const arriving = std::get_if<TextureArriving>(&texture.mStanding);
                if (arriving != nullptr)
                    ++(arriving->mMade.has_value() ? missing.mArrived : missing.mAsked);
                else if (texture.mBakedFrom != sNoRow)
                {
                    const std::optional<std::uint8_t> source = standingLevelOf(each.mTextures[texture.mBakedFrom]);
                    ++(source == texture.mPlannedLevel ? missing.mUnaskedForRoom : missing.mUnaskedBakes);
                }
                else if (texture.mGroundOf != sNoRow)
                    // The layers are told apart in the scene handed over alone, whose description this has.
                    ++(&each == &scene && layersStand(each.mTextures, handing.mScene, texture.mGroundOf)
                            ? missing.mUnaskedForRoom
                            : missing.mUnaskedComposites);
                else
                    ++missing.mUnaskedFiles;
            }
            missing.mMeshes += static_cast<std::uint32_t>(
                std::ranges::count_if(each.mStructures, [](const StructureResident& structure) {
                    return isDemanded(structure.mTier) && structure.mPlanned && !slotOf(structure).has_value();
                }));
            waiting += static_cast<std::uint32_t>(std::ranges::count_if(each.mTextures, textureWaits));
            waiting += static_cast<std::uint32_t>(std::ranges::count_if(each.mStructures, structureWaits));
            demanded += static_cast<std::uint32_t>(std::ranges::count_if(each.mTextures,
                [](const TextureResident& texture) { return isDemanded(texture.mTier) && textureWaits(texture); }));
            demanded += static_cast<std::uint32_t>(
                std::ranges::count_if(each.mStructures, [](const StructureResident& structure) {
                    return isDemanded(structure.mTier) && structureWaits(structure);
                }));
        }
        mReport.mWaiting = waiting;
        mReport.mWaitingDemanded = demanded;
        mReport.mMissingDemanded = missing;
        return epoch;
    }

    void Residency::closeFrame(ResidencyDevice& device)
    {
        // **A chunk is asked as the plan needs it**, where the plan stands in more chunks than the arena holds and has
        // asked, and a block of its own for each large slot the plan gives that has none; a free block is given back
        // only while what is held stands past the cap and a chunk past the plan. **Past the plan by one, the transit
        // chunk** (the cap's `transit`), where an operation found no room and nothing could drain to make it: the
        // arena holds the plan's chunks, but of other slabs than it now wants, as a lowering's destination of a
        // smaller class beside a full chunk of its source's. Its source retires, and the chunk it emptied goes back.
        const std::uint64_t chunkBytes = mLedger.getLayout().mChunkBytes;
        std::array<bool, 256> seen{};
        for (std::uint32_t at = 0; at < mLedger.getClassCount(); ++at)
        {
            const ArenaIndex arena = mLedger.getArena(ClassIndex(at));
            if (std::exchange(seen[static_cast<std::size_t>(arena)], true))
                continue;
            const std::uint64_t chunks = std::uint64_t{ mLedger.getChunkCount(arena) } * chunkBytes;
            const bool transit = std::exchange(mTransitWanted[static_cast<std::size_t>(arena)], false)
                && mCounts.chunkBytesIn(arena) + chunkBytes > chunks;
            if (mCounts.chunkBytesIn(arena) > chunks || transit)
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
