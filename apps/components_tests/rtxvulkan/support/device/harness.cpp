#include "harness.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include <volk.h>

#include <apps/components_tests/rtx/support/bound.hpp>
#include <apps/components_tests/rtx/support/death.hpp>
#include <apps/components_tests/rtxvulkan/support/testinstance.hpp>
#include <components/misc/result.hpp>
#include <components/rtx/common/jobprogress.hpp>
#include <components/rtx/environment/wavespectrum.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtxvulkan/device/instance.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/notfinitecensus.hpp>
#include <components/rtxvulkan/device/physicaldevice.hpp>
#include <components/rtxvulkan/device/validation.hpp>
#include <components/rtxvulkan/spirv/shaderfolder.hpp>
#include <components/rtxvulkan/vulkanrenderer.hpp>

namespace Rtx::Testing
{
    namespace
    {
        /// Keyed on validation, which is the only axis any of these vary along.
        Once<Harness>& harnessCache(Validation validation)
        {
            static Once<Harness> sValidated;
            static Once<Harness> sPlain;
            return validation == Validation::Validated ? sValidated : sPlain;
        }

        /// Keyed on validation, the way the devices are and for the same reason.
        Once<VulkanRenderer>& rendererCache(Validation validation)
        {
            static Once<VulkanRenderer> sValidated;
            static Once<VulkanRenderer> sPlain;
            return validation == Validation::Validated ? sValidated : sPlain;
        }

        /// What `device`'s census counted since a frame last took it, which it then holds no more:
        /// the stores no frame's report carries, as a picture's or a pass a test drove itself. Taken
        /// on the queue behind everything before it, a picture's deferred batch among them, and
        /// nothing where the device does not count.
        NotFinite takeUncountedStores(const Device& device)
        {
            NotFinite stores;
            const NotFiniteCensus* const census = device.getCensus();
            if (census == nullptr)
                return stores;

            const Buffer copied = Buffer::readBack(
                device, NotFiniteCensus::sBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, "uncounted not-finite stores");
            device.getPool().submitAndWait([&](const VkCommandBuffer commands) {
                census->record(commands, copied);
                copied.orderForHostRead(commands);
            });
            census->readInto(copied, stores);
            return stores;
        }

        void reportStores(const NotFinite& stores, std::string_view unread)
        {
            for (const NotFiniteStores& kernel : stores.kernels())
                ADD_FAILURE() << kernel.mStores << " stores not finite in " << kernel.mKernel << ' ' << unread;
        }
    }

    std::unique_ptr<Harness> makeHarness(Validation validation)
    {
        // Tests provoke errors deliberately and assert on them; aborting would take the suite down with the first one.
        // Synchronization validation is **the same switch `describeRenderer` sets, for the same reason**: a test that
        // drives Vulkan directly supplies its own ordering with a submit and a wait, so a missing barrier shows as
        // nothing at all, and a suite validated one way through the renderer and another beside it answers a different
        // question in each file.
        const ValidationOptions options{
            .mLevel = validation == Validation::Validated ? ValidationLevel::Sync : ValidationLevel::Off,
            .mAbortOnError = false,
        };

        Result<std::unique_ptr<Instance>, std::string> instance = makeInstance(options);
        if (!instance.isOk())
            throw std::runtime_error(instance.error());

        auto harness = std::make_unique<Harness>();
        harness->mInstance = std::move(instance.value());

        std::uint32_t count = 0;
        if (vkEnumeratePhysicalDevices(harness->mInstance->getHandle(), &count, nullptr) != VK_SUCCESS || count == 0)
            throw std::runtime_error("no Vulkan device is installed");

        harness->mDevice = std::make_unique<Device>(*harness->mInstance,
            PhysicalDevice::select(harness->mInstance->getHandle(), {}), getCensusShaders(), getPipelineCacheSpec(),
            DeviceReport{ .mStatistics = PipelineStatistics::Captured, .mMarkers = true, .mCheckpoints = true });
        if (ValidationLog* log = harness->mInstance->getValidationLog(); log != nullptr)
            log->takeErrorsOnThisThread(harness->mMadeWith);

        return harness;
    }

    namespace
    {
        /// **The flag reaches the cache and the build together**, so the two cannot come apart: a
        /// device built unvalidated and filed under the validated key would be handed to every test
        /// in the suite.
        Harness& cachedHarness(Validation validation)
        {
            return harnessCache(validation).get([validation] { return makeHarness(validation); });
        }

        /// What the layers raised while the validated renderer was made: the other loads none.
        std::vector<std::string>& rendererMadeWith()
        {
            static std::vector<std::string> sMadeWith;
            return sMadeWith;
        }

        std::unique_ptr<VulkanRenderer> buildRenderer(Validation validation)
        {
            // Every test resizes to what it needs; one texel is only what the first target costs.
            auto renderer = std::make_unique<VulkanRenderer>(describeRenderer(1, 1, validation));
            awaitKernels(*renderer);
            if (validation == Validation::Validated)
                RendererProbe(*renderer).takeValidationErrors(rendererMadeWith());
            return renderer;
        }

