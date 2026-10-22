#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <apps/components_tests/rtx/support/mipchain.hpp>
#include <apps/components_tests/rtx/support/testtexture.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/rtxvulkan/texture/texture.hpp>
#include <components/rtxvulkan/texture/texturearrival.hpp>
#include <components/rtxvulkan/texture/texturepasses.hpp>

namespace Rtx
{
    namespace
    {
        struct RtxTextureArrivalTest : Testing::DeviceTest
        {
            /// Stands `count` textures of `file` in one run and says how many barrier commands the
            /// run recorded.
            std::size_t barriersFor(const TextureData& file, std::size_t count)
            {
                Device& device = getDevice();
                const TexturePasses passes(device);

                Batch upload(getPool());
                TextureArrival arrival(device);
                arrival.open(count);

                // Sized before anything stands, so no texture moves under the work naming it.
                std::vector<Texture> textures(count);
                std::vector<VkBufferImageCopy> regions;
                for (Texture& texture : textures)
                    EXPECT_TRUE(
                        texture
                            .standFile(device, upload, arrival, file, 0, "arrival test", regions, MemoryUse::Essential)
                            .isOk());

                arrival.record(upload, passes);
                upload.flush();
                return arrival.getRecordedBarriers();
            }

            /// What one run made of `files`, each kept as BC7 off a loose chain in a canvas of `room`
            /// a group: each file's first level as the sampler decodes it, four bytes a texel, and
            /// the bytes the canvas came to.
            struct Encoded
            {
                std::vector<std::vector<std::uint8_t>> mDecoded;
                VkDeviceSize mCanvas = 0;
            };

            Encoded encodeAll(std::span<const TextureData> files, const VkDeviceSize room)
            {
                Device& device = getDevice();
                const TexturePasses passes(device);

                Batch upload(getPool());
                TextureArrival arrival(device, sSpreadMeansRoom, room);
                arrival.open(files.size());

                // Sized before anything is made, so nothing the run names moves.
                std::vector<Image> sources;
                std::vector<Image> targets;
                sources.reserve(files.size());
                targets.reserve(files.size());
                for (const TextureData& file : files)
                {
                    const std::uint32_t levels = levelsTo1x1(file.mWidth, file.mHeight);
                    const Image& source = sources.emplace_back(MemoryUse::Essential, device,
                        ImageDescription{ .mWidth = file.mWidth,
                            .mHeight = file.mHeight,
                            .mFormat = VK_FORMAT_R8G8B8A8_UNORM,
                            .mUsage = sUploadedTextureUsage },
                        "encode test source");
                    const Image& target = targets.emplace_back(MemoryUse::Essential, device,
                        ImageDescription{ .mWidth = file.mWidth,
                            .mHeight = file.mHeight,
                            .mFormat = VK_FORMAT_BC7_UNORM_BLOCK,
                            .mUsage = sUploadedTextureUsage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                            .mMipLevels = levels },
                        "encode test target");
                    const std::array regions{ wholeLevel(0, 0, VkExtent3D{ file.mWidth, file.mHeight, 1 }) };
                    arrival.upload(upload, source, file.mBytes, regions);
                    arrival.encode(source,
                        ImageDescription{ .mWidth = file.mWidth,
                            .mHeight = file.mHeight,
                            .mFormat = VK_FORMAT_R8G8B8A8_UNORM,
                            .mUsage = sWrittenTextureUsage,
                            .mMipLevels = levels },
                        false, file.mEncoding, target);
                }
                arrival.record(upload, passes);
                upload.flush();

                Encoded encoded{ .mDecoded = {}, .mCanvas = arrival.getLooseBytes() };
                for (const Image& target : targets)
                {
                    const Image decoded(MemoryUse::Essential, device,
                        ImageDescription{ .mWidth = target.getWidth(),
                            .mHeight = target.getHeight(),
                            .mFormat = VK_FORMAT_R8G8B8A8_UNORM,
                            .mUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                                | VK_IMAGE_USAGE_SAMPLED_BIT },
                        "encode test decoded");
                    const VkOffset3D end{ static_cast<std::int32_t>(target.getWidth()),
                        static_cast<std::int32_t>(target.getHeight()), 1 };
                    const VkImageBlit blit{
                        .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                        .srcOffsets = { VkOffset3D{ 0, 0, 0 }, end },
                        .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                        .dstOffsets = { VkOffset3D{ 0, 0, 0 }, end },
                    };
                    getPool().submitAndWait([&](VkCommandBuffer commands) {
                        target.transition(commands, Use::sTextureSample, Use::sBlitRead);
                        decoded.transition(commands, Use::sUndefined, Use::sBlitWrite);
                        vkCmdBlitImage(commands, target.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            decoded.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
                        decoded.transition(commands, Use::sBlitWrite, Use::sCopyRead);
                    });
                    decoded.read(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, encoded.mDecoded.emplace_back());
                }
                return encoded;
            }
        };

