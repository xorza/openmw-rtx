#include "scenestander.hpp"

#include <algorithm>
#include <cassert>
#include <map>
#include <memory>
#include <thread>

#include <components/crashcatcher/crash.hpp>
#include <components/rtx/image/imageshape.hpp>
#include <components/rtx/renderer/sceneholder.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/meshtable.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/texturetable.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/contentarena.hpp>
#include <components/rtxvulkan/device/timeline.hpp>
#include <components/rtxvulkan/scene/bottomlevelstore.hpp>
#include <components/rtxvulkan/texture/texture.hpp>

namespace Rtx::Testing
{
    namespace
    {
        /// The shape `texture` stands as from its want: a file's levels from there, a bake shaped by what it is made
        /// of.
        ImageShape shapeOf(const TextureData& texture)
        {
            if (texture.mSource == TextureSource::File || texture.mSource == TextureSource::StandIn)
            {
                const std::uint8_t first = std::min<std::uint8_t>(
                    texture.mWant, static_cast<std::uint8_t>(std::max<std::size_t>(texture.mLevels.size(), 1) - 1));
                const MipLevel& top = texture.mLevels[first];
                return ImageShape{ .mSource = TextureSource::File,
                    .mFormat = texture.mFormat,
                    .mWidth = top.mWidth,
                    .mHeight = top.mHeight,
                    .mLevels = static_cast<std::uint8_t>(texture.mLevels.size() - first),
                    .mCompanion = texture.getCompanion() };
            }
            constexpr std::uint32_t extent = Shaders::GROUND_COMPOSITE_EXTENT;
            return ImageShape{ .mSource = texture.mSource,
                .mFormat = texture.mFormat,
                .mWidth = extent,
                .mHeight = extent,
                .mLevels = static_cast<std::uint8_t>(levelsTo1x1(extent, extent)) };
        }

    }

    SceneStander::SceneStander(ResidencyDevice& device)
        : mDevice(device)
        , mLedger([&] {
            SlotLayout layout;
            layout.mArenaOf = { device.arenaOf(SlotKind::Image), device.arenaOf(SlotKind::Buffer) };
            return layout;
        }())
    {
    }

    SceneStander::~SceneStander()
    {
        for (const BlockIndex block : mBlocks)
            mDevice.releaseBlock(block);
    }

    void SceneStander::openFrame(ResidencyDevice& device)
    {
        std::ignore = device.openFrame(mEvents);
        mLedger.collect(mEvents.mFinished);
        for (const BlockIndex block : mEvents.mBlocksArrived)
        {
            mLedger.blockArrived(block);
            mBlocks.push_back(block);
        }
        for (const BlockIndex block : mEvents.mBlocksRefused)
            mLedger.blockRefused(block);
    }

    Slot SceneStander::reserve(ResidencyDevice& device, const ClassIndex of, const SceneSlot scene,
        const TextureIndex texture, const ImageShape& shape)
    {
        const Slot slot = reserveSlot(device, of);
        mStanding.push_back(Standing{ .mScene = scene, .mTexture = texture, .mSlot = slot, .mShape = shape });
        return slot;
    }

    Slot SceneStander::reserveSlot(ResidencyDevice& device, const ClassIndex of)
    {
        if (!mLedger.hasRoom(of))
        {
            const BlockIndex block = mLedger.getTier(of) == SlabTier::Large ? mLedger.askLarge(of)
                                                                            : mLedger.askChunk(mLedger.getArena(of));
            device.acquireBlock(block, mLedger.getArena(of), mLedger.getBlockBytes(block));
            // The device's thread makes it; a test waits, which no frame does.
            for (std::uint32_t tries = 0; !mLedger.hasRoom(of); ++tries)
            {
                assert(tries < 100000 && "a block the device never made");
                std::this_thread::yield();
                openFrame(device);
            }
        }
        const Slot slot = mLedger.reserve(of, SlotOwner{ mNextOwner++ });
        mLedger.commit(slot);
        return slot;
    }

    void SceneStander::buildStructure(const SceneSlot slot, const SceneDesc& scene, const MeshIndex mesh)
    {
        const MeshRange& range = scene.meshes().getRows()[mesh];
        if (range.getTriangleCount() == 0)
            return;
        const StructureShape shape{
            .mTriangles = range.getTriangleCount(), .mVertices = range.mVertices.mCount, .mRefits = range.deforms()
        };
        const ContentPrice price = mDevice.priceStructure(shape);
        const Slot to = reserveSlot(mDevice, mLedger.classOf(SlotKind::Buffer, price.mBytes, price.mAlignment));
        mStructures.push_back(StructureStanding{ .mScene = slot, .mMesh = mesh, .mSlot = to });
        mOps.push_back(BuildStructure{ .mMesh = mesh,
            .mGeneration = scene.meshes().generationOf(mesh),
            .mLoose = SlotPlace{ .mSlot = to, .mBlock = mLedger.getBlockOf(to), .mOffset = mLedger.getOffsetOf(to) },
            .mBytes = price.mBytes });
    }

