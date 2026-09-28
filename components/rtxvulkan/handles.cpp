#include "handles.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <components/files/conversion.hpp>

#include "device.hpp"
#include "spirvfile.hpp"

namespace Rtx
{
    ShaderModule loadShaderModule(const Device& device, const std::filesystem::path& path)
    {
        const std::vector<std::uint32_t> words = readSpirv(path);

        const VkShaderModuleCreateInfo createInfo{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = words.size() * sizeof(std::uint32_t),
            .pCode = words.data(),
        };

        ShaderModule handle
            = ShaderModule::make(device.getHandle(), vkCreateShaderModule, createInfo, "vkCreateShaderModule");

        // The name is built from a path, so it can throw — and a handle already made is destroyed
        // on the way out.
        device.setName(handle.get(), Files::pathToUnicodeString(path.filename()).c_str());

        return handle;
    }

    Semaphore makeSemaphore(const Device& device)
    {
        const VkSemaphoreCreateInfo create{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        return Semaphore::make(device.getHandle(), vkCreateSemaphore, create, "vkCreateSemaphore");
    }

    Semaphore makeTimelineSemaphore(const Device& device, const std::string_view name)
    {
        const VkSemaphoreTypeCreateInfo type{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = 0,
        };
        const VkSemaphoreCreateInfo create{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &type,
        };
        Semaphore handle = Semaphore::make(device.getHandle(), vkCreateSemaphore, create, "vkCreateSemaphore");
        device.setName(handle.get(), name);
        return handle;
    }

    Fence makeSignalledFence(const Device& device)
    {
        const VkFenceCreateInfo create{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        return Fence::make(device.getHandle(), vkCreateFence, create, "vkCreateFence");
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
        return SetLayout::make(
            device.getHandle(), vkCreateDescriptorSetLayout, describe, "vkCreateDescriptorSetLayout");
    }

    namespace
    {
        Sampler createSampler(const Device& device, const VkSamplerCreateInfo& describe, std::string_view name)
        {
            Sampler handle = Sampler::make(device.getHandle(), vkCreateSampler, describe, "vkCreateSampler");
            device.setName(handle.get(), name);
            return handle;
        }

        /// Linear over the whole chain, addressed as the file said: `makeContentSampler`'s shape.
        VkSamplerCreateInfo describeContent(const TextureWrap wrap)
        {
            return VkSamplerCreateInfo{
                .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                .magFilter = VK_FILTER_LINEAR,
                .minFilter = VK_FILTER_LINEAR,
                .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
                .addressModeU = clampsS(wrap) ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .addressModeV = clampsT(wrap) ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT,
                .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
                // Off, and not an oversight: every fetch through a content sampler names its own
                // level, and the device moves an explicit level under anisotropic filtering —
                // `makeFootprintSampler` turns it on for the reads that state a footprint instead.
                .anisotropyEnable = VK_FALSE,
                .maxLod = VK_LOD_CLAMP_NONE,
            };
        }
    }

    Sampler makeTargetSampler(const Device& device, std::string_view name)
    {
        const VkSamplerCreateInfo describe{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
            .maxLod = VK_LOD_CLAMP_NONE,
        };

        return createSampler(device, describe, name);
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
        const VkSamplerCreateInfo describe{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
            .maxLod = VK_LOD_CLAMP_NONE,
            .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
        };

        return createSampler(device, describe, name);
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
            .setLayoutCount = mSetCount,
            .pSetLayouts = sets.data(),
            .pushConstantRangeCount = pushes ? 1u : 0u,
            .pPushConstantRanges = pushes ? &push : nullptr,
        };
        mHandle = Owned<VkPipelineLayout, vkDestroyPipelineLayout>::make(
            device.getHandle(), vkCreatePipelineLayout, pipelineLayout, "vkCreatePipelineLayout");
    }
}
