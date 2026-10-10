#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/crashcatcher/crash.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtx/shaders/storageformat.h>

namespace Rtx
{
    /// How a read of floats decodes a texel read back to the host: from halves, as floats, from
    /// bytes each a fraction of 255, or not at all, for a format only a read of bytes takes.
    enum class TexelDecode
    {
        Bytes,
        Unorm8,
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

    /// What a read-back needs of `format`: a storage format's row of `sStorageFormats`, and the
    /// formats no shader stores in beside them. Ends the process for a format no image here is read
    /// back in — every block format among them, whose texels have no size of their own — because
    /// one read here is a contract broken.
    FormatInfo formatInfoOf(VkFormat format);

    /// The one place a `TextureFormat` becomes Vulkan's. A colour's cases are sRGB, because the files
    /// hold display-encoded bytes and the hardware converts them in the filter; data's are UNORM.
    /// Ends the process for a format `describeImage` refuses, because one arriving here is a
    /// contract broken and not a file.
    VkFormat toVulkanFormat(TextureFormat format);

    /// One format a shader declares an image in, as the backend makes and reads it.
    struct StorageFormatRow
    {
        Shaders::StorageFormat mStorage;

        /// The format of the Vulkan specification's "Compatibility Between SPIR-V Image Formats and
        /// Vulkan Formats" table.
        VkFormat mVulkan;

        /// What an image of it is priced at, and how a read-back takes it.
        FormatInfo mInfo;
    };

    /// Every storage format, in `Shaders::StorageFormat`'s order: the one table its Vulkan format,
    /// its texel size and its decode are read off.
    inline constexpr std::array sStorageFormats{
        StorageFormatRow{ Shaders::StorageFormat::Rgba8, VK_FORMAT_R8G8B8A8_UNORM, { 4, TexelDecode::Unorm8 } },
        StorageFormatRow{ Shaders::StorageFormat::R8, VK_FORMAT_R8_UNORM, { 1, TexelDecode::Unorm8 } },
        StorageFormatRow{ Shaders::StorageFormat::Rg8, VK_FORMAT_R8G8_UNORM, { 2, TexelDecode::Unorm8 } },
        StorageFormatRow{ Shaders::StorageFormat::R16, VK_FORMAT_R16_UNORM, { 2, TexelDecode::Bytes } },
        StorageFormatRow{ Shaders::StorageFormat::R16f, VK_FORMAT_R16_SFLOAT, { 2, TexelDecode::Half } },
        StorageFormatRow{ Shaders::StorageFormat::R32f, VK_FORMAT_R32_SFLOAT, { 4, TexelDecode::Float } },
        StorageFormatRow{ Shaders::StorageFormat::R32ui, VK_FORMAT_R32_UINT, { 4, TexelDecode::Bytes } },
        StorageFormatRow{ Shaders::StorageFormat::Rg16f, VK_FORMAT_R16G16_SFLOAT, { 4, TexelDecode::Half } },
        StorageFormatRow{ Shaders::StorageFormat::Rg32f, VK_FORMAT_R32G32_SFLOAT, { 8, TexelDecode::Float } },
        StorageFormatRow{ Shaders::StorageFormat::Rg32ui, VK_FORMAT_R32G32_UINT, { 8, TexelDecode::Bytes } },
        StorageFormatRow{ Shaders::StorageFormat::Rgba16, VK_FORMAT_R16G16B16A16_UNORM, { 8, TexelDecode::Bytes } },
        StorageFormatRow{ Shaders::StorageFormat::Rgba16f, VK_FORMAT_R16G16B16A16_SFLOAT, { 8, TexelDecode::Half } },
        StorageFormatRow{ Shaders::StorageFormat::Rgba32f, VK_FORMAT_R32G32B32A32_SFLOAT, { 16, TexelDecode::Float } },
    };

    /// `format`'s row of `sStorageFormats`. Constant, so a format a pass names at namespace scope
    /// stays one.
    constexpr const StorageFormatRow& rowOf(const Shaders::StorageFormat format)
    {
        const StorageFormatRow& row = sStorageFormats.at(static_cast<std::size_t>(format));
        if (row.mStorage != format)
            Crash::fatal("sStorageFormats is out of Shaders::StorageFormat's order");
        return row;
    }

    /// The one place a shader's declared layout becomes Vulkan's.
    constexpr VkFormat toVulkanFormat(const Shaders::StorageFormat format)
    {
        return rowOf(format).mVulkan;
    }

    /// Whether `format` is a storage format that holds words (`Shaders::holdsWords`). False for one
    /// no shader stores in.
    constexpr bool holdsWords(const VkFormat format)
    {
        for (const StorageFormatRow& row : sStorageFormats)
            if (row.mVulkan == format)
                return Shaders::holdsWords(row.mStorage);
        return false;
    }

    /// A format with a transfer curve and its twin without one: the same bytes in the same
    /// compatibility class, read through the curve or as the bytes they are.
    struct CurveTwins
    {
        VkFormat mEncoded;
        VkFormat mLinear;
    };

    /// Every format a texture is uploaded or written in that has a curve, beside its twin — one
    /// table, so the two directions below cannot disagree.
    constexpr std::array sCurveTwins{
        CurveTwins{ VK_FORMAT_BC1_RGBA_SRGB_BLOCK, VK_FORMAT_BC1_RGBA_UNORM_BLOCK },
        CurveTwins{ VK_FORMAT_BC2_SRGB_BLOCK, VK_FORMAT_BC2_UNORM_BLOCK },
        CurveTwins{ VK_FORMAT_BC3_SRGB_BLOCK, VK_FORMAT_BC3_UNORM_BLOCK },
        CurveTwins{ VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM },
        CurveTwins{ VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_B8G8R8A8_UNORM },
    };

    /// `format` with its transfer curve taken off: the same bytes, read as the bytes they are.
    /// A format with no curve is its own.
    constexpr VkFormat withoutCurve(const VkFormat format)
    {
        for (const CurveTwins& twins : sCurveTwins)
            if (twins.mEncoded == format)
                return twins.mLinear;
        return format;
    }

    /// `format` read through a transfer curve, which is how the trace samples a written texture
    /// whose file was display-encoded.
    constexpr VkFormat withCurve(const VkFormat format)
    {
        for (const CurveTwins& twins : sCurveTwins)
            if (twins.mLinear == format)
                return twins.mEncoded;
        Crash::fatal("a format with no twin under a curve");
    }

    /// What a texture is created with: one made from a file, uploaded and sampled; and one the device
    /// writes, a chain, a bake or a composite, stored by a dispatch and sampled.
    inline constexpr VkImageUsageFlags sUploadedTextureUsage
        = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    inline constexpr VkImageUsageFlags sWrittenTextureUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    /// How many bytes a texel of `format` takes, which is what an image made from it is priced at.
    constexpr std::uint32_t texelBytes(const Shaders::StorageFormat format)
    {
        return rowOf(format).mInfo.mTexelBytes;
    }
}
