#include "shadercode.hpp"

#include <algorithm>
#include <format>
#include <iterator>

#include <components/crashcatcher/crash.hpp>
#include <components/rtxvulkan/device/bindingtable.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/shaders/shared/counts.h>
#include <components/rtxvulkan/shaders/shared/sets.h>
#include <components/rtxvulkan/spirv/spirvfile.hpp>

namespace Rtx
{
    namespace
    {
        /// The descriptor types a resource of `kind` may be bound as.
        bool bindsAs(const DescriptorKind kind, const VkDescriptorType type)
        {
            switch (kind)
            {
                case DescriptorKind::BareSampler:
                    return type == VK_DESCRIPTOR_TYPE_SAMPLER;
                case DescriptorKind::SampledImage:
                    return type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                case DescriptorKind::CombinedImageSampler:
                    return type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                case DescriptorKind::StorageImage:
                    return type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                case DescriptorKind::UniformTexelBuffer:
                    return type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
                case DescriptorKind::StorageTexelBuffer:
                    return type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
                case DescriptorKind::UniformBuffer:
                    return type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                        || type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
                case DescriptorKind::StorageBuffer:
                    return type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                        || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
                case DescriptorKind::AccelerationStructure:
                    return type == VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
            }
            return false;
        }
    }

    std::optional<std::string> passBindingDisagreement(
        const std::span<const ModuleBinding> module, const std::span<const VkDescriptorSetLayoutBinding> declared)
    {
        for (const ModuleBinding& bound : module)
        {
            if (bound.mSet != Shaders::SET_PASS || bound.mBinding == Shaders::BIND_CENSUS)
                continue;

            const auto stated = std::ranges::find(declared, bound.mBinding, &VkDescriptorSetLayoutBinding::binding);
            if (stated == declared.end())
                return std::format("binding {} is in the module and not in the layout", bound.mBinding);
            if (!bindsAs(bound.mKind, stated->descriptorType))
                return std::format("binding {} is descriptor type {} in the layout and another in the module",
                    bound.mBinding, static_cast<int>(stated->descriptorType));
            // An array of no stated length takes whatever the layout gives it.
            if (bound.mCount != 0 && bound.mCount != stated->descriptorCount)
                return std::format("binding {} is {} in the layout and {} in the module", bound.mBinding,
                    stated->descriptorCount, bound.mCount);
        }
        return std::nullopt;
    }

    ShaderCode::ShaderCode(const Device& device)
        : mDevice(device)
    {
    }

    const void* ShaderCode::stage(const std::string_view module, const BindingTable& pass)
    {
        auto known = std::ranges::find(mRead, module, &Read::mModule);
        if (known == mRead.end())
        {
            readModule(module);
            known = std::prev(mRead.end());
        }

        if (const std::optional<std::string> disagreement = passBindingDisagreement(known->mBindings, pass.get()))
            Crash::fatal(std::format("{}: {}", module, *disagreement));

        return known->mStage;
    }

    void ShaderCode::readModule(const std::string_view module)
    {
        std::vector<std::uint32_t> words = readSpirv(mDevice.getShaderDirectory() / module);
        Read& read
            = mRead.emplace_back(Read{ .mModule = std::string(module), .mWords = std::move(words), .mBindings = {} });
        readBindings(read.mWords, read.mBindings);

        read.mCreate = VkShaderModuleCreateInfo{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .codeSize = read.mWords.size() * sizeof(std::uint32_t),
            .pCode = read.mWords.data(),
        };
        // Named through the stage, since there is no module object to name: a validation message
        // about a stage then says which file it came from.
        read.mNamed = VkDebugUtilsObjectNameInfoEXT{
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
            .pNext = &read.mCreate,
            .objectType = VK_OBJECT_TYPE_SHADER_MODULE,
            .objectHandle = 0,
            .pObjectName = read.mModule.c_str(),
        };
        read.mStage = mDevice.namesObjects() ? static_cast<const void*>(&read.mNamed) : &read.mCreate;
    }
}
