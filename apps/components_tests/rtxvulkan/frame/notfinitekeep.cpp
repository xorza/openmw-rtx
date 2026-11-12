#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <vulkan/vulkan_core.h>

#include <apps/components_tests/rtxvulkan/support/device/censusprobe.hpp>
#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <apps/components_tests/rtxvulkan/support/device/readback.hpp>
#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/renderer/denoiserimage.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/shaders/digest.h>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/frame/digestimages.hpp>
#include <components/rtxvulkan/frame/notfinitekeep.hpp>
#include <components/rtxvulkan/shaders/shared/fields.h>

namespace Rtx
{
    namespace
    {
        constexpr std::uint32_t sWidth = 24;
        constexpr std::uint32_t sHeight = 16;
        constexpr std::uint32_t sNaN = 0x7fc00000u;
        constexpr std::uint32_t sInfinity = 0x7f800000u;

        /// A texel's four floats as bits, each exact in a float: where it stands, which component, and
        /// which image, so no two texels of the test share one.
        std::array<std::uint32_t, 4> texelAt(std::uint32_t x, std::uint32_t y, std::uint32_t image)
        {
            std::array<std::uint32_t, 4> texel{};
            for (std::uint32_t component = 0; component < 4; ++component)
                texel[component] = std::bit_cast<std::uint32_t>(static_cast<float>(x) + 100.0f * static_cast<float>(y)
                    + 0.25f * static_cast<float>(component) + 10000.0f * static_cast<float>(image));
            return texel;
        }

        /// One texel of `image`'s window around `centre`, at `x`, `y` of the window.
        const std::array<std::uint32_t, 4>& windowAt(const NotFiniteImage& image, std::uint32_t x, std::uint32_t y)
        {
            return image.mTexels[y * NotFiniteImage::sSide + x];
        }

        /// Where an image's texels stand: at their pixels, as the channels but one, or in fields, as the
        /// denoiser's (`storedOf`).
        enum class Layout
        {
            Frame,
            Fields,
        };

        struct RtxNotFiniteKeepTest : Testing::DeviceTest
        {
            /// An image of floats over the frame, `texelAt`'s pattern for `image` with `poisoned` put in
            /// component one at each place, at its own pixels or in fields as frame nought lays them.
            Image floats(std::uint32_t image, std::span<const std::array<std::uint32_t, 3>> poisoned, Layout layout)
            {
                const std::uint32_t width = layout == Layout::Fields ? Shaders::storedWidth(sWidth) : sWidth;
                std::vector<std::uint32_t> words(std::size_t{ width } * sHeight * 4);
                const auto texelOf = [&](std::uint32_t x, std::uint32_t y) {
                    const Shaders::uvec2 at = layout == Layout::Fields
                        ? Shaders::storedOf(Shaders::uvec2(x, y), Shaders::fieldSwapOf(0), sWidth)
                        : Shaders::uvec2(x, y);
                    return (std::size_t{ at.y() } * width + at.x()) * 4;
                };
                for (std::uint32_t y = 0; y < sHeight; ++y)
                    for (std::uint32_t x = 0; x < sWidth; ++x)
                        for (std::uint32_t component = 0; component < 4; ++component)
                            words[texelOf(x, y) + component] = texelAt(x, y, image)[component];
                for (const auto& [x, y, bits] : poisoned)
                    words[texelOf(x, y) + 1] = bits;
                return upload(width, VK_FORMAT_R32G32B32A32_SFLOAT, words, "keep test floats");
            }

            /// An image of words over the frame in fields, as frame nought lays them, every word a NaN's
            /// bits plus where it stands.
            Image words()
            {
                const std::uint32_t stored = Shaders::storedWidth(sWidth);
                std::vector<std::uint32_t> values(std::size_t{ stored } * sHeight);
                for (std::uint32_t y = 0; y < sHeight; ++y)
                    for (std::uint32_t x = 0; x < sWidth; ++x)
                    {
                        const Shaders::uvec2 at
                            = Shaders::storedOf(Shaders::uvec2(x, y), Shaders::fieldSwapOf(0), sWidth);
                        values[std::size_t{ at.y() } * stored + at.x()] = sNaN + x + 100 * y;
                    }
                return upload(stored, VK_FORMAT_R32_UINT, values, "keep test words");
            }

