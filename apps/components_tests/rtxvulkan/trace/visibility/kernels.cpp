#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtx/common/jobprogress.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/instance.hpp>
#include <components/rtxvulkan/device/physicaldevice.hpp>
#include <components/rtxvulkan/device/pipelinecache.hpp>
#include <components/rtxvulkan/pipeline/pipeline.hpp>
#include <components/rtxvulkan/texture/texture.hpp>
#include <components/rtxvulkan/trace/fogvolume.hpp>
#include <components/rtxvulkan/trace/gbuffer.hpp>
#include <components/rtxvulkan/trace/visibilitypass.hpp>
#include <components/rtxvulkan/vulkanrenderer.hpp>

namespace Rtx
{
    namespace
    {
        /// The three layouts every kernel of the pass names, which the renderer would own.
        ///
        /// **On the unvalidated device**, for the reason `getUnvalidatedHarness` gives: the layers
        /// took a second over the seven kernels the suite's renderer made and they checked already.
        class RtxVisibilityKernelsTest : public Testing::DeviceTest
        {
        protected:
            RtxVisibilityKernelsTest()
                : Testing::DeviceTest(false)
            {
            }

            SetLayout mTextures = TextureArray::describeLayout(getDevice());
            SetLayout mChannels = GBuffer::describeLayout(getDevice());
            SetLayout mVolume = FogVolume::describeLayout(getDevice());
        };

        /// The kernels are counted as they land, the count never goes back, and it reaches the
        /// whole only once the compile is over. What a frame draws with is the full tuple's seven —
        /// the trace, its direct light, its media, its layers, its bounce, the bounce's landing and
        /// the froxels' launch — beside the 5 kernels no tuple changes, the air's two and the
        /// sprites' three; every other tuple compiles behind them, a tuple a step, 15 of them.
        ///
        /// **The full tuple alone is watched as it compiles, and the table read off the suite's
        /// renderer**, which made every tuple before any test ran (`Testing::getRenderer` waits for
        /// `KernelSet::Every`): 97 kernels made again under the layers were seconds of this binary
        /// for an answer the 12 already give.
        TEST_F(RtxVisibilityKernelsTest, theKernelsAreCountedAsTheyLandAndTheCountEndsWithTheCompile)
        {
            const VisibilityPass pass(getDevice(), mTextures, mChannels, mVolume, false, false);
            constexpr std::uint32_t drawing = 1 + 1 + 1 + 1 + 1 + 1 + 1 + 5;

            JobProgress progress = pass.awaitKernels(KernelSet::Drawing, std::chrono::milliseconds::zero());
            while (!progress.isDone())
            {
                ASSERT_EQ(progress.mCount, drawing);
                ASSERT_LT(progress.mMade, progress.mCount);

                const std::uint32_t before = progress.mMade;
                progress = pass.awaitKernels(KernelSet::Drawing, std::chrono::milliseconds(1));
                ASSERT_GE(progress.mMade, before);
            }

            EXPECT_EQ(progress.mMade, drawing);
            EXPECT_EQ(progress.mCount, drawing);
            EXPECT_NO_THROW(pass.awaitKernels(KernelSet::Every, std::chrono::hours(1)));

            // A pass that specializes nothing has nothing behind the full tuple to wait for.
            const JobProgress unspecialized = pass.awaitKernels(KernelSet::Every, std::chrono::milliseconds::zero());
            EXPECT_EQ(unspecialized.mMade, drawing);
            EXPECT_EQ(unspecialized.mCount, drawing);

            ASSERT_TRUE(Testing::getRenderer().getProfile().mSpecializeKernels);
            const JobProgress first
                = Testing::getRenderer().awaitKernels(KernelSet::Drawing, std::chrono::milliseconds::zero());
            EXPECT_EQ(first.mMade, drawing);
            EXPECT_EQ(first.mCount, drawing);
            const JobProgress every
                = Testing::getRenderer().awaitKernels(KernelSet::Every, std::chrono::milliseconds::zero());
            EXPECT_EQ(every.mMade, drawing + 15u);
            EXPECT_EQ(every.mCount, drawing + 15u);
        }

