#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_video.h>
#include <volk.h>

#include <apps/components_tests/rtx/support/death.hpp>
#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <components/platform/process.hpp>
#include <components/rtx/renderer/verticalsync.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/instance.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/device/physicaldevice.hpp>
#include <components/rtxvulkan/device/validation.hpp>
#include <components/rtxvulkan/present/presenter.hpp>
#include <components/rtxvulkan/present/presenttarget.hpp>
#include <components/rtxvulkan/present/surface.hpp>

namespace Rtx
{
    namespace
    {
        /// SDL's video on the session's display, and a hidden window on it; why SDL started none,
        /// kept as it said it, before a later call says something else.
        struct HiddenWindow
        {
            HiddenWindow()
            {
                mStarted = SDL_InitSubSystem(SDL_INIT_VIDEO);
                if (mStarted)
                    mWindow = SDL_CreateWindow("presenter", 64, 32, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
                else
                    mWhyNot = SDL_GetError();
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
            std::string mWhyNot;
        };

        /// A device of its own on `window`, on an instance with the window system's extensions, which the shared
        /// harness's headless instance has none of, and the window's surface for a presenter to take. Pinned: the
        /// surface and the device hold the instance.
        struct PresentingDevice
        {
            explicit PresentingDevice(SDL_Window* window)
                : mInstance(std::make_unique<Instance>(
                      Testing::describeValidation(Testing::Validation::Validated), Surface::getInstanceExtensions()))
                , mSurface(std::make_unique<Surface>(*mInstance, window))
                , mDevice(std::make_unique<Device>(*mInstance,
                      PhysicalDevice::select(mInstance->getHandle(),
                          [&](VkPhysicalDevice physical, std::uint32_t family) {
                              return mSurface->supports(physical, family).value_or(false);
                          }),
                      Testing::getCensusShaders(), Testing::getPipelineCacheSpec(), DeviceReport{}))
            {
            }

            PresentingDevice(const PresentingDevice&) = delete;
            PresentingDevice& operator=(const PresentingDevice&) = delete;
            ~PresentingDevice() = default;

            std::unique_ptr<Instance> mInstance;
            std::unique_ptr<Surface> mSurface;
            std::unique_ptr<Device> mDevice;
        };

        /// A hidden window to present to, **on the session's display, and skipped without one**
        /// (`Platform::Process::showsADesktop`): NVIDIA's driver presents nothing to a `VK_EXT_headless_surface`
        /// surface, so SDL's offscreen video gives no swapchain to test on every card. A window SDL refuses on a
        /// display it did start is a failure. The device is the test's own to make: a death test makes it inside its
        /// statement, as a death test's child runs the test again from its start, which Windows cannot fork past, so a
        /// device made ahead of the statement is made twice.
        class RtxPresenterDeathTest : public ::testing::Test
        {
        protected:
            void SetUp() override
            {
                if (!mVideo.mStarted)
                    GTEST_SKIP() << "no display to open the test's window on: " << mVideo.mWhyNot;
                if (!Platform::Process::showsADesktop())
                    GTEST_SKIP() << "the session shows no desktop to present to: a service's, as SSH's is";
                ASSERT_NE(mVideo.mWindow, nullptr) << SDL_GetError();
            }

            HiddenWindow mVideo;
        };

        /// A presenting device on the hidden window, made ahead of the test.
        class RtxPresenterTest : public RtxPresenterDeathTest
        {
        protected:
            void SetUp() override
            {
                RtxPresenterDeathTest::SetUp();
                if (IsSkipped() || HasFatalFailure())
                    return;
                mPresenting.emplace(mVideo.mWindow);
            }

            void TearDown() override
            {
                if (!mPresenting.has_value())
                    return;
                mPresenting->mDevice->waitIdle();
                if (ValidationLog* log = mPresenting->mInstance->getValidationLog(); log != nullptr)
                {
                    std::vector<std::string> raised;
                    log->takeErrorsOnThisThread(raised);
                    for (const std::string& error : raised)
                        ADD_FAILURE() << "validation error: " << error;
                }
            }

            std::optional<PresentingDevice> mPresenting;
        };

        /// **A lost surface is made again from the window, and the frames go on**: a present after the rebuild leaves
        /// nothing to remake, and a loss after it is a loss like the first. The handle is not compared: a surface made
        /// after the old one went can take its address.
        TEST_F(RtxPresenterTest, aLostSurfaceIsMadeAgainAndTheFramesGoOn)
        {
            const VkExtent2D extent{ 64, 32 };
            PresentTarget target;
            target.resize(*mPresenting->mDevice, extent.width, extent.height);
            Presenter presenter(
                *mPresenting->mDevice, std::move(mPresenting->mSurface), mVideo.mWindow, VerticalSync::Off);
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
                EXPECT_FALSE(presenter.setVerticalSync(VerticalSync::On, [&] { drained = true; }));
                EXPECT_FALSE(drained);

                presenter.rebuild(extent);
                EXPECT_TRUE(presenter.setVerticalSync(VerticalSync::Off, [&] { drained = true; }));
                EXPECT_TRUE(drained);
                EXPECT_FALSE(presenter.wantsResize(extent));
                show();
                show();
                EXPECT_FALSE(presenter.wantsResize(extent)) << "a present on the new surface was refused";
            }
        }

