#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/bound.hpp>
#include <apps/components_tests/rtxvulkan/support/device/harness.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtx/common/jobprogress.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/instance.hpp>
#include <components/rtxvulkan/device/physicaldevice.hpp>
#include <components/rtxvulkan/device/pipelinecache.hpp>
#include <components/rtxvulkan/frame/gbuffer.hpp>
#include <components/rtxvulkan/pipeline/pipeline.hpp>
#include <components/rtxvulkan/texture/texture.hpp>
#include <components/rtxvulkan/trace/fogvolume.hpp>
#include <components/rtxvulkan/trace/visibilitypass.hpp>
#include <components/rtxvulkan/vulkanrenderer.hpp>

namespace Rtx
{
    namespace
    {
        /// **Every tuple compiled once for the binary**, and read by every test that reads the
        /// specialized table: two tests that each made it compiled every tuple twice, 57 s between them.
        /// On the unvalidated device, which lives for the binary as this does.
        struct SpecializedKernels
        {
            explicit SpecializedKernels(const Device& device)
                : mTextures(TextureArray::describeLayout(device))
                , mChannels(GBuffer::describeLayout(device))
                , mVolume(FogVolume::describeLayout(device))
                , mPass(device, mTextures, mChannels, mVolume, false, true, UpscaleMasks::Written)
            {
                mPass.listKernels(mKernels);
            }

            SpecializedKernels(const SpecializedKernels&) = delete;
            SpecializedKernels& operator=(const SpecializedKernels&) = delete;
            SpecializedKernels(SpecializedKernels&&) = delete;
            SpecializedKernels& operator=(SpecializedKernels&&) = delete;

            SetLayout mTextures;
            SetLayout mChannels;
            SetLayout mVolume;
            VisibilityPass mPass;
            std::vector<VisibilityPass::NamedKernel> mKernels;
        };

        /// Let go of after the suite's last test, while the device it stands on is whole.
        Testing::Once<SpecializedKernels> sSpecialized;

        const std::vector<VisibilityPass::NamedKernel>& specializedKernels()
        {
            return sSpecialized
                .get([] { return std::make_unique<SpecializedKernels>(*Testing::getUnvalidatedHarness().mDevice); })
                .mKernels;
        }

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

            static void TearDownTestSuite() { sSpecialized.release(); }

            SetLayout mTextures = TextureArray::describeLayout(getDevice());
            SetLayout mChannels = GBuffer::describeLayout(getDevice());
            SetLayout mVolume = FogVolume::describeLayout(getDevice());
        };

