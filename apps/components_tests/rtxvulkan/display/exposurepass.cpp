#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <volk.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <apps/components_tests/rtx/support/device/readback.hpp>
#include <components/rtx/shaders/look.h>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/display/exposurepass.hpp>

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
                const ExposurePass pass(device);
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

            /// One frame a sequence meters: every pixel's luminance, and how it follows the frame
            /// before.
            struct Metered
            {
                std::vector<float> mLuminances;
                float mElapsed = 0.0f;
                bool mReset = true;
            };

            /// What the pass holds after metering `frames` in order, each `sSide` square, with no
            /// bias: the exposure the last one left.
            static constexpr std::uint32_t sSide = 50;

            float meterAll(std::span<const Metered> frames)
            {
                Device& device = getDevice();
                const ExposurePass pass(device);
                const Buffer read
                    = Buffer::readBack(device, sizeof(float), VK_BUFFER_USAGE_TRANSFER_DST_BIT, "test-exposure-read");

                for (const Metered& metered : frames)
                {
                    EXPECT_EQ(metered.mLuminances.size(), std::size_t{ sSide } * sSide);
                    std::vector<float> pixels;
                    for (const float luminance : metered.mLuminances)
                        pixels.insert(pixels.end(), { luminance, luminance, luminance, 1.0f });

                    const Image frame = Testing::makeTestImage(
                        device, VkExtent2D{ sSide, sSide }, VK_FORMAT_R32G32B32A32_SFLOAT, "test-exposure-frame");
                    const Buffer staging = Buffer::hostWritten(device, pixels.size() * sizeof(float),
                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "test-exposure-staging");
                    staging.writeAt(0, std::span<const float>(pixels));

                    getPool().submitAndWait([&](VkCommandBuffer commands) {
                        frame.transition(commands, Use::sUndefined, Use::sCopyWrite);
                        const VkBufferImageCopy region = wholeLevel(0, 0, VkExtent3D{ sSide, sSide, 1 });
                        vkCmdCopyBufferToImage(commands, staging.getHandle(), frame.getHandle(),
                            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                        frame.transition(commands, Use::sCopyWrite, Use::sComputeRead);
                        pass.record(commands, frame, metered.mElapsed, metered.mReset, 1.0f);

                        pass.getExposure().transition(commands, Use::sBufferComputeWrite, Use::sBufferCopyRead);
                        pass.getExposure().copyTo(commands, read, sizeof(float));
                        read.orderForHostRead(commands);
                    });
                }

                return *static_cast<const float*>(read.map());
            }
        };

        /// A frame of one luminance, `sSide` square.
        std::vector<float> even(float luminance)
        {
            return std::vector<float>(
                std::size_t{ RtxExposurePassTest::sSide } * RtxExposurePassTest::sSide, luminance);
        }

        /// **The meter leaves out the brightest and the darkest tenth of the lit pixels**
        /// (`EXPOSURE_LOW_SHARE`). A frame at 0.18, the key, with eight per cent of its pixels at a
        /// hundred — a few flames in a room: every pixel between the tenth and the ninetieth share is
        /// at the key, so the frame meters as the even one does, to the bit. Over every lit pixel the
        /// flames pulled the eye shut by `(100 / 0.18)^(0.08 · 0.75)`, nearly half.
        ///
        /// **And it adapts in stops**: from an eye metered on the frame at 0.18 to the target of one
        /// at 0.018, which wants the eye open, after `ln 2` of the rising time constant the eye has
        /// closed half the gap in stops, to the geometric mean of the two, where half the gap in the
        /// exposure itself overshot it by `(e + t) / (2 sqrt(e t))`.
        TEST_F(RtxExposurePassTest, theMeterLeavesOutTheBrightestTenthAndAdaptsInStops)
        {
            std::vector<float> flames = even(0.18f);
            for (std::size_t at = 0; at < flames.size() * 8 / 100; ++at)
                flames[at * 12 % flames.size()] = 100.0f;

            const float keyed = meterAll(std::array{ Metered{ .mLuminances = even(0.18f) } });
            EXPECT_EQ(meterAll(std::array{ Metered{ .mLuminances = flames } }), keyed)
                << "the brightest tenth moved the meter";

            const float dark = meterAll(std::array{ Metered{ .mLuminances = even(0.018f) } });
            ASSERT_GT(dark, 2.0f * keyed) << "a frame ten times darker wants the eye open";
            ASSERT_LT(dark, Shaders::EXPOSURE_MAX);

            const float half = Shaders::EXPOSURE_RISE_SECONDS * std::numbers::ln2_v<float>;
            const float adapted = meterAll(std::array{ Metered{ .mLuminances = even(0.18f) },
                Metered{ .mLuminances = even(0.018f), .mElapsed = half, .mReset = false } });
            EXPECT_NEAR(adapted, std::sqrt(keyed * dark), std::sqrt(keyed * dark) * 1e-5f) << "half the gap in stops";
        }

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
