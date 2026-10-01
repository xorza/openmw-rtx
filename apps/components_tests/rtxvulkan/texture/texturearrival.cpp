#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <apps/components_tests/rtx/support/testtexture.hpp>
#include <components/rtx/image/mipchain.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
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
            chained.mCompleteChain = MipChain::wantedFor(chained);
            ASSERT_TRUE(chained.mCompleteChain);
            EXPECT_EQ(barriersFor(chained, 1), 12u);
            EXPECT_EQ(barriersFor(chained, 3), 12u);
        }
    }
}
