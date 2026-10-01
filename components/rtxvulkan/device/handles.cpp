#include "handles.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

#include <components/rtxvulkan/spirv/spirvfile.hpp>

#include "device.hpp"

namespace Rtx
{
    ShaderModule loadShaderModule(const Device& device, const std::string_view module)
    {
        const std::vector<std::uint32_t> words = readSpirv(device.getShaderDirectory() / module);

        const VkShaderModuleCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .codeSize = words.size() * sizeof(std::uint32_t),
            .pCode = words.data(),
        };

        ShaderModule handle = ShaderModule::make(device, vkCreateShaderModule, createInfo, "vkCreateShaderModule");

        device.setName(handle.get(), module);

        return handle;
    }

    Semaphore makeSemaphore(const Device& device)
    {
        const VkSemaphoreCreateInfo create{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0
        };
        return Semaphore::make(device, vkCreateSemaphore, create, "vkCreateSemaphore");
    }

    Immediate<VkSemaphore, vkDestroySemaphore> makeTimelineSemaphore(const Device& device, const std::string_view name)
    {
        const VkSemaphoreTypeCreateInfo type{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .pNext = nullptr,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = 0,
        };
        const VkSemaphoreCreateInfo create{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &type,
            .flags = 0,
        };
        Immediate<VkSemaphore, vkDestroySemaphore> handle = Immediate<VkSemaphore, vkDestroySemaphore>::make(
            device.getHandle(), vkCreateSemaphore, create, "vkCreateSemaphore");
        device.setName(handle.get(), name);
        return handle;
    }

    Fence makeSignalledFence(const Device& device)
    {
        const VkFenceCreateInfo create{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        return Fence::make(device, vkCreateFence, create, "vkCreateFence");
    }

    SetLayout makeSetLayout(const Device& device, std::span<const VkDescriptorSetLayoutBinding> bindings,
        VkDescriptorSetLayoutCreateFlags flags, const void* next)
    {
        const VkDescriptorSetLayoutCreateInfo describe{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = next,
            .flags = flags,
            .bindingCount = static_cast<std::uint32_t>(bindings.size()),
            .pBindings = bindings.data(),
        };
        return SetLayout(Owned<VkDescriptorSetLayout, vkDestroyDescriptorSetLayout>::make(
                             device, vkCreateDescriptorSetLayout, describe, "vkCreateDescriptorSetLayout"),
            bindings);
    }

    namespace
    {
        Sampler createSampler(const Device& device, const VkSamplerCreateInfo& describe, std::string_view name)
        {
            Sampler handle = Sampler::make(device, vkCreateSampler, describe, "vkCreateSampler");
            device.setName(handle.get(), name);
            return handle;
        }

        /// Linear within a level, over the whole chain, and comparing nothing: what every sampler here
        /// shares, with the levels blended as `mipmaps` says and each axis addressed as its mode says.
        VkSamplerCreateInfo describeLinear(const VkSamplerMipmapMode mipmaps, const VkSamplerAddressMode u,
            const VkSamplerAddressMode v, const VkSamplerAddressMode w)
        {
            return VkSamplerCreateInfo{
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .magFilter = VK_FILTER_LINEAR,
                .minFilter = VK_FILTER_LINEAR,
                .mipmapMode = mipmaps,
                .addressModeU = u,
                .addressModeV = v,
                .addressModeW = w,
                .mipLodBias = 0.0f,
                // Off, and not an oversight: every fetch through a content sampler names its own
                // level, and the device moves an explicit level under anisotropic filtering —
                // `makeFootprintSampler` turns it on for the reads that state a footprint instead.
                .anisotropyEnable = VK_FALSE,
                .maxAnisotropy = 0.0f,
                .compareEnable = VK_FALSE,
                .compareOp = VK_COMPARE_OP_NEVER,
                .minLod = 0.0f,
                .maxLod = VK_LOD_CLAMP_NONE,
                .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
                .unnormalizedCoordinates = VK_FALSE,
            };
        }

        /// Addressed as the file said: `makeContentSampler`'s shape.
        VkSamplerCreateInfo describeContent(const TextureWrap wrap)
        {
            return describeLinear(VK_SAMPLER_MIPMAP_MODE_LINEAR,
                clampsS(wrap) ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT,
                clampsT(wrap) ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT,
                VK_SAMPLER_ADDRESS_MODE_REPEAT);
        }
    }

    Sampler makeTargetSampler(const Device& device, std::string_view name)
    {
        return createSampler(device,
            describeLinear(VK_SAMPLER_MIPMAP_MODE_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE),
            name);
    }

    Sampler makeContentSampler(const Device& device, std::string_view name, const TextureWrap wrap)
    {
        return createSampler(device, describeContent(wrap), name);
    }

    Sampler makeFootprintSampler(
        const Device& device, std::string_view name, const TextureWrap wrap, const std::uint32_t anisotropy)
    {
        const float most
            = device.getPhysicalDevice().getProperties().mProperties2.properties.limits.maxSamplerAnisotropy;

        VkSamplerCreateInfo describe = describeContent(wrap);
        describe.anisotropyEnable = anisotropy > 1 ? VK_TRUE : VK_FALSE;
        describe.maxAnisotropy = std::min(static_cast<float>(anisotropy), most);

        return createSampler(device, describe, name);
    }

    Sampler makeBorderSampler(const Device& device, std::string_view name)
    {
        return createSampler(device,
            describeLinear(VK_SAMPLER_MIPMAP_MODE_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
                VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER),
            name);
    }

    PipelineLayout::PipelineLayout(const Device& device, std::span<const VkDescriptorSetLayoutBinding> bindings,
        const VkPushConstantRange& push, const SharedSetLayouts& shared)
        : mSetLayout(makeSetLayout(device, bindings, VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT))
        , mPush(push)
    {
        assert(push.offset == 0 && "a push range that does not start at nought");

        std::array<VkDescriptorSetLayout, Shaders::SET_COUNT> sets = shared.byNumber();
        sets[Shaders::SET_PASS] = mSetLayout.get();

        for (std::uint32_t set = 0; set < Shaders::SET_COUNT; ++set)
            if (sets[set] != VK_NULL_HANDLE)
                mSetCount = set + 1;

        for (std::uint32_t set = 0; set < mSetCount; ++set)
            assert(
                sets[set] != VK_NULL_HANDLE && "a shared set below one the pipeline reads, with no layout to name it");

        const bool pushes = push.size > 0;
        const VkPipelineLayoutCreateInfo pipelineLayout{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .setLayoutCount = mSetCount,
            .pSetLayouts = sets.data(),
            .pushConstantRangeCount = pushes ? 1u : 0u,
            .pPushConstantRanges = pushes ? &push : nullptr,
        };
        mHandle = Owned<VkPipelineLayout, vkDestroyPipelineLayout>::make(
            device, vkCreatePipelineLayout, pipelineLayout, "vkCreatePipelineLayout");
    }
}
