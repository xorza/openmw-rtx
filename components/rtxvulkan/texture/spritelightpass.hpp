#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/rtx/shaders/spritelight.h>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>

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

        /// Records level `level` of `source`'s bake into `bake`. `source` is met as a texture the
        /// trace samples, which is how an upload leaves it; `bake` is met where a dispatch writes
        /// it. A level reads `source` alone, so every level of every bake runs without a barrier
        /// between. `bake` must hold as many levels as `source` and be writable as storage at each.
        void recordLevel(VkCommandBuffer commands, const Image& source, const Image& bake, std::uint32_t level) const;

    private:
        ComputePipeline<Shaders::SpriteLightConstants> mPipeline;
    };
}
