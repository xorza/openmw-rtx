#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_video.h>
#include <volk.h>

#include <apps/components_tests/rtx/support/death.hpp>
#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/instance.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/device/physicaldevice.hpp>
#include <components/rtxvulkan/device/validation.hpp>
#include <components/rtxvulkan/present/presenter.hpp>
#include <components/rtxvulkan/present/presenttarget.hpp>
#include <components/rtxvulkan/present/surface.hpp>
#include <components/sdlutil/vsyncmode.hpp>

namespace Rtx
{
    namespace
    {
        /// SDL's video on the session's display, and a hidden window on it.
        struct HiddenWindow
        {
            HiddenWindow()
            {
                mStarted = SDL_InitSubSystem(SDL_INIT_VIDEO);
                if (mStarted)
                    mWindow = SDL_CreateWindow("presenter", 64, 32, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
            }

            ~HiddenWindow()
            {
                if (mWindow != nullptr)
                    SDL_DestroyWindow(mWindow);
                if (mStarted)
                    SDL_QuitSubSystem(SDL_INIT_VIDEO);
            }

            HiddenWindow(const HiddenWindow&) = delete;
            HiddenWindow& operator=(const HiddenWindow&) = delete;

            bool mStarted = false;
            SDL_Window* mWindow = nullptr;
        };

        /// A device of its own, on an instance with the window system's extensions, which the shared
        /// harness's headless instance has none of.
        ///
        /// **On the session's display, and a failure without one**, as the binary fails without a
        /// device: no driver here presents to a `VK_EXT_headless_surface` surface, so SDL's offscreen
        /// video gives no swapchain to test.
        class RtxPresenterTest : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                ASSERT_TRUE(mVideo.mStarted) << "no display to open the test's window on: " << SDL_GetError();
                ASSERT_NE(mVideo.mWindow, nullptr) << SDL_GetError();

                mInstance = std::make_unique<Instance>(
                    ValidationOptions{ .mLevel = ValidationLevel::Sync, .mAbortOnError = false },
                    Surface::getInstanceExtensions());
                mSurface = std::make_unique<Surface>(*mInstance, mVideo.mWindow);
                mDevice = std::make_unique<Device>(*mInstance,
                    PhysicalDevice::select(mInstance->getHandle(),
                        [&](VkPhysicalDevice physical, std::uint32_t family) {
                            return mSurface->supports(physical, family);
                        }),
                    Testing::getCensusShaders(), Testing::getPipelineCacheSpec(), PipelineStatistics::Skipped);
            }

            void TearDown() override
            {
                if (mDevice == nullptr)
                    return;
                mDevice->waitIdle();
                if (ValidationLog* log = mInstance->getValidationLog(); log != nullptr)
                {
                    std::vector<std::string> raised;
                    log->takeErrorsOnThisThread(raised);
                    for (const std::string& error : raised)
                        ADD_FAILURE() << "validation error: " << error;
                }
            }

            HiddenWindow mVideo;
            std::unique_ptr<Instance> mInstance;
            std::unique_ptr<Surface> mSurface;
            std::unique_ptr<Device> mDevice;
        };

        /// **A lost surface is made again from the window, and the frames go on**: a present after
        /// the rebuild leaves nothing to remake, and a loss after it is a loss like the first, which
        /// a present that did not show on the new surface would make the second. One lost again
        /// before any present showed on the new surface ends the process, where a rebuild each frame
        /// would hold the game in a stall. The handle is not compared: a surface made after the old
        /// one went can take its address.
        TEST_F(RtxPresenterTest, aLostSurfaceIsMadeAgainAndTheFramesGoOn)
        {
            const VkExtent2D extent{ 64, 32 };
            PresentTarget target;
            target.resize(*mDevice, extent.width, extent.height);
            Presenter presenter(*mDevice, *mSurface, mVideo.mWindow, SDLUtil::VSyncMode::Disabled);
            const auto show
                = [&] { presenter.present(target.getShown(), PresentTarget::sResting, [](VkCommandBuffer) {}); };

            show();
            show();
            EXPECT_FALSE(presenter.wantsResize(extent));

            for (int loss = 0; loss < 2; ++loss)
            {
                presenter.surfaceLost();
                EXPECT_TRUE(presenter.wantsResize(extent)) << "a lost surface asks for its rebuild";
                show();
                EXPECT_TRUE(presenter.wantsResize(extent)) << "a frame took nothing from a lost surface";

                // A setting changed while the surface is lost rebuilds nothing, and the rebuild that
                // makes the surface again takes it: setting it back is then a change of mode.
                bool drained = false;
                EXPECT_FALSE(presenter.setVerticalSync(SDLUtil::VSyncMode::Enabled, [&] { drained = true; }));
                EXPECT_FALSE(drained);

                presenter.rebuild(extent);
                EXPECT_TRUE(presenter.setVerticalSync(SDLUtil::VSyncMode::Disabled, [&] { drained = true; }));
                EXPECT_TRUE(drained);
                EXPECT_FALSE(presenter.wantsResize(extent));
                show();
                show();
                EXPECT_FALSE(presenter.wantsResize(extent)) << "a present on the new surface was refused";
            }

            presenter.surfaceLost();
            presenter.rebuild(extent);
            Testing::expectDies([&] { presenter.surfaceLost(); },
                "the window's surface was lost again before a frame was shown on the one made after it");
        }

        /// **Under FIFO a frame waits for the present two before it** (`Swapchain::awaitLatency`):
        /// each present carries the next id, the wait names one the swapchain was given, and a wait
        /// the hidden window never answers runs out once, a tenth of a second, and is not asked
        /// again. Validation is what holds the ids to the swapchain; the clock holds the waits to
        /// one patience each, and the frame's uses as the blit is handed them.
        TEST_F(RtxPresenterTest, aFrameUnderFifoWaitsForThePresentTwoBeforeIt)
        {
            ASSERT_TRUE(mDevice->hasPresentWait()) << "this card offers the wait, which the test is about";

            const VkExtent2D extent{ 64, 32 };
            PresentTarget target;
            target.resize(*mDevice, extent.width, extent.height);
            Presenter presenter(*mDevice, *mSurface, mVideo.mWindow, SDLUtil::VSyncMode::Enabled);

            constexpr int frames = 4;
            const auto began = std::chrono::steady_clock::now();
            // Every other frame left where the blit reads it, as the interface leaves it, which the
            // blit then takes with no barrier of its own: validation holds both to their uses.
            for (int frame = 0; frame < frames; ++frame)
            {
                presenter.awaitLatency();
                const Image& shown = target.getShown();
                if (frame % 2 == 0)
                    presenter.present(shown, PresentTarget::sResting, [](VkCommandBuffer) {});
                else
                    presenter.present(shown, Use::sBlitRead, [&](VkCommandBuffer commands) {
                        shown.transition(commands, PresentTarget::sResting, Use::sBlitRead);
                    });
            }
            presenter.awaitLatency();
            EXPECT_LT(std::chrono::steady_clock::now() - began, std::chrono::milliseconds(100) * (frames - 1))
                << "a wait was asked twice for one present";
        }
    }
}
