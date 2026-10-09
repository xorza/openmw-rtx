#include "formats.hpp"

#include <components/crashcatcher/crash.hpp>

namespace Rtx
{
    FormatInfo formatInfoOf(const VkFormat format)
    {
        for (const StorageFormatRow& row : sStorageFormats)
            if (row.mVulkan == format)
                return row.mInfo;

        // The formats a picture is presented or uploaded in, which no shader stores.
        switch (format)
        {
            case VK_FORMAT_R8G8B8A8_SRGB:
            case VK_FORMAT_B8G8R8A8_UNORM:
            case VK_FORMAT_B8G8R8A8_SRGB:
                return FormatInfo{ 4, TexelDecode::Bytes };
            default:
                break;
        }

        Crash::fatal("no read-back is recorded for this image format");
    }

    VkFormat toVulkanFormat(TextureFormat format)
    {
        switch (format)
        {
            case TextureFormat::Bc1RgbaSrgb:
                return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
            case TextureFormat::Bc2Srgb:
                return VK_FORMAT_BC2_SRGB_BLOCK;
            case TextureFormat::Bc3Srgb:
                return VK_FORMAT_BC3_SRGB_BLOCK;
            case TextureFormat::Rgba8Unorm:
                return VK_FORMAT_R8G8B8A8_UNORM;
            case TextureFormat::Rgba8Srgb:
                return VK_FORMAT_R8G8B8A8_SRGB;
            case TextureFormat::Bgra8Srgb:
                return VK_FORMAT_B8G8R8A8_SRGB;
            case TextureFormat::Bc1RgbaUnorm:
                return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
            case TextureFormat::Bc2Unorm:
                return VK_FORMAT_BC2_UNORM_BLOCK;
            case TextureFormat::Bc3Unorm:
                return VK_FORMAT_BC3_UNORM_BLOCK;
            case TextureFormat::Bgra8Unorm:
                return VK_FORMAT_B8G8R8A8_UNORM;
            case TextureFormat::Bc5Unorm:
                return VK_FORMAT_BC5_UNORM_BLOCK;
            case TextureFormat::Bc4Unorm:
                return VK_FORMAT_BC4_UNORM_BLOCK;
            case TextureFormat::Bc7Srgb:
                return VK_FORMAT_BC7_SRGB_BLOCK;
            case TextureFormat::Bc7Unorm:
                return VK_FORMAT_BC7_UNORM_BLOCK;

            // Never uploaded: `describeImage` widens these to RGBA8 and refuses the rest, so one
            // arriving here is a contract broken and not a file.
            case TextureFormat::Rgb565:
            case TextureFormat::Argb1555:
            case TextureFormat::Xrgb1555:
            case TextureFormat::Argb4444:
            case TextureFormat::Xrgb4444:
            case TextureFormat::Rgb8:
            case TextureFormat::Bgr8:
            case TextureFormat::Luminance:
            case TextureFormat::LuminanceAlpha:
            case TextureFormat::Alpha8:
            case TextureFormat::Red8:
            case TextureFormat::Rg8:
            case TextureFormat::Rgba16:
            case TextureFormat::Luminance16:
            case TextureFormat::LuminanceAlpha16:
            case TextureFormat::Red16:
            case TextureFormat::Rg16:
            case TextureFormat::Red16f:
            case TextureFormat::Rg16f:
            case TextureFormat::Rgb16f:
            case TextureFormat::Rgba16f:
            case TextureFormat::Red32f:
            case TextureFormat::Rg32f:
            case TextureFormat::Rgb32f:
            case TextureFormat::Rgba32f:
            case TextureFormat::Unnamed:
                break;
        }

        // A format nothing above named: a new one that forgets a case lands here rather than
        // creating an image with a format nobody chose.
        Crash::fatal("a texture format this renderer does not upload");
    }
}
