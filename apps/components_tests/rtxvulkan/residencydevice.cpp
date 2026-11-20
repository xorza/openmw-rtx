#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/residencyconformance.hpp>
#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <components/rtx/renderer/residencydevice.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/scene/sceneidentities.hpp>
#include <components/rtxvulkan/vulkanrenderer.hpp>

namespace Rtx
{
    namespace
    {
        /// **The renderer the binary shares, as residency's device**, its world stood empty of a scene of its own,
        /// which every hand-over places: what the conformance suite runs over on a card.
        class SharedRenderer final : public ResidencyDevice
        {
        public:
            SharedRenderer()
                : mRenderer(Testing::getRenderer())
            {
                mRenderer.standScene(SceneSlot::world(), mScene);
            }

            ArenaIndex arenaOf(const SlotKind kind) const override { return mRenderer.arenaOf(kind); }
            std::optional<ContentPrice> priceImage(const ImageShape& shape) const override
            {
                return mRenderer.priceImage(shape);
            }
            ContentPrice priceStructure(const StructureShape& shape) const override
            {
                return mRenderer.priceStructure(shape);
            }
            bool standsStructures() const override { return mRenderer.standsStructures(); }
            ContentBudget openFrame(DeviceEvents& events) override { return mRenderer.openFrame(events); }
            DeviceEpoch handOver(SceneSlot slot, const SceneDesc& /*scene*/, std::span<const ResidencyOp> ops) override
            {
                // The scene the world was stood with, which a placement must name.
                return mRenderer.handOver(slot, mScene, ops);
            }
            void acquireBlock(const BlockIndex block, const ArenaIndex arena, const std::uint64_t bytes) override
            {
                mRenderer.acquireBlock(block, arena, bytes);
            }
            void releaseBlock(const BlockIndex block) override { mRenderer.releaseBlock(block); }
            void awaitBlocks() override { mRenderer.awaitBlocks(); }

        private:
            VulkanRenderer& mRenderer;
            SceneIdentities mIdentities;
            SceneDesc mScene{ mIdentities };
        };

        struct VulkanFactory
        {
            using Device = SharedRenderer;

            /// Frames a block's making on the device's thread may take: each opening a pass of the queue.
            static constexpr std::uint32_t sFramesMost = 100000;

            static std::unique_ptr<SharedRenderer> make() { return std::make_unique<SharedRenderer>(); }
        };
    }

    // gtest names the suite in identifiers it builds, so it is instantiated in the suite's own namespace.
    namespace Testing
    {
        INSTANTIATE_TYPED_TEST_SUITE_P(Vulkan, ResidencyDeviceConformance, VulkanFactory);
    }
}
