#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
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
#include <components/rtxvulkan/device/pipelinereport.hpp>
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
        ///
        /// **On a device of its own over the shipped shaders** (`getShippedShaders`), on the unvalidated
        /// harness's instance: what the driver compiled is measured of what the game runs, and the census's
        /// counting, which the harness's devices carry, is no part of a ceiling.
        struct SpecializedKernels
        {
            explicit SpecializedKernels(const Instance& instance)
                : mDevice(instance, PhysicalDevice::select(instance.getHandle(), {}), Testing::getShippedShaders(),
                      Testing::getPipelineCacheSpec(), DeviceReport{})
                , mTextures(TextureArray::describeLayout(mDevice))
                , mChannels(GBuffer::describeLayout(mDevice))
                , mVolume(FogVolume::describeLayout(mDevice))
                , mPass(mDevice, mTextures, mChannels, mVolume, false, true, UpscaleMasks::Written)
            {
                mPass.listKernels(mKernels);
            }

            SpecializedKernels(const SpecializedKernels&) = delete;
            SpecializedKernels& operator=(const SpecializedKernels&) = delete;
            SpecializedKernels(SpecializedKernels&&) = delete;
            SpecializedKernels& operator=(SpecializedKernels&&) = delete;

            Device mDevice;
            SetLayout mTextures;
            SetLayout mChannels;
            SetLayout mVolume;
            VisibilityPass mPass;
            std::vector<VisibilityPass::NamedKernel> mKernels;
        };

        /// Let go of after the suite's last test, while the device it stands on is whole.
        Testing::Once<SpecializedKernels> sSpecialized;

        const SpecializedKernels& specialized()
        {
            return sSpecialized.get(
                [] { return std::make_unique<SpecializedKernels>(*Testing::getUnvalidatedHarness().mInstance); });
        }

        /// The three layouts every kernel of the pass names, which the renderer would own.
        ///
        /// **On the unvalidated device**, for the reason `getUnvalidatedHarness` gives: the layers
        /// took a second over the seven kernels the suite's renderer made and they checked already.
        class RtxVisibilityKernelsTest : public Testing::DeviceTest
        {
        protected:
            RtxVisibilityKernelsTest()
                : Testing::DeviceTest(Testing::Validation::Unvalidated)
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
        /// launch) beside the 6 kernels no tuple changes (the air's two, the sprites' three and the lamp lists');
        /// every other tuple compiles behind them, a tuple a step, 47 of them: the 32 of the maps, the sky and a
        /// dark map's alpha, and the 16 with the maps that march a relief beside them, but the full tuple, one of
        /// those.
        ///
        /// Only the full tuple is watched as it compiles, the rest read off the suite's renderer, which made
        /// every tuple before any test ran (`Testing::getRenderer` waits for `KernelSet::Every`): every kernel
        /// made again under the layers would be seconds of this binary for an answer the 15 already give.
        TEST_F(RtxVisibilityKernelsTest, theKernelsAreCountedAsTheyLandAndTheCountEndsWithTheCompile)
        {
            const VisibilityPass pass(getDevice(), mTextures, mChannels, mVolume, false, false, UpscaleMasks::Written);
            constexpr std::uint32_t drawing = 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 6;

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

        /// No kernel spills past its ceiling, where the driver says (`PipelineReport::compiledUseOf`), in any tuple
        /// a frame runs. A kernel that spills runs at the speed of its scratch: the inline trace's, 800 words a
        /// lane through 256 registers, drew an 80x45 frame of a low-end AMD integrated GPU in 650 ms
        /// (`docs/research/2026-10-21-amd-integrated-stage-times.md`).
        ///
        /// - Each target driver's own ceilings, each one that only falls: AMD's own driver, RADV and NVIDIA,
        ///   each read by its own names (`CompiledUse`), since one compiler's count is no count of another's.
        ///   A driver with none is a failure: the targets are those three.
        /// - **A row holds every tuple of its stage that agrees with the constants it names, and of the rows
        ///   that hold a tuple the one naming the most is its ceiling**, as Mesa's CI keeps a driver's list of
        ///   expected failures: a ceiling's reason is a stage's, and a tuple is its stage with code taken out.
        ///   Refused: a recorded figure a kernel and tuple, as Mesa's shader-db compares two listings, some 330
        ///   lines a driver with no reason beside any, each stale on the two drivers not at hand.
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
            /// The tuples a row holds: those that agree with each constant it names, every tuple where it names
            /// none.
            struct TupleSet
            {
                std::optional<bool> mSun = std::nullopt;
                std::optional<bool> mMoons = std::nullopt;
                std::optional<bool> mSea = std::nullopt;
                std::optional<bool> mMaps = std::nullopt;
                std::optional<bool> mDarkAlpha = std::nullopt;
                std::optional<bool> mRelief = std::nullopt;

                bool holds(const VisibilityVariant& tuple) const
                {
                    const auto agrees = [](const std::optional<bool> named, const bool has) {
                        return !named.has_value() || *named == has;
                    };
                    return agrees(mSun, tuple.mSun) && agrees(mMoons, tuple.mMoons) && agrees(mSea, tuple.mSea)
                        && agrees(mMaps, tuple.mMaps) && agrees(mDarkAlpha, tuple.mDarkAlpha)
                        && agrees(mRelief, tuple.mRelief);
                }

                std::ptrdiff_t named() const
                {
                    const std::array constants{ mSun, mMoons, mSea, mMaps, mDarkAlpha, mRelief };
                    return std::ranges::count_if(
                        constants, [](const std::optional<bool> constant) { return constant.has_value(); });
                }
            };
            struct Ceiling
            {
                std::uint64_t mScratch;
                std::uint64_t mRegisters;
                std::uint64_t mShared;
            };
            struct Row
            {
                std::string_view mName;
                TupleSet mTuples;
                Ceiling mCeiling;
            };
            struct DriverCeilings
            {
                VkDriverId mDriver;

                /// What holds a kernel no row does.
                Ceiling mRule;
                std::span<const Row> mRows;
            };

            // Measured on a low-end AMD integrated GPU under AMD's Windows driver, in two tuples: the whole one,
            // which the rows of a dark map's alpha are, and the same without that read (`HAS_DARK_ALPHA`), which
            // the others are. Every other tuple is one of the two with code taken out, and is held at its figure.
            // `visibility` under the rule since a hit's vertex colours and a relief's layers' frame stand past
            // the ground stack (`docs/research/2026-11-13-amd-integrated-register-tiers.md`); the water and media
            // at what they hold since each walk resolves its hit once ("A walk resolves the surface it ends on
            // once"); `media` with a dark map's alpha at 216 since a read pairs an image with its sampler
            // (`TEXTURE`), still four waves on an RDNA SIMD (`docs/research/2026-11-09-separate-samplers.md`);
            // the landing at 144 since a sheet takes its span off the corners the hit carried
            // (`docs/research/2026-11-09-sheet-span.md`); `panes` with no scratch since a layer under the water
            // takes its column's shafts from `media` (`docs/research/2026-11-12-panes-column-ratio.md`).
            constexpr std::array<Row, 10> sAmd{ {
                { "water", {}, { 0, 177, 2048 } },
                { "water", { .mDarkAlpha = true }, { 0, 190, 2048 } },
                { "direct", {}, { 0, 160, 2048 } },
                { "direct", { .mDarkAlpha = true }, { 0, 144, 2048 } },
                { "media", {}, { 0, 192, 2048 } },
                { "media", { .mDarkAlpha = true }, { 0, 216, 2048 } },
                { "panes", {}, { 0, 248, 2048 } },
                { "panes", { .mDarkAlpha = true }, { 0, 254, 2048 } },
                { "landing", {}, { 0, 144, 2048 } },
                { "bounce", { .mDarkAlpha = true }, { 0, 144, 2048 } },
            } };
            // Measured on a high-end NVIDIA laptop GPU under driver 620.70.06, in every tuple. Its compiler keeps
            // what a stage's registers do not hold in shared memory, up to a figure its register count sets —
            // 4224 bytes a workgroup at 80 registers, 4992 at 96, 6272 at 128 — and what is over in a lane's
            // local memory, its scratch: one spill in two places, which it takes over more registers
            // (`docs/research/2026-11-14-nvidia-register-spills.md`). So a row holds a stage's scratch at what it
            // is and not at none, and its registers at the count the compiler steps to, 96, 128 or 168. The maps
            // decide that count in most stages, and a tuple without them holds fewer registers and more shared
            // memory: here a tuple with code taken out is not held by the tuple it was taken from.
            // `fog integrate` holds its column depths, floats a thread read at an index the tent's loop leaves
            // unrolled, which the compiler keeps in shared memory and not a lane's local memory. `lamp lists`
            // holds its tile's tables, three words a lamp a tile's rays can reach and the list,
            // `3 × 128 × 4 + 8 × 4` and the count.
            constexpr std::array<Row, 22> sNvidia{ {
                { "visibility", {}, { 0, 128, 3712 } },
                { "visibility", { .mMaps = false }, { 0, 96, 2944 } },
                { "visibility", { .mSea = true, .mMaps = true }, { 0, 128, 0 } },
                { "water", {}, { 0, 128, 2944 } },
                { "water", { .mMaps = true }, { 0, 128, 2688 } },
                { "direct", {}, { 48, 128, 4992 } },
                { "direct", { .mMaps = false }, { 48, 96, 4992 } },
                { "direct", { .mRelief = true }, { 0, 128, 2688 } },
                { "direct", { .mDarkAlpha = false, .mRelief = true }, { 0, 128, 2176 } },
                { "media", {}, { 16, 128, 6272 } },
                { "media", { .mDarkAlpha = false }, { 0, 128, 5504 } },
                { "panes", {}, { 112, 168, 6272 } },
                { "panes", { .mSea = false }, { 96, 128, 6272 } },
                { "panes", { .mSun = true, .mMoons = true, .mSea = true, .mMaps = true }, { 0, 168, 5504 } },
                { "panes", { .mSun = true, .mMoons = true, .mSea = true, .mMaps = true, .mDarkAlpha = false },
                    { 0, 168, 4992 } },
                { "landing", { .mMaps = false }, { 0, 96, 3968 } },
                { "landing", { .mMaps = true }, { 0, 128, 384 } },
                { "landing", { .mMaps = true, .mRelief = true }, { 0, 128, 0 } },
                { "bounce", { .mMaps = false }, { 0, 96, 384 } },
                { "bounce", { .mMaps = true }, { 0, 128, 0 } },
                { "fog integrate", {}, { 0, 128, 1792 } },
                { "lamp lists", {}, { 0, 128, 1584 } },
            } };
            // Measured on the handheld's integrated GPU under RADV, in the two tuples AMD's rows were.
            // `visibility` at 144 and the landing under the rule since a sheet takes its span off the corners
            // the hit carried (`docs/research/2026-11-09-sheet-span.md`).
            constexpr std::array<Row, 6> sRadv{ {
                { "visibility", {}, { 0, 144, 2048 } },
                { "water", {}, { 0, 160, 2048 } },
                { "direct", {}, { 0, 144, 2048 } },
                { "media", {}, { 0, 176, 2048 } },
                { "media", { .mDarkAlpha = true }, { 0, 192, 2048 } },
                { "panes", {}, { 0, 256, 2048 } },
            } };
            const std::array sDrivers{
                DriverCeilings{ VK_DRIVER_ID_AMD_PROPRIETARY, { 0, 128, 2048 }, sAmd },
                DriverCeilings{ VK_DRIVER_ID_MESA_RADV, { 0, 128, 2048 }, sRadv },
                DriverCeilings{ VK_DRIVER_ID_NVIDIA_PROPRIETARY, { 0, 128, 0 }, sNvidia },
            };
            constexpr std::array<std::pair<std::string_view, std::uint64_t>, 4> sStacks{ {
                { "fog scatter", 320 },
                { "fog depth", 320 },
                { "sprite composite", 672 },
                { "sprite shelter", 320 },
            } };

            const VkDriverId driver = getDevice().getPhysicalDevice().getProperties().mVulkan12.driverID;
            const auto measured = std::ranges::find(sDrivers, driver, &DriverCeilings::mDriver);
            ASSERT_NE(measured, sDrivers.end()) << "no ceilings measured for driver " << driver;

            const SpecializedKernels& table = specialized();
            const std::vector<VisibilityPass::NamedKernel>& kernels = table.mKernels;

            // Each tuple by its index, which a listed kernel carries.
            std::array<VisibilityVariant, VisibilityVariant::sCount> tuples{};
            for (std::uint32_t bits = 0; bits < VisibilityVariant::sCount; ++bits)
            {
                const VisibilityVariant tuple{
                    .mSun = (bits & 1u) != 0,
                    .mMoons = (bits & 2u) != 0,
                    .mSea = (bits & 4u) != 0,
                    .mMaps = (bits & 8u) != 0,
                    .mDarkAlpha = (bits & 16u) != 0,
                    .mRelief = (bits & 32u) != 0,
                };
                tuples[tuple.index()] = tuple;
            }
            std::array<bool, VisibilityVariant::sCount> listed{};
            std::uint32_t held = 0;
            for (const VisibilityPass::NamedKernel& kernel : kernels)
            {
                if (!std::exchange(listed[kernel.mTuple], true))
                    ++held;
            }
            ASSERT_EQ(held, VisibilityVariant::sMade) << "the tuples held";
            ASSERT_EQ(std::ranges::count(kernels, VisibilityVariant{}.index(), &VisibilityPass::NamedKernel::mTuple),
                1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 1 + 6);

            // Named with its tuple's constants, but a kernel no tuple changes, which is listed once.
            std::vector<std::string> names;
            for (const VisibilityPass::NamedKernel& kernel : kernels)
            {
                const bool tupled = std::ranges::count(kernels, kernel.mName, &VisibilityPass::NamedKernel::mName) > 1;
                names.push_back(tupled ? tuples[kernel.mTuple].describe(kernel.mName) : std::string(kernel.mName));
            }

            // **Every driver's rows are read against the listing, whichever driver runs**, since which row is a
            // kernel's ceiling asks no device: a row that is no kernel's names a stage or a tuple nothing has,
            // and two that hold a kernel and name as many constants leave its ceiling to their order.
            std::vector<Ceiling> ceilings(kernels.size(), measured->mRule);
            for (const DriverCeilings& stated : sDrivers)
            {
                std::vector<bool> used(stated.mRows.size(), false);
                for (std::size_t at = 0; at < kernels.size(); ++at)
                {
                    std::optional<std::size_t> ceiling;
                    std::ptrdiff_t named = -1;
                    int alike = 0;
                    for (std::size_t row = 0; row < stated.mRows.size(); ++row)
                    {
                        const Row& candidate = stated.mRows[row];
                        if (kernels[at].mName != candidate.mName
                            || !candidate.mTuples.holds(tuples[kernels[at].mTuple]))
                            continue;

                        const std::ptrdiff_t count = candidate.mTuples.named();
                        if (count == named)
                            ++alike;
                        if (count > named)
                        {
                            ceiling = row;
                            named = count;
                            alike = 1;
                        }
                    }
                    EXPECT_LE(alike, 1) << "driver " << stated.mDriver << "'s rows that hold `" << names[at]
                                        << "` and name as many constants";
                    if (!ceiling.has_value())
                        continue;

                    used[*ceiling] = true;
                    if (stated.mDriver == driver)
                        ceilings[at] = stated.mRows[*ceiling].mCeiling;
                }

                for (std::size_t row = 0; row < used.size(); ++row)
                {
                    EXPECT_TRUE(used[row]) << "driver " << stated.mDriver << "'s row " << row << ", of `"
                                           << stated.mRows[row].mName << "`, is no kernel's ceiling";
                }
            }

            for (std::size_t at = 0; at < kernels.size(); ++at)
            {
                const VisibilityPass::NamedKernel& kernel = kernels[at];
                const Ceiling& ceiling = ceilings[at];
                const std::string& name = names[at];

                const CompiledUse use = table.mDevice.getPipelineReport().compiledUseOf(
                    kernel.mPipeline->getHandle(), kernel.mPipeline->getBindPoint());
                // A launch's stack is its scratch on RADV, a lane's stack a lane: held as its stack.
                if (use.mScratchBytes.has_value() && !use.mStackBytes.has_value())
                {
                    EXPECT_LE(*use.mScratchBytes, ceiling.mScratch) << "the scratch of `" << name << "`";
                }

                if (use.mStackBytes.has_value())
                {
                    std::uint64_t stack = 0;
                    for (const auto& [launch, most] : sStacks)
                    {
                        if (kernel.mName == launch)
                            stack = most;
                    }
                    EXPECT_LE(*use.mStackBytes, stack) << "the stack of `" << name << "`";
                }
                else if (use.mRegisters.has_value())
                {
                    EXPECT_LE(*use.mRegisters, ceiling.mRegisters) << "the registers of `" << name << "`";
                }

                if (use.mSharedBytes.has_value())
                {
                    EXPECT_LE(*use.mSharedBytes, ceiling.mShared) << "the shared memory of `" << name << "`";
                }

                const bool stage = kernel.mName == "visibility" || kernel.mName == "water" || kernel.mName == "direct"
                    || kernel.mName == "media" || kernel.mName == "panes" || kernel.mName == "bounce"
                    || kernel.mName == "landing" || kernel.mName == "unbounced";
                if (stage && use.mSubgroupSize.has_value())
                {
                    EXPECT_EQ(*use.mSubgroupSize, 32u) << "the lanes of `" << name << "`";
                }
            }
        }

        /// **Tuples a stage reads alike share its pipeline** (`VisibilityPass::canonicalOf`): the unbounced
        /// stage reads no sun and no moons, so a tuple without either runs the full tuple's pipeline, and it
        /// reads the sea, so a tuple without one runs a pipeline of its own; the eye's stage reads every
        /// constant, so no two of its tuples share one. Of the 324 stage pipelines listed, 80 are shared.
        TEST_F(RtxVisibilityKernelsTest, tuplesAStageReadsAlikeShareItsPipeline)
        {
            const std::vector<VisibilityPass::NamedKernel>& kernels = specialized().mKernels;
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
