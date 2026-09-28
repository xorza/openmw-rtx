#include <array>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <components/rtx/shaders/look.h>
#include <components/rtxvulkan/buffer.hpp>
#include <components/rtxvulkan/commands.hpp>
#include <components/rtxvulkan/exposurepass.hpp>
#include <components/rtxvulkan/image.hpp>
#include <components/rtxvulkan/imageuse.hpp>

#include "support/device/harness.hpp"
#include "support/device/readback.hpp"

namespace Rtx
{
    namespace
    {
        struct RtxExposurePassTest : Testing::DeviceTest
        {
            /// What the pass meters a frame of `width` by `height` pixels, every one of them
            /// `luminance`, at: taken outright, as a reset takes it, and with no bias.
            float meter(std::uint32_t width, std::uint32_t height, float luminance)
            {
                Device& device = getDevice();
                const ExposurePass pass(device, Testing::getShaderDirectory());
                const Image frame = Testing::makeTestImage(
                    device, VkExtent2D{ width, height }, VK_FORMAT_R16G16B16A16_SFLOAT, "test-exposure-frame");
                const Buffer read
                    = Buffer::readBack(device, sizeof(float), VK_BUFFER_USAGE_TRANSFER_DST_BIT, "test-exposure-read");

                getPool().submitAndWait([&](VkCommandBuffer commands) {
                    frame.transition(commands, Use::sUndefined, Use::sClearWrite);

                    VkClearColorValue colour{};
                    const std::array<float, 4> value{ luminance, luminance, luminance, 1.0f };
                    std::memcpy(colour.float32, value.data(), sizeof(colour.float32));
                    const VkImageSubresourceRange whole{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                    vkCmdClearColorImage(
                        commands, frame.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &colour, 1, &whole);

                    frame.transition(commands, Use::sClearWrite, Use::sComputeRead);
                    pass.record(commands, frame, 0.0f, true, 1.0f);

                    pass.getExposure().transition(commands, Use::sBufferComputeWrite, Use::sBufferCopyRead);
                    pass.getExposure().copyTo(commands, read, sizeof(float));
                    read.orderForHostRead(commands);
                });

                return *static_cast<const float*>(read.map());
            }
        };

        /// A frame too large to sum its bins in a word meters as bright as it is.
        ///
        /// **The mean bin is each bin's index times its population, summed**: 255 times the pixels
        /// where every pixel is in the top bin, which passes 2^32 at 16,843,009 of them — an
        /// eight-thousand-line display. 4105 squared is 16,851,025, which a word wraps to 2,044,079:
        /// a mean bin of 0.12, a luminance of 9.4e-4 and an exposure of 52 for a frame at a
        /// thousand. The top bin stands for `2^MAX_LOG_LUMINANCE` = 640, which meters
        /// `(0.18 / 640)^0.75` = 0.0022 and is held at `EXPOSURE_MIN`.
        TEST_F(RtxExposurePassTest, aFrameTooLargeToSumItsBinsInAWordMetersAsBrightAsItIs)
        {
            EXPECT_FLOAT_EQ(meter(4105, 4105, 1000.0f), Shaders::EXPOSURE_MIN);
        }
    }
}
