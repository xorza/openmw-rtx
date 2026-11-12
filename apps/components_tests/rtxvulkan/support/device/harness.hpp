#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/renderer/shaderset.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/instance.hpp>
#include <components/rtxvulkan/device/pipelinecache.hpp>

#include "rendererprobe.hpp"

namespace Rtx
{
    class VulkanRenderer;
}

namespace Rtx::Testing
{
    /// Instance and device for the whole test binary: bring-up costs a good fraction of a second and nothing mutates
    /// it, so paying once is the difference between a suite that gets run and one that does not. Both are held by
    /// pointer so the instance can be built before there is a device to pair it with, and so it is destroyed last.
    struct Harness
    {
        std::unique_ptr<Instance> mInstance;
        std::unique_ptr<Device> mDevice;

        /// What the layers raised while the two were made, taken before any test's own drain,
        /// which reads whatever is on the log as a previous test's and drops it.
        std::vector<std::string> mMadeWith;
    };

    /// Something built once for the whole binary.
    ///
    /// **Held by whoever declares one rather than in a function-local static**, so that a gtest environment can close
    /// it while the process is still whole (see `DeviceEnvironment` in `harness.cpp`).
    template <class T>
    struct Once
    {
        std::unique_ptr<T> mValue;

        template <class Build>
        T& get(Build&& build)
        {
            if (mValue == nullptr)
                mValue = build();

            return *mValue;
        }

        void release() { mValue.reset(); }
    };

    /// The device every test that drives Vulkan directly shares.
    ///
    /// **Always there by the time a test asks**: `DeviceEnvironment` asks first and fails the binary where there is
    /// none, and a device that does not meet the requirements throws out of `PhysicalDevice::select`, which fails it
    /// the same way.
    Harness& getHarness();

    /// The same, with no validation layers loaded.
    ///
    /// **Two things in this suite run unvalidated, and each is measured rather than asserted.** `getAllocationCount`
    /// replaces the global `operator new`, so it cannot tell a layer's allocation from the renderer's: with the layers
    /// loaded, `RtxFrameCostTest` measures 32,352 to 49,152 allocations over its 32 frames against a budget of nought,
    /// which is a deleted test and not a stricter one. And `RtxVisibilityKernelsTest` watches seven kernels compile,
    /// which the layers held at 1.2 to 1.4 s against 0.03 without them. A settled mean's 320 frames
    /// (`getUnvalidatedRenderer`) are the third. Everything else is validated, so this device is only built if
    /// something asks.
    Harness& getUnvalidatedHarness();

    /// A device of the caller's own, closed when it lets go: what a test about a device coming and
    /// going holds, since the two above live for the whole binary.
    std::unique_ptr<Harness> makeHarness(bool validation);

    /// The shaders every device and renderer of the suites reads: the census's, as every run of the
    /// harness but a measured one reads, so a kernel a test dispatches is the module `check` runs
    /// and every store it makes is counted. With the probes, which only the tests run. Read once, for
    /// the binary's whole run.
    const ShaderSet& getCensusShaders();

    /// Where a device this suite makes keeps what it compiled, and what that cache is keyed on.
    ///
    /// **The suite's own, in its build folder**, kept across runs so a run compiles what changed: the player's cache
    /// under the home folder made what a test compiled, and how long it took, depend on what a game on the machine had
    /// left there. `PipelineCache` keys the file on the driver and on the shaders. **None in a death test's child**,
    /// which read the suite's 118 MB file and hashed it, a fifth of its start, for a statement that compiles nothing.
    ///
    /// **And the driver's own cache stays on, unlike a measuring process's** (`RtxRenderer` says what a pipeline out
    /// of a cache draws differently): a suite holds its pictures against values it derived, to a tolerance that covers
    /// what two compiles of one shader round apart, and compiling in every process is a suite four times as long for a
    /// promise no test here makes.
    PipelineCacheSpec getPipelineCacheSpec();

    /// How every renderer in this suite is built, apart from the extent and the upscaler a caller sets for itself.
    ///
    /// **One place, so that a test standing up its own renderer cannot end up validated less than the shared one.**
    /// The layers are two switches and a caller that remembered one of them would have a renderer half validated and
    /// no way to tell.
    ///
    /// @param validation off only for `getUnvalidatedRenderer`, which says why.
    RendererOptions describeRenderer(std::uint32_t width, std::uint32_t height, bool validation = true);