    void SceneStander::structureOperations(
        const SceneSlot slot, const SceneDesc& scene, MeshesSeen& seen, const bool every)
    {
        const MeshTable& meshes = scene.meshes();
        if (meshes.getFreedCount() != seen.mFreed)
            for (const MeshIndex gone : meshes.getFreed())
                std::erase_if(mStructures, [&](const StructureStanding& standing) {
                    if (standing.mScene != slot || standing.mMesh != gone)
                        return false;
                    mOps.push_back(DropStructure{ .mMesh = gone, .mFrom = standing.mSlot });
                    mFreed.push_back(standing.mSlot);
                    return true;
                });
        if (every)
        {
            for (std::uint32_t at = 0; at < meshes.getRows().size(); ++at)
                if (meshes.isLive(MeshIndex(at)))
                    buildStructure(slot, scene, MeshIndex(at));
        }
        else if (meshes.getRevision() != seen.mRevision)
            for (const MeshIndex arrived : meshes.getArrived())
                buildStructure(slot, scene, arrived);
        seen = MeshesSeen{ .mRevision = meshes.getRevision(), .mFreed = meshes.getFreedCount() };
    }

    void SceneStander::letGo(const SceneSlot scene, const TextureIndex texture)
    {
        std::erase_if(mStanding, [&](const Standing& standing) {
            const bool goes = standing.mScene == scene && (texture == sNoRow || standing.mTexture == texture);
            if (goes)
                mFreed.push_back(standing.mSlot);
            return goes;
        });
    }

    void SceneStander::setScene(
        SceneHolder& holder, const SceneSlot slot, const SceneDesc& scene, std::span<const TextureData> textures)
    {
        // What stood of the slot's last scene goes with its build, read up to the last hand-over.
        letGo(slot, sNoRow);
        for (const Slot freed : mFreed)
            mLedger.retire(freed, mLast);
        mFreed.clear();
        std::erase_if(mQueued, [&](const Queued& queued) { return queued.mScene == slot; });
        std::erase_if(mStructures, [&](const StructureStanding& standing) {
            if (standing.mScene == slot)
                mLedger.retire(standing.mSlot, mLast);
            return standing.mScene == slot;
        });
        holder.standScene(slot, scene);
        const std::uint32_t at = slot.isWorld() ? 0 : slot.getViewIndex() + 1;
        mFreedSeen[at] = scene.textures().getFreedCount();

        // A build places what it built; a hand-over follows only for something to stand, so a scene of nothing leaves
        // nothing deferred behind a build that waited.
        if (textures.empty() && scene.meshes().getLiveCount() == 0)
            return;
        extendScene(slot, scene, textures);
        mArrived.clear();
        for (const Queued& queued : mQueued)
            if (queued.mScene == slot)
                mArrived.push_back(queued.mTexels);
        std::erase_if(mQueued, [&](const Queued& queued) { return queued.mScene == slot; });
        std::ignore = operationsFor(holder, slot, mArrived, {});
        MeshesSeen& seen = mMeshesSeen[at];
        seen = MeshesSeen{};
        structureOperations(slot, scene, seen, true);
        handedOver(holder.handOver(slot, scene, mOps));
    }

    void SceneStander::extendScene(
        const SceneSlot slot, const SceneDesc& /*scene*/, std::span<const TextureData> arrived)
    {
        for (const TextureData& texture : arrived)
            mQueued.push_back(Queued{ .mScene = slot, .mTexels = texture });
    }

    void SceneStander::placeScene(SceneHolder& holder, const SceneSlot slot, const SceneDesc& scene)
    {
        mArrived.clear();
        for (const Queued& queued : mQueued)
            if (queued.mScene == slot)
                mArrived.push_back(queued.mTexels);
        std::erase_if(mQueued, [&](const Queued& queued) { return queued.mScene == slot; });
        // What the scene freed since it was last read, as the device reads it: once its count of freed slots moves.
        std::uint64_t& seen = mFreedSeen[slot.isWorld() ? 0 : slot.getViewIndex() + 1];
        const bool freed = scene.textures().getFreedCount() != seen;
        seen = scene.textures().getFreedCount();
        handOver(holder, slot, scene, freed ? scene.textures().getFreed() : std::span<const TextureIndex>{});
    }

    void SceneStander::handOver(
        SceneHolder& holder, const SceneSlot slot, const SceneDesc& scene, std::span<const TextureIndex> freed)
    {
        std::ignore = operationsFor(holder, slot, mArrived, freed);
        structureOperations(slot, scene, mMeshesSeen[slot.isWorld() ? 0 : slot.getViewIndex() + 1], false);
        handedOver(holder.handOver(slot, scene, mOps));
    }

