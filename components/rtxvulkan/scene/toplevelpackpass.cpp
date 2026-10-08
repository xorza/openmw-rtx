#include "toplevelpackpass.hpp"

#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>

namespace Rtx
{
    TopLevelPackPass::TopLevelPackPass(const Device& device)
        : mPipeline(device, {}, {}, "toplevelpack.comp.spv", "top level pack")
    {
    }

    void TopLevelPackPass::record(VkCommandBuffer commands, const Packing& what) const
    {
        dispatch(commands, mPipeline,
            Shaders::TopLevelPackConstants{
                .mRows = what.mRows, .mPacked = what.mPacked, .mStarts = what.mStarts, .mCount = what.mCount },
            Groups::along(what.mCount, Shaders::TOP_LEVEL_PACK_WORKGROUP));

        // The build reads the packed rows as its instances, which are build input read at the
        // build's own stage.
        handOver(commands, Use::sBufferComputeWrite,
            BufferUse{ VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_SHADER_READ_BIT });
    }
}
