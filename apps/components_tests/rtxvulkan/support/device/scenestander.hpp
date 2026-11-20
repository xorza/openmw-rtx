#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

#include <components/rtx/common/deviceepoch.hpp>
#include <components/rtx/common/slotledger.hpp>
#include <components/rtx/image/imageshape.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/renderer/residencydevice.hpp>
#include <components/rtx/renderer/residencyop.hpp>
#include <components/rtx/renderer/slot.hpp>

namespace Rtx
{
    class Device;
    class SceneDesc;
    class SceneHolder;
}

namespace Rtx::Testing
{
    /// **What a GPU test stands a scene's textures with**: the textures it describes stood whole in slots of a ledger
    /// of its own, as residency would issue them, with no plan, cap or feed between — a test of what the device makes
    /// of an operation, not of what residency decides. Each texture from its want (`TextureData::mWant`), a bake from
    /// its description. One a renderer, kept across the tests that share it.
    class SceneStander
    {
    public:
        explicit SceneStander(ResidencyDevice& device);

        SceneStander(const SceneStander&) = delete;
        SceneStander& operator=(const SceneStander&) = delete;

        /// Gives back every block it was given: what is bound in them is the dead images of what it stood.
        ~SceneStander();

        /// The slot's first build, then `textures` stood and the scene placed where there are any: what stood of the
        /// slot's last scene retires.
        void setScene(
            SceneHolder& holder, SceneSlot slot, const SceneDesc& scene, std::span<const TextureData> textures);

        /// `arrived` stood by the next `placeScene`, beside what arrived in the scene.
        void extendScene(SceneSlot slot, const SceneDesc& scene, std::span<const TextureData> arrived);

        /// One hand-over: what arrived appended, what `extendScene` queued stood, and the scene placed.
        void placeScene(SceneHolder& holder, SceneSlot slot, const SceneDesc& scene);

        /// **The operations that stand `textures` in `slot`'s scene**, and drop what stood of `freed`, valid until the
        /// next call, for a caller that executes them itself (`TextureArray::execute`); `handedOver` then says the
        /// epoch they rode.
        std::span<const ResidencyOp> operationsFor(ResidencyDevice& device, SceneSlot slot,
            std::span<const TextureData> textures, std::span<const TextureIndex> freed = {});
        void handedOver(DeviceEpoch epoch);

    private:
        struct Standing
        {
            SceneSlot mScene;
            TextureIndex mTexture = sNoRow;
            Slot mSlot;
            ImageShape mShape;
        };

        /// Opens a frame: what finished collected, blocks arrived, the device's events read.
        void openFrame(ResidencyDevice& device);

        /// A slot of `of` reserved, asking the device for a block and waiting for it where none has room.
        Slot reserve(
            ResidencyDevice& device, ClassIndex of, SceneSlot scene, TextureIndex texture, const ImageShape& shape);

        /// One hand-over of what `extendScene` queued, dropping what stood of `freed`.
        void handOver(SceneHolder& holder, SceneSlot slot, const SceneDesc& scene, std::span<const TextureIndex> freed);

        /// Lets every slot of `scene` go, or only `texture`'s, retired at the epoch the next hand-over rides.
        void letGo(SceneSlot scene, TextureIndex texture);

        ResidencyDevice& mDevice;
        SlotLedger mLedger;
        std::vector<BlockIndex> mBlocks;
        DeviceEvents mEvents;
        DeviceEpoch mLast;
        std::vector<Standing> mStanding;

        /// What the next hand-over stands, each a copy of the test's description spanning the test's storage.
        struct Queued
        {
            SceneSlot mScene;
            TextureData mTexels;
        };
        std::vector<Queued> mQueued;
        std::vector<TextureData> mArrived;
        std::vector<TextureData> mTexels;
        std::vector<ResidencyOp> mOps;
        std::vector<Slot> mFreed;

        /// The scene's count of freed slots as each slot's hand-over last read it, the world's first.
        std::map<std::uint32_t, std::uint64_t> mFreedSeen;
    };

    /// The stander kept beside `renderer`, one a renderer the binary shares.
    SceneStander& standerOf(ResidencyDevice& renderer);

    /// Lets every stander go, giving its blocks back: before the renderers they stand on go.
    void releaseStanders();

    /// **The device's content arena as a device residency hands operations to**, for a test that stands textures in an
    /// array of its own and executes them itself: prices and blocks the device's, and no scene to hand over.
    class ArenaDevice final : public ResidencyDevice
    {
    public:
        explicit ArenaDevice(const Device& device)
            : mDevice(device)
        {
        }

        ArenaIndex arenaOf(SlotKind kind) const override;
        std::optional<ContentPrice> priceImage(const ImageShape& shape) const override;
        ContentPrice priceStructure(const StructureShape& shape) const override;
        bool standsStructures() const override { return false; }
        ContentBudget openFrame(DeviceEvents& events) override;
        DeviceEpoch handOver(SceneSlot slot, const SceneDesc& scene, std::span<const ResidencyOp> ops) override;
        void acquireBlock(BlockIndex block, ArenaIndex arena, std::uint64_t bytes) override;
        void releaseBlock(BlockIndex block) override;
        void awaitBlocks() override;

    private:
        const Device& mDevice;
    };
}