    std::span<const ResidencyOp> SceneStander::operationsFor(ResidencyDevice& device, const SceneSlot slot,
        std::span<const TextureData> textures, std::span<const TextureIndex> freed)
    {
        openFrame(device);
        mOps.clear();

        // What the scene gave back since: what stood of it goes as residency's drops would.
        for (const TextureIndex gone : freed)
            for (const Standing& standing : mStanding)
                if (standing.mScene == slot && standing.mTexture == gone)
                    mOps.push_back(DropImage{ .mTexture = gone, .mFrom = standing.mSlot });
        for (const ResidencyOp& op : mOps)
            letGo(slot, std::get<DropImage>(op).mTexture);

        // Every source before every bake, as the device records them; what stood of a slot stood again goes as the
        // hand-over rides.
        mTexels.clear();
        for (const TextureData& texture : textures)
            if (texture.mSource != TextureSource::StandIn)
                mTexels.push_back(texture);
        std::ranges::stable_sort(mTexels, {}, [](const TextureData& texture) {
            return texture.mSource == TextureSource::File ? 0 : texture.mSource == TextureSource::SpriteBake ? 1 : 2;
        });
        for (const TextureData& texture : mTexels)
        {
            // A sprite's bake is shaped as its source stands, which stood ahead of it.
            ImageShape shape = shapeOf(texture);
            if (texture.mSource == TextureSource::SpriteBake)
            {
                const TextureIndex source = std::get<TextureIndex>(texture.mFrom);
                const auto stood = std::ranges::find_if(mStanding,
                    [&](const Standing& standing) { return standing.mScene == slot && standing.mTexture == source; });
                assert(stood != mStanding.end() && "a sprite's bake stood with no source standing");
                shape = stood->mShape;
                shape.mSource = TextureSource::SpriteBake;
                shape.mFormat = texture.mFormat;
                shape.mCompanion = TextureCompanion::Neutral;
            }
            const std::optional<ContentPrice> price = device.priceImage(shape);
            if (!price.has_value())
                continue;
            std::optional<Slot> from;
            for (const Standing& standing : mStanding)
                if (standing.mScene == slot && standing.mTexture == texture.mSlot)
                    from = standing.mSlot;
            if (from.has_value())
                letGo(slot, texture.mSlot);
            const Slot to = reserve(
                device, mLedger.classOf(SlotKind::Image, price->mBytes, price->mAlignment), slot, texture.mSlot, shape);
            mOps.push_back(StandImage{ .mTexture = texture.mSlot,
                .mTo = SlotPlace{ .mSlot = to, .mBlock = mLedger.getBlockOf(to), .mOffset = mLedger.getOffsetOf(to) },
                .mLevel = texture.mSource == TextureSource::File ? texture.mWant : std::uint8_t{ 0 },
                .mFrom = from,
                .mUpload
                = LevelSpan{ .mFirst = texture.mWant, .mEnd = static_cast<std::uint8_t>(texture.mLevels.size()) },
                .mTexels = &texture });
        }
        return mOps;
    }

    void SceneStander::handedOver(const DeviceEpoch epoch)
    {
        mLast = epoch;
        for (const Slot freed : mFreed)
            mLedger.retire(freed, epoch);
        mFreed.clear();
    }

    namespace
    {
        /// One a renderer the binary shares, kept for the run as the renderer is, and let go of before it.
        std::map<const ResidencyDevice*, std::unique_ptr<SceneStander>>& standers()
        {
            static std::map<const ResidencyDevice*, std::unique_ptr<SceneStander>> held;
            return held;
        }
    }

    SceneStander& standerOf(ResidencyDevice& renderer)
    {
        std::unique_ptr<SceneStander>& stander = standers()[&renderer];
        if (stander == nullptr)
            stander = std::make_unique<SceneStander>(renderer);
        return *stander;
    }

    void releaseStanders()
    {
        standers().clear();
    }

    void releaseStanderOf(const ResidencyDevice& renderer)
    {
        standers().erase(&renderer);
    }

    ArenaIndex ArenaDevice::arenaOf(const SlotKind kind) const
    {
        return mDevice.getContent().arenaOf(kind);
    }

    std::optional<ContentPrice> ArenaDevice::priceImage(const ImageShape& shape) const
    {
        return Texture::price(mDevice, shape);
    }

    ContentPrice ArenaDevice::priceStructure(const StructureShape& shape) const
    {
        return BottomLevelStore::price(mDevice, shape);
    }

    ContentBudget ArenaDevice::openFrame(DeviceEvents& events)
    {
        events.clear();
        mDevice.getContent().collect(events.mBlocksArrived, events.mBlocksRefused);
        events.mFinished = DeviceEpoch{ .mValue = mDevice.getTimeline().poll() };
        return ContentBudget{ .mCapBytes = std::uint64_t{ 1 } << 40 };
    }

    DeviceEpoch ArenaDevice::handOver(
        SceneSlot /*slot*/, const SceneDesc& /*scene*/, std::span<const ResidencyOp> /*ops*/)
    {
        Crash::fatal("a scene handed over to a test's arena, which executes its own operations");
    }

    void ArenaDevice::acquireBlock(const BlockIndex block, const ArenaIndex arena, const std::uint64_t bytes)
    {
        mDevice.getContent().acquire(block, arena, bytes);
    }

    void ArenaDevice::awaitBlocks()
    {
        mDevice.getContent().awaitIdle();
    }

    void ArenaDevice::releaseBlock(const BlockIndex block)
    {
        mDevice.getContent().release(block);
    }
}
