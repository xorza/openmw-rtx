#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

#include <unistd.h>

#include <gtest/gtest.h>

#include <SDL.h>

#include <components/rtx/error.hpp>
#include <components/rtxgl/importedframe.hpp>
#include <components/rtxvulkan/commands.hpp>
#include <components/rtxvulkan/device.hpp>
#include <components/rtxvulkan/image.hpp>
#include <components/rtxvulkan/instance.hpp>
#include <components/rtxvulkan/physicaldevice.hpp>

#include "harness.hpp"

namespace Rtx
{
    namespace
    {
        /// Fills `image` with one colour and leaves it where a copy can read it.
        void fill(CommandPool& pool, const Image& image, const std::array<float, 4>& value)
        {
            pool.submitAndWait([&](VkCommandBuffer commands) {
                image.transition(commands, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT);

                VkClearColorValue colour{};
                std::memcpy(colour.float32, value.data(), sizeof(colour.float32));
                const VkImageSubresourceRange whole{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                vkCmdClearColorImage(
                    commands, image.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &colour, 1, &whole);

                image.transition(commands, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                    VK_ACCESS_2_MEMORY_READ_BIT);
            });
        }

        /// An image written on one device, read on another, through the descriptor between them.
        ///
        /// **The half of GL/Vulkan interop that can be tested without a GL context.** The in-game
        /// path does not present through a swapchain: it exports the traced frame's allocation and
        /// OpenGL imports it, because the character doll, the local map, the global map and video
        /// playback are all OSG render-to-texture users and a Vulkan window would mean reimplementing
        /// every one of them before the game was playable again (`docs/rtx/plan.md` §3).
        ///
        /// What the second Vulkan device stands in for is OpenGL. It is not the same importer, and
        /// it does not prove `glImportMemoryFdEXT` will be happy — but everything on this side of the
        /// descriptor is the same: the image created shareable so its layout is one both sides agree
        /// on rather than one the driver chose privately, the allocation exported, the size reported
        /// as the driver's padded one rather than the arithmetic one, and the descriptor's ownership
        /// handed over exactly once.
        ///
        /// **Two devices and not two images on one**, because a single device would alias the memory
        /// whether or not the export meant anything.
        TEST(RtxInteropTest, aFrameExportedOnOneDeviceIsTheFrameImportedOnAnother)
        {
            std::string reason;
            Testing::Harness* exporter = Testing::getHarness(reason);
            if (exporter == nullptr)
                GTEST_SKIP() << reason;

            Testing::Harness* importer = Testing::getUnvalidatedHarness(reason);
            if (importer == nullptr)
                GTEST_SKIP() << reason;

            constexpr std::uint32_t sWidth = 64;
            constexpr std::uint32_t sHeight = 48;
            constexpr VkFormat sFormat = VK_FORMAT_R8G8B8A8_UNORM;
            constexpr VkImageUsageFlags sUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

            const Image shared(
                *exporter->mDevice, sWidth, sHeight, sFormat, sUsage, "shared-frame", Sharing::Exportable);

            CommandPool writing(*exporter->mDevice);

            // **Exact eighth-bit fractions, and not 0.25, 0.5, 0.75.** Encoding a float to a unorm
            // is a round to nearest, and 0.5 lands exactly between 127 and 128 — a tie whose
            // direction the hardware picks, which measured 127 here. Asserting a tie-break would be
            // asserting this driver rather than the transfer. `n / 255` has no such question.
            //
            // Three different channels, because a grey would pass a check that read one of them
            // three times.
            fill(writing, shared, { 64.0f / 255.0f, 128.0f / 255.0f, 191.0f / 255.0f, 1.0f });

            const int descriptor = shared.exportMemory();
            ASSERT_GE(descriptor, 0) << "the export reported success and handed back no descriptor";

            const VkDeviceSize bytes = shared.getMemoryBytes();
            EXPECT_GE(bytes, VkDeviceSize{ sWidth } * sHeight * 4)
                << "the allocation is smaller than the pixels in it, which is not a padding";

            // From here the descriptor is the import's, whether it succeeds or throws.
            const Image imported(
                *importer->mDevice, sWidth, sHeight, sFormat, sUsage, "imported-frame", descriptor, bytes);

            CommandPool reading(*importer->mDevice);

            // **Undefined and not `GENERAL`.** The layout the other device left it in is that
            // device's business; what crosses the descriptor is the memory, and this one has to
            // claim the image for itself before it reads. Discarding is right because the contents
            // are already there — a layout transition out of `UNDEFINED` keeps the bytes.
            std::vector<std::uint8_t> pixels;
            reading.submitAndWait([&](VkCommandBuffer commands) {
                imported.transition(commands, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                    VK_ACCESS_2_MEMORY_READ_BIT);
            });
            imported.read(reading, VK_IMAGE_LAYOUT_GENERAL, pixels);

            ASSERT_EQ(pixels.size(), std::size_t{ sWidth } * sHeight * 4);

            for (std::size_t at = 0; at < pixels.size(); at += 4)
            {
                ASSERT_EQ(pixels[at], 64) << "red at " << at / 4;
                ASSERT_EQ(pixels[at + 1], 128) << "green at " << at / 4;
                ASSERT_EQ(pixels[at + 2], 191) << "blue at " << at / 4;
                ASSERT_EQ(pixels[at + 3], 255) << "alpha at " << at / 4;
            }

            for (const ValidationMessage& message : exporter->mInstance->getValidationLog()->getErrorsOnThisThread())
                ADD_FAILURE() << "validation error from the export: " << message.mText;
        }

        /// A hidden OpenGL window, for the half of interop the Vulkan-to-Vulkan test cannot reach.
        ///
        /// **4.5 core**, because the import is done with direct state access: `glCreateTextures`
        /// and `glTextureStorageMem2DEXT` rather than a bind. Hidden because nothing is drawn — what
        /// is being asked is whether the texture holds the right bytes, not whether it can be shown.
        class GlContext
        {
        public:
            GlContext()
            {
                if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
                {
                    mObstacle = std::string("no display: ") + SDL_GetError();
                    return;
                }

                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
                SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

                mWindow = SDL_CreateWindow("rtx interop", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 16, 16,
                    SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
                if (mWindow == nullptr)
                {
                    mObstacle = std::string("no OpenGL window: ") + SDL_GetError();
                    return;
                }

                mContext = SDL_GL_CreateContext(mWindow);
                if (mContext == nullptr)
                    mObstacle = std::string("no OpenGL 4.5 context: ") + SDL_GetError();
            }

            ~GlContext()
            {
                if (mContext != nullptr)
                    SDL_GL_DeleteContext(mContext);
                if (mWindow != nullptr)
                    SDL_DestroyWindow(mWindow);

                SDL_QuitSubSystem(SDL_INIT_VIDEO);
            }

            GlContext(const GlContext&) = delete;
            GlContext& operator=(const GlContext&) = delete;

            /// Why there is no context, or empty where there is one.
            const std::string& getObstacle() const { return mObstacle; }

        private:
            SDL_Window* mWindow = nullptr;
            SDL_GLContext mContext = nullptr;
            std::string mObstacle;
        };

        /// **The whole of the in-game path's handoff, end to end**: Vulkan writes a frame, exports
        /// its allocation, and OpenGL reads back the pixels that were written.
        ///
        /// This is what the second-Vulkan-device test above stands in for and cannot prove. It is
        /// also the check the project's own rule asks for — a rendering change verified by opening
        /// the game window is one nobody verifies — so the import has a component of its own rather
        /// than living where only the game can reach it.
        ///
        /// Skips where there is no display, which is the ordinary case over ssh and is where nearly
        /// all of this project's iteration happens.
        TEST(RtxInteropTest, openGlImportsTheFrameVulkanExported)
        {
            std::string reason;
            Testing::Harness* harness = Testing::getHarness(reason);
            if (harness == nullptr)
                GTEST_SKIP() << reason;

            const GlContext gl;
            if (!gl.getObstacle().empty())
                GTEST_SKIP() << gl.getObstacle();

            // **Before anything is imported, not after it looks wrong.** Two GPUs with the window on
            // one and the tracer on the other is not a case that fails loudly: the import succeeds
            // and the texture holds whatever was at that address.
            const std::array<std::uint8_t, VK_UUID_SIZE> vulkan = harness->mDevice->getPhysicalDevice().getUuid();
            RtxGl::DeviceUuid same{};
            std::copy(vulkan.begin(), vulkan.end(), same.begin());
            if (const std::string mismatch = RtxGl::findDeviceMismatch(same); !mismatch.empty())
                GTEST_SKIP() << mismatch;

            constexpr std::uint32_t sWidth = 64;
            constexpr std::uint32_t sHeight = 48;

            const Image shared(*harness->mDevice, sWidth, sHeight, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
                    | VK_IMAGE_USAGE_SAMPLED_BIT,
                "gl-shared-frame", Sharing::Exportable);

            CommandPool pool(*harness->mDevice);
            fill(pool, shared, { 64.0f / 255.0f, 128.0f / 255.0f, 191.0f / 255.0f, 1.0f });

            const RtxGl::ImportedFrame imported(shared.exportMemory(), shared.getMemoryBytes(), sWidth, sHeight);

            std::vector<std::uint8_t> pixels;
            imported.read(pixels);
            ASSERT_EQ(pixels.size(), std::size_t{ sWidth } * sHeight * 4);

            for (std::size_t at = 0; at < pixels.size(); at += 4)
            {
                ASSERT_EQ(pixels[at], 64) << "red at " << at / 4;
                ASSERT_EQ(pixels[at + 1], 128) << "green at " << at / 4;
                ASSERT_EQ(pixels[at + 2], 191) << "blue at " << at / 4;
                ASSERT_EQ(pixels[at + 3], 255) << "alpha at " << at / 4;
            }

            harness->mInstance->getValidationLog()->clear();
        }

        /// Exporting what was never made shareable is a caller's mistake, and the descriptor a
        /// second export hands back is a second descriptor.
        TEST(RtxInteropTest, everyExportIsADescriptorTheCallerHasToClose)
        {
            std::string reason;
            Testing::Harness* harness = Testing::getHarness(reason);
            if (harness == nullptr)
                GTEST_SKIP() << reason;

            const Image shared(*harness->mDevice, 8, 8, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "twice-exported", Sharing::Exportable);

            // **A new one every call.** `vkGetMemoryFdKHR` transfers ownership rather than lending,
            // so a caller that exported twice and closed once is leaking one — and one that closed
            // the same number twice is closing a descriptor something else has since been given.
            const int first = shared.exportMemory();
            const int second = shared.exportMemory();
            ASSERT_GE(first, 0);
            ASSERT_GE(second, 0);
            EXPECT_NE(first, second);

            EXPECT_EQ(::close(first), 0);
            EXPECT_EQ(::close(second), 0);

            harness->mInstance->getValidationLog()->clear();
        }
    }
}
