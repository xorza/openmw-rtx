#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtx/support/testtexture.hpp>
#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/image/textureformat.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/batch.hpp>
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

                // Sized before anything stands, so no texture moves under the work naming it.
                std::vector<Texture> textures(count);
                std::vector<VkBufferImageCopy> regions;
                for (Texture& texture : textures)
                    EXPECT_TRUE(texture
                            .standFile(device, upload, arrival, file, 0, "arrival test", regions, MemoryUse::Essential)
                            .isOk());

                arrival.record(upload, passes, nullptr);
                upload.flush();
                return arrival.getRecordedBarriers();
            }
        };

        /// **A run's barriers are its phases', whatever its length.** Colours uploaded take two
        /// commands however many there are: the copies' way in, and their way out to the sampler.
        TEST_F(RtxTextureArrivalTest, aRunsBarriersAreItsPhasesWhateverItsLength)
        {
            const Testing::TestTexture painted = Testing::paintTwoTones(32, 96, TextureFormat::Rgba8Unorm);
            EXPECT_EQ(barriersFor(painted.mData, 1), 2u);
            EXPECT_EQ(barriersFor(painted.mData, 8), 2u);
        }
    }
}