        /// **No kernel spills past its ceiling**, where the driver says (`Device::compiledUseOf`). The
        /// full tuple, which carries every case. A kernel that spills runs at the speed of its
        /// scratch: the inline trace's, which carried 800 words a lane through 256 registers, drew an
        /// 80x45 frame of a Radeon 680M in 650 ms
        /// (`docs/rtx/research/2026-10-21-amd-680m-stage-times.md`).
        ///
        /// - **No scratch and 128 registers at most, but where a kernel holds more, at what it holds**,
        ///   which only falls: the trace's stages, whose walks keep their records live through one
        ///   loop ("The spill gate holds what each kernel spills"), measured on the 680M, the one
        ///   driver known to report scratch. A dispatch at 128 registers runs eight waves on an RDNA
        ///   SIMD, which holds 1024 a lane across them, and one at 256 four, and a ray tracer lives on
        ///   the waves that hide its memory. A count of a 32-lane wave's, which is what the trace's
        ///   dispatch is pinned to (`SubgroupSize::ThirtyTwo`) and every driver that reports an
        ///   executable says it ran.
        /// - **A launch's stack at what each holds**, which only falls, every driver answering it: the
        ///   most any driver reports, one ceiling for every card. RADV's, measured on the 680M under
        ///   Mesa 26.2.4, where NVIDIA's driver reported none for three of the four launches and 48
        ///   bytes for the sprites' composite.
        ///   AMD's driver reports a launch's registers as a stub of three, whatever it traces; the
        ///   stack is where its spills show, as the 81 KB of the trace when it was a launch did.
        TEST_F(RtxVisibilityKernelsTest, noKernelSpillsPastItsCeiling)
        {
            struct Ceiling
            {
                std::string_view mName;
                std::uint64_t mScratch;
                std::uint64_t mRegisters;
            };
            constexpr Ceiling sEveryOther{ "", 0, 128 };
            // `direct` at 256 and not 244, a rise inside the waves it allowed (`docs/rtx/architecture.md`,
            // "No kernel spills past its ceiling").
            constexpr std::array<Ceiling, 5> sCeilings{ {
                { "visibility", 0, 256 },
                { "direct", 0, 256 },
                { "media", 0, 256 },
                { "panes", 192, 256 },
                { "landing", 0, 256 },
            } };
            constexpr std::array<std::pair<std::string_view, std::uint64_t>, 4> sStacks{ {
                { "fog scatter", 320 },
                { "fog depth", 320 },
                { "sprite composite", 672 },
                { "sprite shelter", 320 },
            } };

            const VisibilityPass pass(getDevice(), mTextures, mChannels, mVolume, false, false);
            std::vector<VisibilityPass::NamedKernel> kernels;
            pass.listKernels(kernels);
            ASSERT_EQ(kernels.size(), 1u + 1u + 1u + 1u + 1u + 1u + 1u + 5u);

            for (const VisibilityPass::NamedKernel& kernel : kernels)
            {
                Ceiling ceiling = sEveryOther;
                for (const Ceiling& held : sCeilings)
                {
                    if (kernel.mName == held.mName)
                        ceiling = held;
                }

                const CompiledUse use
                    = getDevice().compiledUseOf(kernel.mPipeline->getHandle(), kernel.mPipeline->getBindPoint());
                if (use.mScratchBytes.has_value())
                {
                    EXPECT_LE(*use.mScratchBytes, ceiling.mScratch) << "the " << kernel.mName << " kernel's scratch";
                }

                if (use.mStackBytes.has_value())
                {
                    std::uint64_t stack = 0;
                    for (const auto& [name, held] : sStacks)
                    {
                        if (kernel.mName == name)
                            stack = held;
                    }
                    EXPECT_LE(*use.mStackBytes, stack) << "the " << kernel.mName << " kernel's stack";
                }
                else if (use.mRegisters.has_value())
                {
                    EXPECT_LE(*use.mRegisters, ceiling.mRegisters) << "the " << kernel.mName << " kernel's registers";
                }

                const bool stage = kernel.mName == "visibility" || kernel.mName == "direct" || kernel.mName == "media"
                    || kernel.mName == "panes" || kernel.mName == "bounce" || kernel.mName == "landing";
                if (stage && use.mSubgroupSize.has_value())
                {
                    EXPECT_EQ(*use.mSubgroupSize, 32u) << "the " << kernel.mName << " kernel's lanes";
                }
            }
        }

        /// A kernel that cannot be made is thrown to every ask, the bounded one included, and not
        /// only to the first: a caller that caught it once must not go on to record from a table
        /// with a hole in it. The constructor returns before any kernel is made, so it is the one
        /// call that does not throw. **A device of its own, whose shader directory holds nothing**,
        /// because the modules are the device's to find.
        TEST_F(RtxVisibilityKernelsTest, aKernelThatCannotBeMadeIsThrownToEveryAsk)
        {
            const Instance& instance = *mHarness.mInstance;
            const Device empty(instance, PhysicalDevice::select(instance.getHandle(), {}),
                ShaderSet{ .mDirectory = "no-such-directory" }, PipelineCacheSpec{}, PipelineStatistics::Skipped);
            const SetLayout textures = TextureArray::describeLayout(empty);
            const SetLayout channels = GBuffer::describeLayout(empty);
            const SetLayout volume = FogVolume::describeLayout(empty);
            const VisibilityPass pass(empty, textures, channels, volume, false, true);

            EXPECT_THROW(pass.awaitKernels(KernelSet::Drawing, std::chrono::hours(1)), InputError);
            EXPECT_THROW(pass.awaitKernels(KernelSet::Drawing, std::chrono::hours(1)), InputError);
            EXPECT_THROW(pass.awaitKernels(KernelSet::Every, std::chrono::milliseconds::zero()), InputError);
        }
    }
}