        /// The kernels are counted as they land, the count never goes back, and it reaches the whole only once
        /// the compile is over. What a frame draws with is the full tuple's nine (the trace, the water's walk,
        /// its direct light, media, layers, bounce, the bounce's landing, the ambient's stage and the froxels'
        /// launch) beside the 5 kernels no tuple changes (the air's two and the sprites' three); every other
        /// tuple compiles behind them, a tuple a step, 47 of them: the 32 of the maps, the sky and a dark map's
        /// alpha, and the 16 with the maps that march a relief beside them, but the full tuple, one of those.
        ///
        /// Only the full tuple is watched as it compiles, the rest read off the suite's renderer, which made
        /// every tuple before any test ran (`Testing::getRenderer` waits for `KernelSet::Every`): every kernel
        /// made again under the layers would be seconds of this binary for an answer the 14 already give.
        TEST_F(RtxVisibilityKernelsTest, theKernelsAreCountedAsTheyLandAndTheCountEndsWithTheCompile)
        {
            const VisibilityPass pass(getDevice(), mTextures, mChannels, mVolume, false, false, UpscaleMasks::Written);
            constexpr std::uint32_t drawing = 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 5;

            JobProgress progress = pass.awaitKernels(KernelSet::Drawing, std::chrono::milliseconds::zero());
            const Testing::WaitBound bound(Testing::sColdPatience);
            while (!progress.isDone())
            {
                ASSERT_FALSE(bound.passed()) << progress.mMade << " of " << progress.mCount << " kernels made";
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
            EXPECT_EQ(every.mMade, drawing + 47u);
            EXPECT_EQ(every.mCount, drawing + 47u);
        }

        /// No kernel spills past its ceiling, where the driver says (`Device::compiledUseOf`): the full tuple,
        /// which carries every case, and the same without a dark map's alpha, which nearly every scene that has
        /// everything else runs. A kernel that spills runs at the speed of its scratch: the inline trace's, 800
        /// words a lane through 256 registers, drew an 80x45 frame of a low-end AMD integrated GPU in 650 ms
        /// (`docs/research/2026-10-21-amd-integrated-stage-times.md`).
        ///
        /// - Each target driver's own ceilings, each one that only falls: AMD's own driver, RADV and NVIDIA,
        ///   each read by its own names (`CompiledUse`), since one compiler's count is no count of another's.
        ///   A driver with none is a failure: the targets are those three.
        /// - No scratch and 128 registers at most, but where a kernel holds more, at what it holds: the
        ///   trace's stages, whose walks keep their records live through one loop ("The spill gate holds what
        ///   each kernel spills"). 128 registers run eight waves on an RDNA SIMD and 256 four, and a ray tracer
        ///   lives on the waves that hide its memory. Counted for a 32-lane wave, which the trace's dispatch is
        ///   pinned to (`SubgroupSize::ThirtyTwo`) and every driver that reports an executable says it ran.
        /// - The shared memory each holds: what a kernel declares plus what the compiler keeps there (a ray
        ///   query's traversal, 2048 bytes on AMD's driver, on NVIDIA's what each kernel's live state asks), so
        ///   a rise is a cost the gate names, as a spill is.
        /// - A launch's stack at what each holds, every driver answering it: the most any driver reports, one
        ///   ceiling for every card. RADV's, measured on the integrated GPU, where NVIDIA's driver reported
        ///   none for three of the four launches and 48 bytes for the sprites' composite. AMD's driver reports
        ///   a launch's registers as a stub of three; its spills show in the stack.
        TEST_F(RtxVisibilityKernelsTest, noKernelSpillsPastItsCeiling)
        {
            struct Ceiling
            {
                std::string_view mName;
                std::uint64_t mScratch;
                std::uint64_t mRegisters;
                std::uint64_t mShared;
            };
            struct DriverCeilings
            {
                VkDriverId mDriver;
                Ceiling mEveryOther;
                std::span<const Ceiling> mWithoutDarkAlpha;

                /// The whole tuple, which a scene with a see-through dark map runs and every other scene only
                /// until its own tuple lands, at what that read costs it (`HAS_DARK_ALPHA`).
                std::span<const Ceiling> mWhole;
            };

            // Measured on a low-end AMD integrated GPU under AMD's Windows driver. `visibility` at 145 ("The
            // sea's walk, a stage of its own"); the water and media at what they hold since each walk resolves
            // its hit once ("A walk resolves the surface it ends on once"); the whole tuple's `media` at 220
            // since a read pairs an image with its sampler (`TEXTURE`), still four waves on an RDNA SIMD
            // (`docs/research/2026-11-09-separate-samplers.md`); the landing at 144 since a sheet takes its
            // span off the corners the hit carried (`docs/research/2026-11-09-sheet-span.md`); `panes` with
            // no scratch since a layer under the water takes its column's shafts from `media`
            // (`docs/research/2026-11-12-panes-column-ratio.md`).
            constexpr std::array<Ceiling, 6> sAmdWithoutDarkAlpha{ {
                { "visibility", 0, 145, 2048 },
                { "water", 0, 177, 2048 },
                { "direct", 0, 160, 2048 },
                { "media", 0, 192, 2048 },
                { "panes", 0, 248, 2048 },
                { "landing", 0, 144, 2048 },
            } };
            constexpr std::array<Ceiling, 7> sAmdWhole{ {
                { "visibility", 0, 145, 2048 },
                { "water", 0, 190, 2048 },
                { "direct", 0, 144, 2048 },
                { "media", 0, 220, 2048 },
                { "panes", 0, 254, 2048 },
                { "landing", 0, 144, 2048 },
                { "bounce", 0, 144, 2048 },
            } };
            // Measured on a high-end NVIDIA laptop GPU, whose compiler holds a stage at 128 registers and keeps
            // what does not fit in shared memory, and the whole tuple's `media` 32 bytes of local memory a lane,
            // under driver 615.78. The water at 3456 bytes of shared memory since a mapped ground's tangent
            // selects its axis (`traversal.glsl`), the price of no NaN on a face along ±x, which 16 warps an SM
            // at 128 registers' shared memory holds all the same.
            constexpr std::array<Ceiling, 5> sNvidiaWithoutDarkAlpha{ {
                { "water", 0, 128, 3456 },
                { "direct", 0, 128, 1664 },
                { "media", 0, 128, 6272 },
                { "panes", 0, 168, 5760 },
                { "landing", 0, 128, 896 },
            } };
            constexpr std::array<Ceiling, 6> sNvidiaWhole{ {
                { "water", 0, 128, 3456 },
                { "direct", 0, 128, 1920 },
                { "media", 32, 128, 6272 },
                { "panes", 0, 168, 6272 },
                { "landing", 0, 128, 896 },
                { "fog integrate", 0, 128, 1792 },
            } };
            // Measured on the handheld's integrated GPU under RADV. `visibility` at 144 and the landing under the
            // rule since a sheet takes its span off the corners the hit carried
            // (`docs/research/2026-11-09-sheet-span.md`).
            constexpr std::array<Ceiling, 5> sRadvWithoutDarkAlpha{ {
                { "visibility", 0, 144, 2048 },
                { "water", 0, 160, 2048 },
                { "direct", 0, 144, 2048 },
                { "media", 0, 176, 2048 },
                { "panes", 0, 256, 2048 },
            } };
            constexpr std::array<Ceiling, 5> sRadvWhole{ {
                { "visibility", 0, 144, 2048 },
                { "water", 0, 160, 2048 },
                { "direct", 0, 144, 2048 },
                { "media", 0, 192, 2048 },
                { "panes", 0, 256, 2048 },
            } };
            const std::array sDrivers{ DriverCeilings{ VK_DRIVER_ID_AMD_PROPRIETARY, { "", 0, 128, 2048 },
                                           sAmdWithoutDarkAlpha, sAmdWhole },
                DriverCeilings{ VK_DRIVER_ID_MESA_RADV, { "", 0, 128, 2048 }, sRadvWithoutDarkAlpha, sRadvWhole },
                DriverCeilings{
                    VK_DRIVER_ID_NVIDIA_PROPRIETARY, { "", 0, 128, 0 }, sNvidiaWithoutDarkAlpha, sNvidiaWhole } };
            constexpr std::array<std::pair<std::string_view, std::uint64_t>, 4> sStacks{ {
                { "fog scatter", 320 },
                { "fog depth", 320 },
                { "sprite composite", 672 },
                { "sprite shelter", 320 },
            } };

            const VkDriverId driver = getDevice().getPhysicalDevice().getProperties().mVulkan12.driverID;
            const auto measured = std::ranges::find(sDrivers, driver, &DriverCeilings::mDriver);
            ASSERT_NE(measured, sDrivers.end()) << "no ceilings measured for driver " << driver;

            const std::vector<VisibilityPass::NamedKernel>& kernels = specializedKernels();

            const std::uint32_t whole = VisibilityVariant{}.index();
            const std::uint32_t withoutDarkAlpha = VisibilityVariant{ .mDarkAlpha = false }.index();
            const auto tupled = [&](const std::uint32_t tuple) {
                return std::ranges::count_if(
                    kernels, [&](const VisibilityPass::NamedKernel& kernel) { return kernel.mTuple == tuple; });
            };
            ASSERT_EQ(tupled(whole), 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 5);
            ASSERT_EQ(tupled(withoutDarkAlpha), 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1);

            for (const VisibilityPass::NamedKernel& kernel : kernels)
            {
                if (kernel.mTuple != whole && kernel.mTuple != withoutDarkAlpha)
                    continue;

                Ceiling ceiling = measured->mEveryOther;
                const std::span<const Ceiling> ceilings
                    = kernel.mTuple == whole ? measured->mWhole : measured->mWithoutDarkAlpha;
                for (const Ceiling& stated : ceilings)
                {
                    if (kernel.mName == stated.mName)
                        ceiling = stated;
                }

                const CompiledUse use
                    = getDevice().compiledUseOf(kernel.mPipeline->getHandle(), kernel.mPipeline->getBindPoint());
                // A launch's stack is its scratch on RADV, a lane's stack a lane: held as its stack.
                if (use.mScratchBytes.has_value() && !use.mStackBytes.has_value())
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

                if (use.mSharedBytes.has_value())
                {
                    EXPECT_LE(*use.mSharedBytes, ceiling.mShared)
                        << "the " << kernel.mName << " kernel's shared memory";
                }

                const bool stage = kernel.mName == "visibility" || kernel.mName == "water" || kernel.mName == "direct"
                    || kernel.mName == "media" || kernel.mName == "panes" || kernel.mName == "bounce"
                    || kernel.mName == "landing" || kernel.mName == "unbounced";
                if (stage && use.mSubgroupSize.has_value())
                {
                    EXPECT_EQ(*use.mSubgroupSize, 32u) << "the " << kernel.mName << " kernel's lanes";
                }
            }
        }

        /// **Tuples a stage reads alike share its pipeline** (`VisibilityPass::canonicalOf`): the unbounced
        /// stage reads no sun and no moons, so a tuple without either runs the full tuple's pipeline, and it
        /// reads the sea, so a tuple without one runs a pipeline of its own; the eye's stage reads every
        /// constant, so no two of its tuples share one. Of the 324 stage pipelines listed, 80 are shared.
        TEST_F(RtxVisibilityKernelsTest, tuplesAStageReadsAlikeShareItsPipeline)
        {
            const std::vector<VisibilityPass::NamedKernel>& kernels = specializedKernels();
            const auto of = [&](std::string_view name, VisibilityVariant tuple) {
                const auto found = std::ranges::find_if(kernels, [&](const VisibilityPass::NamedKernel& kernel) {
                    return kernel.mName == name && kernel.mTuple == tuple.index();
                });
                return found != kernels.end() ? found->mPipeline : nullptr;
            };

            const VisibilityVariant full{};
            const Pipeline* const whole = of("unbounced", full);
            ASSERT_NE(whole, nullptr);
            EXPECT_EQ(of("unbounced", VisibilityVariant{ .mSun = false, .mMoons = false }), whole);
            EXPECT_NE(of("unbounced", VisibilityVariant{ .mSea = false }), whole);
            EXPECT_NE(of("visibility", VisibilityVariant{ .mSun = false }), of("visibility", full));

            std::vector<const Pipeline*> distinct;
            for (const VisibilityPass::NamedKernel& kernel : kernels)
                distinct.push_back(kernel.mPipeline);
            std::ranges::sort(distinct);
            const auto [first, last] = std::ranges::unique(distinct);
            distinct.erase(first, last);
            EXPECT_EQ(kernels.size() - distinct.size(), 80u) << "the stage pipelines shared tuples save";
        }

        /// A kernel that cannot be made is thrown to every ask, the bounded one included, and not only the
        /// first: a caller that caught it once must not go on to record from a table with a hole in it. The
        /// constructor returns before any kernel is made, so it is the one call that does not throw. A device
        /// of its own, whose shader set holds nothing, because the modules are the device's to find.
        TEST_F(RtxVisibilityKernelsTest, aKernelThatCannotBeMadeIsThrownToEveryAsk)
        {
            const Instance& instance = *mHarness.mInstance;
            const Device empty(instance, PhysicalDevice::select(instance.getHandle(), {}), ShaderSet{},
                PipelineCacheSpec{}, DeviceReport{});
            const SetLayout textures = TextureArray::describeLayout(empty);
            const SetLayout channels = GBuffer::describeLayout(empty);
            const SetLayout volume = FogVolume::describeLayout(empty);
            const VisibilityPass pass(empty, textures, channels, volume, false, true, UpscaleMasks::Written);

            EXPECT_THROW(pass.awaitKernels(KernelSet::Drawing, std::chrono::hours(1)), InputError);
            EXPECT_THROW(pass.awaitKernels(KernelSet::Drawing, std::chrono::hours(1)), InputError);
            EXPECT_THROW(pass.awaitKernels(KernelSet::Every, std::chrono::milliseconds::zero()), InputError);
        }
    }
}
