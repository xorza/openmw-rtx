#include "shadercode.hpp"

#include <algorithm>

#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/spirv/spirvfile.hpp>

namespace Rtx
{
    ShaderCode::ShaderCode(const Device& device)
        : mDevice(device)
    {
    }

    const void* ShaderCode::stage(const std::string_view module)
    {
        const auto known = std::ranges::find(mRead, module, &Read::mModule);
        if (known != mRead.end())
            return known->mStage;

        std::vector<std::uint32_t> words = readSpirv(mDevice.getShaderDirectory() / module);
        Read& read = mRead.emplace_back(Read{ .mModule = std::string(module), .mWords = std::move(words) });

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

        return read.mStage;
    }
}
