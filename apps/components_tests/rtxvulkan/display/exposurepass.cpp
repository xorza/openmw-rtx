#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <optional>
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
            /// `luminance`, at, with no bias: the frame metered twice, from a reset and then over a
            /// time long enough that the eye closes the whole gap (`sSettling`).
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
                    pass.record(commands, frame, 0.0f, EyeStart::Day, 1.0f);
                });

                // A submit of its own, whose head barrier orders its histogram's clear after the
                // reduction above read it.
                getPool().submitAndWait([&](VkCommandBuffer commands) {
                    pass.record(commands, frame, sSettling, std::nullopt, 1.0f);

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

                /// Where a reset starts the eye: the game's day unless a test of the other says.
                EyeStart mStart = EyeStart::Day;
            };

            /// What the pass holds after metering `frames` in order, each `sSide` square, with no
            /// bias: the exposure the last one left.
            static constexpr std::uint32_t sSide = 50;

            /// A time over which the eye closes the whole gap to what it meters, either way:
            /// `exp(-1000 / EXPOSURE_RISE_SECONDS)` is nought in a float.
            static constexpr float sSettling = 1000.0f;

            /// The frames that settle the eye on `luminances`: a reset, which starts it at a day, and
            /// the same frame again over `sSettling`.
            static std::array<Metered, 2> settledOn(const std::vector<float>& luminances)
            {
                return { Metered{ .mLuminances = luminances },
                    Metered{ .mLuminances = luminances, .mElapsed = sSettling, .mReset = false } };
            }

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
                        pass.record(commands, frame, metered.mElapsed,
                            metered.mReset ? std::optional(metered.mStart) : std::nullopt, 1.0f);

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
        /// exposure itself overshot it by `(e + t) / (2 sqrt(e t))`. Back the other way, after `ln 2`
        /// of the falling one, the eye is at the same mean: one rate in stops, at two speeds.
        TEST_F(RtxExposurePassTest, theMeterLeavesOutTheBrightestTenthAndAdaptsInStops)
        {
            std::vector<float> flames = even(0.18f);
            for (std::size_t at = 0; at < flames.size() * 8 / 100; ++at)
                flames[at * 12 % flames.size()] = 100.0f;

            const float keyed = meterAll(settledOn(even(0.18f)));
            EXPECT_EQ(meterAll(settledOn(flames)), keyed) << "the brightest tenth moved the meter";

            const float dark = meterAll(settledOn(even(0.018f)));
            ASSERT_GT(dark, 2.0f * keyed) << "a frame ten times darker wants the eye open";
            ASSERT_LT(dark, Shaders::EXPOSURE_MAX);

            const float half = Shaders::EXPOSURE_RISE_SECONDS * std::numbers::ln2_v<float>;
            const std::array<Metered, 2> settled = settledOn(even(0.18f));
            const float adapted = meterAll(std::array{
                settled[0], settled[1], Metered{ .mLuminances = even(0.018f), .mElapsed = half, .mReset = false } });
            EXPECT_NEAR(adapted, std::sqrt(keyed * dark), std::sqrt(keyed * dark) * 1e-5f) << "half the gap in stops";

            const std::array<Metered, 2> opened = settledOn(even(0.018f));
            const float closed = meterAll(std::array{ opened[0], opened[1],
                Metered{ .mLuminances = even(0.18f),
                    .mElapsed = Shaders::EXPOSURE_FALL_SECONDS * std::numbers::ln2_v<float>,
                    .mReset = false } });
            EXPECT_NEAR(closed, std::sqrt(keyed * dark), std::sqrt(keyed * dark) * 1e-5f)
                << "half the gap in stops, closing";
        }

        /// **An eye with no past starts at a bright day and opens toward what it meters**, so a load
        /// goes from dark to normal and never from bright to normal; **and a frame with nothing lit
        /// leaves the eye where it stands.**
        ///
        /// A reset frame moves the eye no time, so it holds `EXPOSURE_DAY`, `(1/10)^0.75` = 0.1778,
        /// whatever the frame is: a room at a tenth of the key wants it open. Over `ln 2` of the
        /// rising time constant it opens half the gap in stops, to the geometric mean of the day and
        /// the room's own exposure. A black frame — every pixel under `EXPOSURE_BLACK`, a world still
        /// arriving — over any time leaves an eye settled on the key where it was, to the bit, where
        /// a target of one opened it by stops on the frames a load begins with.
        TEST_F(RtxExposurePassTest, anEyeWithNoPastStartsAtADayAndABlackFrameHoldsIt)
        {
            EXPECT_NEAR(
                Shaders::EXPOSURE_DAY, std::pow(1.0f / Shaders::DAYLIGHT_GAIN, Shaders::EXPOSURE_ADAPTATION), 1e-7f);

            const std::vector<float> room = even(0.018f);
            EXPECT_EQ(meterAll(std::array{ Metered{ .mLuminances = room } }), Shaders::EXPOSURE_DAY)
                << "a reset took the room's exposure outright";

            const float roomed = meterAll(settledOn(room));
            ASSERT_GT(roomed, 2.0f * Shaders::EXPOSURE_DAY) << "a room wants the eye open past a day";
            const float half = Shaders::EXPOSURE_RISE_SECONDS * std::numbers::ln2_v<float>;
            const float opening = meterAll(std::array{
                Metered{ .mLuminances = room }, Metered{ .mLuminances = room, .mElapsed = half, .mReset = false } });
            const float between = std::sqrt(Shaders::EXPOSURE_DAY * roomed);
            EXPECT_NEAR(opening, between, between * 1e-5f) << "half the gap from a day, in stops";

            const std::array<Metered, 2> keyed = settledOn(even(0.18f));
            const float settled = meterAll(keyed);
            EXPECT_EQ(meterAll(std::array{ keyed[0], keyed[1],
                          Metered{ .mLuminances = even(0.0f), .mElapsed = 1.0f, .mReset = false } }),
                settled)
                << "a black frame moved the eye";
            EXPECT_EQ(meterAll(std::array{ Metered{ .mLuminances = even(0.0f) } }), Shaders::EXPOSURE_DAY)
                << "a black frame with no past";

            // **A measured run's reset takes the frame's measurement outright** (`EyeStart::Settled`):
            // the room's own exposure on its first frame, where the game's holds the day.
            const float settledAtOnce
                = meterAll(std::array{ Metered{ .mLuminances = room, .mStart = EyeStart::Settled } });
            EXPECT_NEAR(settledAtOnce, roomed, roomed * 1e-5f) << "a settled start eased from a day";
            EXPECT_EQ(meterAll(std::array{ Metered{ .mLuminances = even(0.0f), .mStart = EyeStart::Settled } }),
                Shaders::EXPOSURE_DAY)
                << "a settled start on a black frame";
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