        VulkanRenderer& cachedRenderer(Validation validation)
        {
            return rendererCache(validation).get([validation] { return buildRenderer(validation); });
        }

        /// What holding a device for the run costs the rest of the binary: every device closed after the last test and
        /// before `main` returns.
        ///
        /// **A fork of a process holding a device runs to seconds**: its mappings are copied and the driver's fork
        /// handlers run. Measured at four to eight seconds a death test once the pixel suite had built a renderer,
        /// against a fifth of a second for the `threadsafe` re-exec, which `main` sets for every death test.
        ///
        /// **Two Vulkan devices destroyed after `main` has returned abort inside the validation layer**, with no
        /// message. One pair survives static destruction and a second does not, and this binary keeps up to four: a
        /// raw device and a `Renderer`, each validated and unvalidated. Closing them here is the fix.
        ///
        /// **No device is a failed run, not an empty one**: a skip per test is honest per test and a green run of
        /// nothing per suite, so the first thing this binary does is ask for the device. **Both renderers are built
        /// beside it, before any test is timed**: three and a half seconds of the validated one's pipelines even out of
        /// a warm driver cache, and the unvalidated one's that every `RtxSettlingTest` draws through, would otherwise
        /// be charged to whichever test asked first, and one that cannot be built fails the binary as a missing device
        /// does.
        ///
        /// **Not in a death test's child**, which runs this again for one statement that aborts and needs the device
        /// at most: `inDeathChild` says how it knows.
        class DeviceEnvironment : public ::testing::Environment
        {
            void SetUp() override
            {
                try
                {
                    getHarness();
                }
                catch (const std::exception& obstacle)
                {
                    FAIL() << "rtx-gpu-tests needs a device and this machine has none: " << obstacle.what();
                }

                if (Testing::inDeathChild())
                    return;

                try
                {
                    getRenderer();
                    getUnvalidatedRenderer();
                }
                catch (const std::exception& obstacle)
                {
                    FAIL() << "rtx-gpu-tests cannot make its renderers: " << obstacle.what();
                }
            }

            void TearDown() override
            {
                // The renderer before the raw devices, which is the order they were built in; neither
                // depends on the other.
                for (const Validation validation : { Validation::Validated, Validation::Unvalidated })
                    rendererCache(validation).release();
                for (const Validation validation : { Validation::Validated, Validation::Unvalidated })
                    harnessCache(validation).release();
            }
        };

        // Before `main`, because gtest only tears down environments registered before the run starts.
        [[maybe_unused]] const bool sRegistered = [] {
            ::testing::AddGlobalTestEnvironment(new DeviceEnvironment);
            return true;
        }();
    }

    Harness& getHarness()
    {
        return cachedHarness(Validation::Validated);
    }

    Harness& getUnvalidatedHarness()
    {
        return cachedHarness(Validation::Unvalidated);
    }

    const ShaderSet& getCensusShaders()
    {
        static const ShaderFolder sCensus(std::filesystem::path(OPENMW_RTX_CENSUS_SHADER_DIR), true);
        return sCensus.getSet();
    }

    const ShaderSet& getShippedShaders()
    {
        static const ShaderFolder sShipped(std::filesystem::path(OPENMW_RTX_SHADER_DIR), false);
        return sShipped.getSet();
    }

    PipelineCacheSpec getPipelineCacheSpec()
    {
        if (inDeathChild())
            return PipelineCacheSpec{};
        return PipelineCacheSpec{ .mDirectory = std::filesystem::path(OPENMW_RTX_TEST_PIPELINE_CACHE_DIR) };
    }

    RendererOptions describeRenderer(std::uint32_t width, std::uint32_t height, Validation validation)
    {
        RendererOptions options;
        options.mShaders = getCensusShaders();
        options.mCacheDirectory = getPipelineCacheSpec().mDirectory;
        options.mWidth = width;
        options.mHeight = height;
        // **Synchronization validation wherever the layers are, because a missing barrier is what
        // this suite is worst at seeing.** Every test here submits and waits, so the ordering a
        // frame relies on is supplied by the harness rather than by the code under test, and a
        // hazard shows as nothing at all. It costs as much as the core checks it runs beside
        // (`getUnvalidatedRenderer` has the numbers).
        options.mRun.mValidation.mLevel
            = validation == Validation::Validated ? ValidationLevel::Sync : ValidationLevel::Off;
        // Tests provoke errors deliberately and assert on them; aborting would take the suite down
        // with the first one.
        options.mRun.mValidation.mAbortOnError = false;
        // One, because a measured exposure makes every pixel depend on the whole frame's histogram,
        // and a test hand-computes a pixel. A test of the eye asks per frame (`FrameOptions`).
        options.mRun.mProfile.mExposure = FixedExposure{ 1.0f };
        // And no dither, for the same reason: a test reads the curve's bytes. A test of the dither
        // asks for it.
        options.mRun.mProfile.mDither = false;

        return options;
    }

