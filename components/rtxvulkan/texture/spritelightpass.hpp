#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/shaders/shared/spritelight.h>

namespace Rtx
{
    class Device;
    class Image;

    /// A sprite texture's light bake, made on the device from the texture's alpha as the bake's
    /// slot arrives: `spritelight.comp`, which says what the bake is and why it is here and not on
    /// the host. One dispatch a level, into a chain shaped like the source's.
    class SpriteLightPass
    {
    public:
        explicit SpriteLightPass(const Device& device);

        /// Records stage `stage` (`SPRITE_LIGHT_FROM_*`) of level `level` of `source`'s bake into
        /// `bake`. `source` is met as a texture the trace samples, which is how an upload leaves it;
        /// `bake` is met where a dispatch reads and writes it. A stage reads back what the stages
        /// before it wrote, so the caller orders each stage after the one before; a level reads
        /// `source` alone, so every level of every bake runs one stage without a barrier between.
        /// `bake` must hold as many levels as `source` and be readable and writable as storage at
        /// each.
        void recordStage(VkCommandBuffer commands, const Image& source, const Image& bake, std::uint32_t level,
            std::uint32_t stage) const;

    private:
        ComputePipeline<Shaders::SpriteLightConstants> mPipeline;
    };
}