        /// **A run's barriers are its phases', whatever its length.** Textures uploaded and shaded
        /// take four commands however many there are: the copies' way in; their way out beside the
        /// maps' way in; the sums against the maps; and the maps' way to the sampler. A chain adds
        /// one a level, of every chain at once: the two tones made complete from their one level of
        /// 128 go to one texel in eight levels, so four and eight, whether one stands or three.
        TEST_F(RtxTextureArrivalTest, aRunsBarriersAreItsPhasesWhateverItsLength)
        {
            const Testing::TestTexture painted = Testing::paintTwoTones(32, 96, TextureFormat::Rgba8Unorm);
            ASSERT_FALSE(painted.mData.mCompleteChain);
            EXPECT_EQ(barriersFor(painted.mData, 1), 4u);
            EXPECT_EQ(barriersFor(painted.mData, 8), 4u);

            TextureData chained = painted.mData;
            chained.mCompleteChain = chained.wantsCompletedChain();
            ASSERT_TRUE(chained.mCompleteChain);
            EXPECT_EQ(barriersFor(chained, 1), 12u);
            EXPECT_EQ(barriersFor(chained, 3), 12u);
        }

        /// **Loose chains made a group at a time over one canvas encode what each made alone**: two
        /// files past the loose side, one dark and one light, each its own group under a room of one
        /// byte, so the light file's chain stands in the memory the dark one's was encoded from. Each
        /// comes out as it does when it is a run's only file, and the canvas is one chain long. Under
        /// a room both fit in, the two stand side by side and the canvas is both.
        TEST_F(RtxTextureArrivalTest, looseChainsMadeAGroupAtATimeEncodeWhatEachMadeAlone)
        {
            Testing::TestTexture dark;
            Testing::paintLevels(dark, sLargestLooseChainSide + 8, 8, 1, "dark", 64);
            Testing::TestTexture light;
            Testing::paintLevels(light, sLargestLooseChainSide + 8, 8, 1, "light", 192);
            const std::array files{ dark.mData, light.mData };

            const Encoded darkAlone = encodeAll(std::span(files).subspan(0, 1), sLooseChainRoom);
            const Encoded lightAlone = encodeAll(std::span(files).subspan(1, 1), sLooseChainRoom);
            const Encoded grouped = encodeAll(files, 1);
            const Encoded together = encodeAll(files, sLooseChainRoom);

            ASSERT_EQ(grouped.mDecoded.size(), 2u);
            EXPECT_EQ(grouped.mDecoded[0], darkAlone.mDecoded[0]);
            EXPECT_EQ(grouped.mDecoded[1], lightAlone.mDecoded[0]) << "the light file read the dark one's chain";
            EXPECT_EQ(together.mDecoded[0], darkAlone.mDecoded[0]);
            EXPECT_EQ(together.mDecoded[1], lightAlone.mDecoded[0]);
            EXPECT_EQ(grouped.mDecoded[1][0], 192) << "a uniform level decodes to its own value";

            const VkMemoryRequirements one = Image::requirementsFor(getDevice(),
                ImageDescription{ .mWidth = sLargestLooseChainSide + 8,
                    .mHeight = 8,
                    .mFormat = VK_FORMAT_R8G8B8A8_UNORM,
                    .mUsage = sWrittenTextureUsage,
                    .mMipLevels = levelsTo1x1(sLargestLooseChainSide + 8, 8) });
            EXPECT_EQ(grouped.mCanvas, one.size) << "a group past the room was not made over the one before";
            EXPECT_EQ(together.mCanvas, alignUp(one.size, one.alignment) + one.size);
        }
    }
}