            Image upload(
                std::uint32_t width, VkFormat format, const std::vector<std::uint32_t>& words, std::string_view name)
            {
                Image image = Testing::makeTestImage(getDevice(), VkExtent2D{ width, sHeight }, format, name);
                std::array regions{ wholeLevel(0, 0, VkExtent3D{ width, sHeight, 1 }) };
                Batch batch(getPool());
                uploadImage(batch, image, std::as_bytes(std::span(words)), regions);
                image.transition(batch.getCommands(), Use::sTextureSample, Use::sComputeRead);
                batch.flush();
                return image;
            }

            /// Records the keep of `images` as frame `frame`, and takes what it kept.
            std::optional<NotFiniteFrame> keep(NotFiniteKeep& keep, const DigestImages& images, std::uint64_t frame)
            {
                getPool().submitAndWait(
                    [&](const VkCommandBuffer commands) { keep.record(commands, images, frame, 0); });
                return keep.take();
            }
        };

        /// **The census decides, and the frame's order picks the window**: nothing is kept of a frame whose
        /// census counted nought, however its images read; a frame whose census counted one keeps every image
        /// the frame had, each with its first texel not finite, the topmost and then the leftmost, and the
        /// windows stand around the first image's in the frame's order — the channels', the denoiser's, the
        /// composed frame last — and not around the topmost of them all. An image of words is kept and never
        /// searched, and a window past the frame's edge holds nought. Taking it rearms the keep.
        TEST_F(RtxNotFiniteKeepTest, keepsTheFramesImagesAroundTheFirstImagesFirstTexelNotFinite)
        {
            const NotFiniteCensus* const census = getDevice().getCensus();
            ASSERT_NE(census, nullptr) << "the test device's shaders count";
            const Testing::CensusProbe counted(*census);

            constexpr std::array<std::array<std::uint32_t, 3>, 1> indirectPoison{ { { 20, 9, sNaN } } };
            constexpr std::array<std::array<std::uint32_t, 3>, 3> albedoPoison{ { { 3, 12, sNaN }, { 7, 2, sInfinity },
                { 1, 2, sNaN } } };
            constexpr std::array<std::array<std::uint32_t, 3>, 1> composedPoison{ { { 0, 0, sNaN } } };
            constexpr std::array<std::array<std::uint32_t, 3>, 1> momentsPoison{ { { 5, 3, sNaN } } };
            const Image composed = floats(0, composedPoison, Layout::Frame);
            const Image indirect = floats(1, indirectPoison, Layout::Frame);
            const Image albedo = floats(2, albedoPoison, Layout::Frame);
            const Image surface = floats(3, {}, Layout::Frame);
            const Image moments = floats(4, momentsPoison, Layout::Fields);
            const Image fast = words();

            std::array<const Image*, Shaders::DIGEST_IMAGES> channels{};
            channels[indexOf(Channel::Direct)] = &composed;
            channels[indexOf(Channel::Indirect)] = &indirect;
            channels[indexOf(Channel::Albedo)] = &albedo;
            channels[indexOf(Channel::Surface)] = &surface;
            std::array<const Image*, sDenoiserImageCount> denoiser{};
            denoiser[static_cast<std::size_t>(DenoiserImage::AccumulateMoments)] = &moments;
            denoiser[static_cast<std::size_t>(DenoiserImage::AccumulateFast)] = &fast;
            const DigestImages images(channels, 0, denoiser, VkExtent2D{ sWidth, sHeight });

            NotFiniteKeep kept(getDevice());
            EXPECT_EQ(keep(kept, images, 6), std::nullopt) << "a census of nought keeps nothing";

            counted.count("atrous.comp", 5);
            const std::optional<NotFiniteFrame> frame = keep(kept, images, 7);
            ASSERT_TRUE(frame.has_value());
            EXPECT_EQ(frame->mFrame, 7u);
            EXPECT_EQ(frame->mWidth, sWidth);
            EXPECT_EQ(frame->mHeight, sHeight);
            EXPECT_EQ(frame->mCentre, (NotFiniteAt{ .mX = 20, .mY = 9 }))
                << "the indirect channel's, the first image in the frame's order, and not the composed frame's";

            ASSERT_EQ(frame->mImages.size(), 6u) << "the images the frame had and no other";
            const NotFiniteImage& keptIndirect = frame->mImages[0];
            const NotFiniteImage& keptAlbedo = frame->mImages[1];
            const NotFiniteImage& keptSurface = frame->mImages[2];
            const NotFiniteImage& keptMoments = frame->mImages[3];
            const NotFiniteImage& keptFast = frame->mImages[4];
            const NotFiniteImage& keptComposed = frame->mImages[5];
            EXPECT_EQ(keptIndirect.mName, "g-indirect");
            EXPECT_EQ(keptAlbedo.mName, "g-albedo");
            EXPECT_EQ(keptSurface.mName, "g-surface");
            EXPECT_EQ(keptMoments.mName, "accumulate-moments");
            EXPECT_EQ(keptFast.mName, "accumulate-fast");
            EXPECT_EQ(keptComposed.mName, "composed");

            EXPECT_EQ(keptIndirect.mFirst, (NotFiniteAt{ .mX = 20, .mY = 9 }));
            EXPECT_EQ(keptAlbedo.mFirst, (NotFiniteAt{ .mX = 1, .mY = 2 }))
                << "the topmost row's, and the leftmost in it of a NaN and an infinity";
            EXPECT_EQ(keptSurface.mFirst, std::nullopt);
            EXPECT_EQ(keptMoments.mFirst, (NotFiniteAt{ .mX = 5, .mY = 3 })) << "found through the fields";
            EXPECT_EQ(keptFast.mFirst, std::nullopt) << "words are never searched, NaN's bits or not";
            EXPECT_EQ(keptComposed.mFirst, (NotFiniteAt{ .mX = 0, .mY = 0 }));

            EXPECT_FALSE(keptIndirect.mWords);
            EXPECT_TRUE(keptFast.mWords);

            // The window's centre, at 4, 4 of it, is the texel at 20, 9; 0, 0 of it the one at 16, 5; and
            // 8, 0 of it would be at 24, 5, a column past the frame.
            const std::array<std::uint32_t, 4> centre{ texelAt(20, 9, 1)[0], sNaN, texelAt(20, 9, 1)[2],
                texelAt(20, 9, 1)[3] };
            EXPECT_EQ(windowAt(keptIndirect, 4, 4), centre);
            EXPECT_EQ(windowAt(keptIndirect, 0, 0), texelAt(16, 5, 1));
            EXPECT_EQ(windowAt(keptIndirect, 8, 0), (std::array<std::uint32_t, 4>{}));
            EXPECT_EQ(windowAt(keptSurface, 4, 4), texelAt(20, 9, 3)) << "every image's window stands there";
            EXPECT_EQ(windowAt(keptMoments, 3, 2), texelAt(19, 7, 4)) << "read through the fields";
            EXPECT_EQ(windowAt(keptComposed, 7, 8), texelAt(23, 13, 0));
            EXPECT_EQ(windowAt(keptFast, 4, 4), (std::array<std::uint32_t, 4>{ sNaN + 920, 0, 0, 1 }))
                << "read through the fields, with what a load of one word fills the rest with";

            EXPECT_EQ(kept.take(), std::nullopt) << "taken once";
            const std::optional<NotFiniteFrame> again = keep(kept, images, 8);
            ASSERT_TRUE(again.has_value()) << "the next frame that stores one is kept after it";
            EXPECT_EQ(again->mFrame, 8u);

            counted.clear();
        }
    }
}