    /// Holds until every kernel `renderer` started making as it was made is made, and throws what
    /// making one threw. Before its validation errors are taken for the renderer's making: a kernel
    /// is made on a thread of its own after the constructor returns, and what the layers raise
    /// meanwhile is filed under whoever made the renderer.
    void awaitKernels(Renderer& renderer);

    /// The renderer the pixel tests trace through, built once for the binary on the first ask. Throws where it cannot
    /// be built, which fails the test that asked rather than skipping it.
    ///
    /// **What makes these tests an acceptance suite for any backend.** They assert hand-computed radiances, mip levels
    /// and transmittances, none of which is a statement about an API.
    ///
    /// **One for the binary, and a second is what a slow test is made of.** Measured with the on-disk pipeline cache
    /// warm and the layers loaded: building it and waiting out every kernel the trace can need costs three seconds,
    /// and every `resize`, `setScene`, `renderFrame` and `readPixels` after that costs one to fifteen milliseconds. A
    /// test that upscales sets the mode on this one, which `RendererTest::SetUp` puts back. Made before any test runs,
    /// by the environment that makes the device.
    VulkanRenderer& getRenderer();

    /// What the layers raised while `getRenderer`'s renderer was made, for the reason
    /// `Harness::mMadeWith` gives.
    const std::vector<std::string>& getRendererMadeWith();

    /// The same, with no validation layers loaded, for the test that counts allocations and the one whose claim needs
    /// more frames than the layers leave it time for.
    ///
    /// **A second renderer, for the reason `getUnvalidatedHarness` gives.** The layers allocate and
    /// `getAllocationCount` replaces the global `operator new`, so a frame measured through the shared renderer
    /// reports hundreds of allocations that no change to this code could remove. And they cost a frame three times its
    /// own on a low-end AMD integrated GPU, where `theRingLeavesASettledMeanItsLight`'s 320 frames took past what a
    /// test may. Built only if something asks.
    VulkanRenderer& getUnvalidatedRenderer();

    /// The base of a test that drives Vulkan directly.
    ///
    /// **The validation errors are drained before the test and reported after it**, the way `RendererTest` does, so
    /// whatever a previous test left behind is not this one's to report. **And the census's stores no frame reported
    /// are reported after it**, as `RendererTest` reports them.
    class DeviceTest : public ::testing::Test
    {
    protected:
        /// **Validated unless a derived fixture says otherwise**, which only a test that counts
        /// allocations or watches kernels the suite already checked wants: `getUnvalidatedHarness`
        /// says what the layers cost each.
        explicit DeviceTest(bool validation = true);

        void SetUp() override;

        void TearDown() override;

        Device& getDevice() const { return *mHarness.mDevice; }

        /// The device's own pool.
        CommandPool& getPool() const;

        Harness& mHarness;

    private:
        void takeRaised();

        std::vector<std::string> mRaised;
    };

    /// The base of a test that renders.
    ///
    /// **The validation errors are drained before the test and reported after it**, so a hazard the layers caught
    /// fails the test that caused it. **And every store the census counted is reported after it**: those of the test's
    /// frames, and those no frame reported, a picture's above all. A test that stores one on purpose takes them first
    /// (`RendererProbe::takeCountedStores`) and checks what it took. Not drained before it as well: every test that
    /// draws on the shared devices stands on one of the two fixtures, so a test starts with nothing left, and a death
    /// test's child, which ends before `TearDown`, submits nothing for the census. **And the renderer's histories and
    /// what a test told it are reset before it** (`SetUp`), so it draws nothing a previous test's frames or settings
    /// left either.
    class RendererTest : public ::testing::Test
    {
    protected:
        /// On the renderer the binary shares (`getRenderer`).
        RendererTest();

        /// On `renderer`, a fixture's own that outlives every test of it, reset before each test to what
        /// `made` says it was made with.
        RendererTest(VulkanRenderer& renderer, RunProfile made);

        void SetUp() override;

        void TearDown() override;

        /// Drops whatever `renderer` is holding, so a test starts with nothing against it.
        ///
        /// **For a fixture with a renderer of its own**: each one owns its instance and so its own
        /// log, and `mRenderer` is already done here.
        void forgetErrors(VulkanRenderer& renderer);

        /// Reports what `renderer` raised since `forgetErrors`, each failure headed by `what`.
        void reportErrors(VulkanRenderer& renderer, std::string_view what);

        VulkanRenderer& mRenderer;
        RendererProbe mProbe;

    private:
        RunProfile mMade;
        std::vector<std::string> mErrors;
    };
}
