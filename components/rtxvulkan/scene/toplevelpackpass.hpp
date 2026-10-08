#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/shaders/shared/toplevelpack.h>

namespace Rtx
{
    class Device;

    /// Packs a top level's rows for its build: the rows that place an instance, in slot order, and
    /// none of the rows between them (`toplevelpack.comp`). One pipeline, which every scene records
    /// ahead of each build of its top level.
    class TopLevelPackPass
    {
    public:
        explicit TopLevelPackPass(const Device& device);

        /// What one packing reads and writes: the rows by slot, where they go, each block's first
        /// place, and how many rows there are by slot.
        struct Packing
        {
            VkDeviceAddress mRows = 0;
            VkDeviceAddress mPacked = 0;
            VkDeviceAddress mStarts = 0;
            std::uint32_t mCount = 0;
        };

        /// Records the packing and orders its writes against the build that reads them. The rows
        /// and the starts were written by the host ahead of the submit.
        void record(VkCommandBuffer commands, const Packing& what) const;

    private:
        ComputePipeline<Shaders::TopLevelPackConstants> mPipeline;
    };
}
