#include "shadercode.hpp"

#include <algorithm>
#include <format>
#include <utility>

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

    std::optional<std::string> bindingDisagreement(const std::span<const ModuleBinding> module, const SetTables& sets)
    {
        for (const ModuleBinding& bound : module)
        {
            if (bound.mSet == Shaders::SET_PASS && bound.mBinding == Shaders::BIND_CENSUS)
                continue;
            if (bound.mSet >= sets.size() || sets[bound.mSet] == nullptr)
                return std::format("the module reads set {}, which the layout does not name", bound.mSet);

            const std::span<const VkDescriptorSetLayoutBinding> declared = sets[bound.mSet]->get();
            const auto stated = std::ranges::find(declared, bound.mBinding, &VkDescriptorSetLayoutBinding::binding);
            if (stated == declared.end())
                return std::format(
                    "binding {} of set {} is in the module and not in the layout", bound.mBinding, bound.mSet);
            if (!bindsAs(bound.mKind, stated->descriptorType))
                return std::format("binding {} of set {} is descriptor type {} in the layout and another in the module",
                    bound.mBinding, bound.mSet, static_cast<int>(stated->descriptorType));
            // An array of no stated length takes whatever the layout gives it.
            if (bound.mCount != 0 && bound.mCount != stated->descriptorCount)
                return std::format("binding {} of set {} is {} in the layout and {} in the module", bound.mBinding,
                    bound.mSet, stated->descriptorCount, bound.mCount);
        }
        return std::nullopt;
    }

    std::optional<std::string> specializationDisagreement(
        const std::span<const ModuleSpecConstant> module, const std::span<const std::uint32_t> words)
    {
        for (const ModuleSpecConstant& constant : module)
        {
            if (constant.mId == Shaders::SPEC_CENSUS_KERNEL)
                continue;
            if (constant.mId >= words.size())
                return std::format(
                    "specialization constant {} is past the {} words the stage is handed, and would "
                    "take its default",
                    constant.mId, words.size());
            if (constant.mKind == SpecKind::Bool && words[constant.mId] > 1)
                return std::format(
                    "specialization constant {} is a bool and is handed {}", constant.mId, words[constant.mId]);
        }
        return std::nullopt;
    }

    std::optional<std::string> pushDisagreement(const std::optional<std::uint32_t> end, const std::uint32_t range)
    {
        if (!end.has_value())
            return std::nullopt;
        if (*end > range)
            return std::format("the push block ends at {} bytes, past the {} the layout declares", *end, range);
        if (*end < range && (range % 8 != 0 || range - *end >= 8))
            return std::format(
                "the push block ends at {} bytes, short of the {} the layout declares by more than a rounding to "
                "eight",
                *end, range);
        return std::nullopt;
    }

    namespace
    {
        /// The components a vertex attribute of `format` hands its input, for the formats this
        /// renderer describes a vertex with, and nothing for any other.
        std::optional<std::uint32_t> componentsOf(const VkFormat format)
        {
            switch (format)
            {
                case VK_FORMAT_R32_SFLOAT:
                case VK_FORMAT_R32_UINT:
                    return 1;
                case VK_FORMAT_R32G32_SFLOAT:
                    return 2;
                case VK_FORMAT_R32G32B32_SFLOAT:
                    return 3;
                case VK_FORMAT_R32G32B32A32_SFLOAT:
                case VK_FORMAT_R8G8B8A8_UNORM:
                    return 4;
                default:
                    return std::nullopt;
            }
        }
    }

    std::optional<std::string> inputDisagreement(
        const std::span<const ModuleInput> inputs, const std::span<const VkVertexInputAttributeDescription> attributes)
    {
        for (const ModuleInput& input : inputs)
        {
            const auto fed
                = std::ranges::find(attributes, input.mLocation, &VkVertexInputAttributeDescription::location);
            if (fed == attributes.end())
                return std::format("the input at location {} has no attribute", input.mLocation);
            const std::optional<std::uint32_t> components = componentsOf(fed->format);
            if (!components.has_value())
                return std::format("the attribute at location {} is of format {}, whose components this does not count",
                    input.mLocation, static_cast<int>(fed->format));
            if (*components < input.mComponents)
                return std::format("the input at location {} reads {} components of an attribute of {}",
                    input.mLocation, input.mComponents, *components);
        }
        return std::nullopt;
    }

    ShaderCode::ShaderCode(const Device& device, const std::span<const std::string_view> modules)
        : mDevice(device)
    {
        for (const std::string_view module : modules)
            if (std::ranges::find(mRead, module, &Read::mModule) == mRead.end())
                readModule(module);
    }

    const ShaderCode::Read& ShaderCode::readOf(const std::string_view module) const
    {
        const auto known = std::ranges::find(mRead, module, &Read::mModule);
        Crash::contract(known != mRead.end(), "a stage of a module its shader code was not made to read");
        return *known;
    }

    const void* ShaderCode::stage(const std::string_view module, const SetTables& sets, const std::uint32_t pushBytes,
        const std::span<const std::uint32_t> words) const
    {
        const Read& known = readOf(module);
        const ModuleInterface& interface = known.mInterface;
        for (const std::optional<std::string>& disagreement : { bindingDisagreement(interface.mBindings, sets),
                 specializationDisagreement(interface.mSpecConstants, words),
                 pushDisagreement(interface.mPushEnd, pushBytes) })
            if (disagreement.has_value())
                Crash::fatal(std::format("{}: {}", module, *disagreement));

        return known.mStage;
    }

    void ShaderCode::feed(
        const std::string_view module, const std::span<const VkVertexInputAttributeDescription> attributes) const
    {
        if (const std::optional<std::string> disagreement
            = inputDisagreement(readOf(module).mInterface.mInputs, attributes))
            Crash::fatal(std::format("{}: {}", module, *disagreement));
    }

    void ShaderCode::readModule(const std::string_view module)
    {
        // Both read before the entry is made, so a module either refuses leaves none behind.
        std::vector<std::uint32_t> words = readSpirv(mDevice.getShaderDirectory() / module);
        ModuleInterface interface = readInterface(words);
        Read& read = mRead.emplace_back(
            Read{ .mModule = std::string(module), .mWords = std::move(words), .mInterface = std::move(interface) });

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
