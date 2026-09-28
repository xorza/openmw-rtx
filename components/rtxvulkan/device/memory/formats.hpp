#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/crashcatcher/crash.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/shaders/storageformat.h>

namespace Rtx
{
    /// How a read of floats decodes a texel read back to the host: from halves, as floats, or not at
    /// all, for a format only a read of bytes takes.
    enum class TexelDecode
    {
        Bytes,
        Half,
        Float,
    };

    /// What a read-back needs of a format: how many bytes one texel takes, and how a read of floats
    /// decodes it.
    struct FormatInfo
    {
        std::uint32_t mTexelBytes;
        TexelDecode mDecode;
    };

    /// The one table of `FormatInfo`, asked by a read-back and by nothing else. Ends the process
    /// for a format no image here is read back in — every block format among them, whose texels
    /// have no size of their own — because one read here is a contract broken.
    FormatInfo formatInfoOf(VkFormat format);

    /// The one place a `TextureFormat` becomes Vulkan's. A colour's cases are sRGB, because the files
    /// hold display-encoded bytes and the hardware converts them in the filter; data's are UNORM.
    /// Ends the process for a format `describeImage` refuses, because one arriving here is a
    /// contract broken and not a file.
    VkFormat toVulkanFormat(TextureFormat format);

    /// The one place a shader's declared layout becomes Vulkan's: the format of the Vulkan
    /// specification's "Compatibility Between SPIR-V Image Formats and Vulkan Formats" table.
    /// Constant, so a format a pass names at namespace scope stays one.
    constexpr VkFormat toVulkanFormat(const Shaders::StorageFormat format)
    {
        switch (format)
        {
            case Shaders::StorageFormat::Rgba8:
                return VK_FORMAT_R8G8B8A8_UNORM;
            case Shaders::StorageFormat::R8:
                return VK_FORMAT_R8_UNORM;
            case Shaders::StorageFormat::R16:
                return VK_FORMAT_R16_UNORM;
            case Shaders::StorageFormat::R16f:
                return VK_FORMAT_R16_SFLOAT;
            case Shaders::StorageFormat::R32f:
                return VK_FORMAT_R32_SFLOAT;
            case Shaders::StorageFormat::Rg16f:
                return VK_FORMAT_R16G16_SFLOAT;
            case Shaders::StorageFormat::Rg32f:
                return VK_FORMAT_R32G32_SFLOAT;
            case Shaders::StorageFormat::Rgba16f:
                return VK_FORMAT_R16G16B16A16_SFLOAT;
            case Shaders::StorageFormat::Rgba32f:
                return VK_FORMAT_R32G32B32A32_SFLOAT;
        }

        Crash::fatal("a storage format with no Vulkan format");
    }
}
