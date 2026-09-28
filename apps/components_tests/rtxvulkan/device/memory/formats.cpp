#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/death.hpp>
#include <components/rtx/image/texels.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/shaders/storageformat.h>
#include <components/rtxvulkan/device/memory/formats.hpp>

namespace Rtx
{
    namespace
    {
        using Shaders::StorageFormat;

        /// **A layout a shader declares is created as the format the specification pairs it with.**
        /// A view in any other format is undefined behaviour that the layers only warn about, so
        /// this mapping is the whole of what keeps an image and its declaration agreeing.
        ///
        /// Copied by hand from the Vulkan specification's "Compatibility Between SPIR-V Image
        /// Formats and Vulkan Formats" table, one row per qualifier `storageformat.h` spells.
        TEST(RtxFormatsTest, aStorageFormatIsTheOneTheSpecificationPairsItsQualifierWith)
        {
            constexpr std::array<std::pair<StorageFormat, VkFormat>, 8> sTable{ {
                { StorageFormat::Rgba8, VK_FORMAT_R8G8B8A8_UNORM },
                { StorageFormat::R16, VK_FORMAT_R16_UNORM },
                { StorageFormat::R16f, VK_FORMAT_R16_SFLOAT },
                { StorageFormat::R32f, VK_FORMAT_R32_SFLOAT },
                { StorageFormat::Rg16f, VK_FORMAT_R16G16_SFLOAT },
                { StorageFormat::Rg32f, VK_FORMAT_R32G32_SFLOAT },
                { StorageFormat::Rgba16f, VK_FORMAT_R16G16B16A16_SFLOAT },
                { StorageFormat::Rgba32f, VK_FORMAT_R32G32B32A32_SFLOAT },
            } };

            for (const auto& [format, expected] : sTable)
                EXPECT_EQ(toVulkanFormat(format), expected) << "layout " << static_cast<int>(format);
        }

        /// **Every format this uploads is the block or order its file holds, with the curve where it
        /// is a colour and without it where it is data.** A normal map read through the sRGB curve
        /// bends every direction toward one corner, and nothing downstream could tell.
        TEST(RtxFormatsTest, anUploadedFormatIsItsFilesBlockWithTheCurveOnlyForAColour)
        {
            constexpr std::array<std::pair<TextureFormat, VkFormat>, 11> sTable{ {
                { TextureFormat::Bc1RgbaSrgb, VK_FORMAT_BC1_RGBA_SRGB_BLOCK },
                { TextureFormat::Bc2Srgb, VK_FORMAT_BC2_SRGB_BLOCK },
                { TextureFormat::Bc3Srgb, VK_FORMAT_BC3_SRGB_BLOCK },
                { TextureFormat::Rgba8Unorm, VK_FORMAT_R8G8B8A8_UNORM },
                { TextureFormat::Rgba8Srgb, VK_FORMAT_R8G8B8A8_SRGB },
                { TextureFormat::Bgra8Srgb, VK_FORMAT_B8G8R8A8_SRGB },
                { TextureFormat::Bc1RgbaUnorm, VK_FORMAT_BC1_RGBA_UNORM_BLOCK },
                { TextureFormat::Bc2Unorm, VK_FORMAT_BC2_UNORM_BLOCK },
                { TextureFormat::Bc3Unorm, VK_FORMAT_BC3_UNORM_BLOCK },
                { TextureFormat::Bgra8Unorm, VK_FORMAT_B8G8R8A8_UNORM },
                { TextureFormat::Bc5Unorm, VK_FORMAT_BC5_UNORM_BLOCK },
            } };

            std::size_t uploadable = 0;
            for (std::size_t at = 0; at < sTextureFormatCount; ++at)
                uploadable += isUploadable(static_cast<TextureFormat>(at)) ? 1 : 0;
            EXPECT_EQ(uploadable, sTable.size()) << "an uploadable format no row above names";

            for (const auto& [format, expected] : sTable)
                EXPECT_EQ(toVulkanFormat(format), expected) << nameOf(format);
        }

        /// **A read-back knows every format it reads, from one table.** The bytes one texel takes are
        /// what a read-back buffer is sized by, and the decode is what a read of floats does with
        /// them; two tables that disagreed once read the motion channels as pairs of halves. A block
        /// format has no texel of its own and is never read back, so it ends the process.
        ///
        /// Each row by hand: the channels times the bytes of one channel — one byte for 8 bits,
        /// two for a half or a 16-bit channel, four for a float.
        TEST(RtxFormatsTest, aReadBackFormatIsItsTexelSizeAndItsDecode)
        {
            struct Row
            {
                VkFormat mFormat;
                std::uint32_t mBytes;
                TexelDecode mDecode;
            };
            constexpr std::array<Row, 13> sTable{ {
                { VK_FORMAT_R8_UNORM, 1, TexelDecode::Bytes },
                { VK_FORMAT_R8G8_UNORM, 2, TexelDecode::Bytes },
                { VK_FORMAT_R16_UNORM, 2, TexelDecode::Bytes },
                { VK_FORMAT_R16_SFLOAT, 2, TexelDecode::Half },
                { VK_FORMAT_R8G8B8A8_UNORM, 4, TexelDecode::Bytes },
                { VK_FORMAT_R8G8B8A8_SRGB, 4, TexelDecode::Bytes },
                { VK_FORMAT_B8G8R8A8_UNORM, 4, TexelDecode::Bytes },
                { VK_FORMAT_B8G8R8A8_SRGB, 4, TexelDecode::Bytes },
                { VK_FORMAT_R16G16_SFLOAT, 4, TexelDecode::Half },
                { VK_FORMAT_R32_SFLOAT, 4, TexelDecode::Float },
                { VK_FORMAT_R16G16B16A16_SFLOAT, 8, TexelDecode::Half },
                { VK_FORMAT_R32G32_SFLOAT, 8, TexelDecode::Float },
                { VK_FORMAT_R32G32B32A32_SFLOAT, 16, TexelDecode::Float },
            } };

            for (const Row& row : sTable)
            {
                const FormatInfo info = formatInfoOf(row.mFormat);
                EXPECT_EQ(info.mTexelBytes, row.mBytes) << "format " << row.mFormat;
                EXPECT_EQ(info.mDecode, row.mDecode) << "format " << row.mFormat;
            }

            // Every storage layout a pass declares can be read back, since a channel is one.
            for (const StorageFormat format :
                { StorageFormat::Rgba8, StorageFormat::R16, StorageFormat::R16f, StorageFormat::R32f,
                    StorageFormat::Rg16f, StorageFormat::Rg32f, StorageFormat::Rgba16f, StorageFormat::Rgba32f })
                EXPECT_GT(formatInfoOf(toVulkanFormat(format)).mTexelBytes, 0u) << static_cast<int>(format);

            Testing::expectDies(
                [] { formatInfoOf(VK_FORMAT_BC1_RGBA_SRGB_BLOCK); }, "no read-back is recorded for this image format");
        }
    }
}