        /// **A surface lost again before a frame showed on the one made after it ends the process**, where a rebuild
        /// each frame would hold the game in a stall.
        TEST_F(RtxPresenterDeathTest, aSurfaceLostAgainBeforeAFrameShowedEndsTheProcess)
        {
            Testing::expectDies(
                [&] {
                    PresentingDevice presenting(mVideo.mWindow);
                    Presenter presenter(
                        *presenting.mDevice, std::move(presenting.mSurface), mVideo.mWindow, VerticalSync::Off);
                    presenter.surfaceLost();
                    presenter.rebuild(VkExtent2D{ 64, 32 });
                    presenter.surfaceLost();
                },
                "the window's surface was lost again before a frame was shown on the one made after it");
        }

        /// **A suboptimal present rebuilds once an extent, and is shown as it is after that**: a
        /// driver that answers suboptimal on a fresh swapchain would rebuild on every frame. A new
        /// extent is a new chance, and the rebuild it asks is owed once more.
        TEST_F(RtxPresenterTest, aSuboptimalPresentRebuildsOnceAnExtent)
        {
            const VkExtent2D extent{ 64, 32 };
            const VkExtent2D wider{ 96, 32 };
            PresentTarget target;
            target.resize(*mPresenting->mDevice, extent.width, extent.height);
            Presenter presenter(
                *mPresenting->mDevice, std::move(mPresenting->mSurface), mVideo.mWindow, VerticalSync::Off);
            presenter.present(target.getShown(), PresentTarget::sResting, [](VkCommandBuffer) {});
            EXPECT_FALSE(presenter.wantsResize(extent));

            presenter.suboptimal();
            EXPECT_TRUE(presenter.wantsResize(extent)) << "the first suboptimal present asks for a rebuild";
            presenter.rebuild(extent);
            presenter.suboptimal();
            EXPECT_FALSE(presenter.wantsResize(extent)) << "a suboptimal present after the rebuild rebuilt again";
            presenter.suboptimal();
            EXPECT_FALSE(presenter.wantsResize(extent));

            presenter.rebuild(wider);
            presenter.suboptimal();
            EXPECT_TRUE(presenter.wantsResize(wider)) << "a new extent owes its own rebuild";
        }

        /// **Under FIFO a frame waits for the present two before it** (`Swapchain::awaitLatency`): each present
        /// carries the next id, the wait names one the swapchain was given, and a wait the hidden window never answers
        /// runs out once, a tenth of a second, and is not asked again. Validation holds the ids to the swapchain; the
        /// clock holds the waits to one patience each. A card that cannot wait for a present, as a low-end AMD
        /// integrated GPU under AMD's Windows driver cannot, waits for none, and its frames go on.
        TEST_F(RtxPresenterTest, aFrameUnderFifoWaitsForThePresentTwoBeforeIt)
        {
            const VkExtent2D extent{ 64, 32 };
            PresentTarget target;
            target.resize(*mPresenting->mDevice, extent.width, extent.height);
            Presenter presenter(
                *mPresenting->mDevice, std::move(mPresenting->mSurface), mVideo.mWindow, VerticalSync::On);

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
