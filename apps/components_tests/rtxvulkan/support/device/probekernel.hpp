#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>

namespace Rtx::Testing
{
    /// One storage buffer a probe kernel binds, at `mBinding`.
    struct ProbeBuffer
    {
        std::uint32_t mBinding;
        const Buffer* mBuffer;
    };

    /// **Runs the probe kernel `module` once**, over `count` invocations in workgroups of `workgroup`, each of its
    /// bindings one of `buffers`, a storage buffer, pushed `constants`; and waits until the host can read what it
    /// wrote. What every test of a shader library's function through a kernel of its own does around the reading.
    template <class Constants>
    void runProbe(CommandPool& pool, std::string_view module, std::span<const ProbeBuffer> buffers,
        const Constants& constants, std::uint32_t count, std::uint32_t workgroup)
    {
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        for (const ProbeBuffer& buffer : buffers)
            bindings.push_back(VkDescriptorSetLayoutBinding{
                buffer.mBinding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr });
        const ComputePipeline<Constants> pipeline(pool.getDevice(), bindings, {}, module, module);

        DescriptorWrites writes(pipeline);
        for (const ProbeBuffer& buffer : buffers)
            writes.buffer(buffer.mBinding, VkDescriptorBufferInfo{ buffer.mBuffer->getHandle(), 0, VK_WHOLE_SIZE });

        pool.submitAndWait([&](VkCommandBuffer commands) {
            dispatch(commands, pipeline, writes, constants, Groups::along(count, workgroup));
            handOver(commands, Use::sBufferComputeWrite, Use::sBufferHostRead);
        });
    }
}