    void awaitKernels(Renderer& renderer)
    {
        const WaitBound bound(sColdPatience);
        for (JobProgress progress = renderer.awaitKernels(KernelSet::Every, std::chrono::milliseconds(100));
            !progress.isDone(); progress = renderer.awaitKernels(KernelSet::Every, std::chrono::milliseconds(100)))
            if (bound.passed())
                throw std::runtime_error(std::format(
                    "the renderer's kernels were {} of {} made after five minutes", progress.mMade, progress.mCount));
    }

    VulkanRenderer& getRenderer()
    {
        return cachedRenderer(Validation::Validated);
    }

    const std::vector<std::string>& getRendererMadeWith()
    {
        return rendererMadeWith();
    }

    VulkanRenderer& getUnvalidatedRenderer()
    {
        return cachedRenderer(Validation::Unvalidated);
    }

    DeviceTest::DeviceTest(Validation validation)
        : mHarness(cachedHarness(validation))
    {
    }

    void DeviceTest::SetUp()
    {
        // **Taken and then dropped**, because `takeErrorsOnThisThread` appends where a renderer's
        // own `takeValidationErrors` clears first: the log has to be emptied even though nothing
        // reads what comes off it here.
        takeRaised();
        mRaised.clear();
    }

    void DeviceTest::TearDown()
    {
        reportStores(takeUncountedStores(getDevice()), "that no frame of the test reported");
        takeRaised();
        for (const std::string& error : mRaised)
            ADD_FAILURE() << "validation error: " << error;
    }

    void DeviceTest::takeRaised()
    {
        // Nothing at all where the layers are not loaded, which is the unvalidated device
        // `getUnvalidatedHarness` says why there is.
        if (ValidationLog* log = mHarness.mInstance->getValidationLog(); log != nullptr)
            log->takeErrorsOnThisThread(mRaised);
    }

    CommandPool& DeviceTest::getPool() const
    {
        return getDevice().getPool();
    }

    RendererTest::RendererTest(Validation validation)
        : RendererTest(cachedRenderer(validation), describeRenderer(1, 1, validation).mRun)
    {
    }

    RendererTest::RendererTest(VulkanRenderer& renderer, RunProfile made)
        : mRenderer(renderer)
        , mProbe(renderer)
        , mMade(std::move(made))
    {
    }

    void RendererTest::SetUp()
    {
        forgetErrors(mRenderer);

        // **And nothing a previous test's frames left**, for the same reason: what the renderer carries from one frame
        // to the next (the denoiser's and the air's histories, the exposure, the ripples on the water) is state a test
        // did not draw, and a footfall one test presses into the water bends the next test's still sea. A new world
        // costs every history, as it does the game.
        mRenderer.setScene(Rtx::SceneSlot::world(), SceneDesc{}, {});

        // And none of what a previous test told the renderer: the modes its reserve keeps room for, its gamma, its
        // upscaler, its filtering, its bounces and its sea, as the renderer was made with them, so a test that failed
        // before it put one back leaks nothing into the rest of the shard. The fog's field is put back by the one test
        // that states another, under a guard, since a field told drains the frames.
        const RenderProfile& made = mMade.mProfile;
        mProbe.setReservedUpscales(mMade.mReservedUpscales);
        mRenderer.setGamma(made.mGamma);
        mRenderer.setUpscale(made.mUpscale);
        mRenderer.setAnisotropy(made.mAnisotropy);
        mRenderer.setIndirectLighting(made.mReconstruction.mSampling.mIndirect);
        mRenderer.setBounceRays(made.mReconstruction.mSampling.mBounceRays);
        mProbe.setSea(SeaState{});
    }

    void RendererTest::TearDown()
    {
        // The frames' first, which wait the frames out: the census read after them is then what
        // came after every frame.
        reportStores(mProbe.takeCountedStores(), "in frames of the test");
        reportStores(takeUncountedStores(mProbe.getDevice()), "that no frame of the test reported");
        reportErrors(mRenderer, "validation error");

        // What the renderer kept of a frame that stored one, so the next test's keep starts empty: a test that
        // stored one on purpose has said what it asks of the stores.
        mRenderer.takeNotFiniteFrame();
    }

    void RendererTest::forgetErrors(VulkanRenderer& renderer)
    {
        RendererProbe(renderer).takeValidationErrors(mErrors);
    }

    void RendererTest::reportErrors(VulkanRenderer& renderer, std::string_view what)
    {
        RendererProbe(renderer).takeValidationErrors(mErrors);
        for (const std::string& error : mErrors)
            ADD_FAILURE() << what << ": " << error;
    }
}
