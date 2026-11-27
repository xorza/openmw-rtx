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
            ~SpecializedKernels() = default;

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
        /// every other tuple compiles behind them, a tuple a step, 5 of them: the 4 of the maps and the sea, and the 2
        /// with the maps that march a relief beside them, but the full tuple, one of those.
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
            EXPECT_EQ(every.mMade, drawing + 5u);
            EXPECT_EQ(every.mCount, drawing + 5u);
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
                std::optional<bool> mSea = std::nullopt;
                std::optional<bool> mMaps = std::nullopt;
                std::optional<bool> mRelief = std::nullopt;

                bool holds(const VisibilityVariant& tuple) const
                {
                    const auto agrees = [](const std::optional<bool> named, const bool has) {
                        return !named.has_value() || *named == has;
                    };
                    return agrees(mSea, tuple.mSea) && agrees(mMaps, tuple.mMaps) && agrees(mRelief, tuple.mRelief);
                }

                std::ptrdiff_t named() const
                {
                    const std::array constants{ mSea, mMaps, mRelief };
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

            // Measured on a low-end AMD integrated GPU under AMD's Windows driver 26.8.1, in every tuple, none of
            // which holds scratch, and held by the tuples without a dark map's alpha since its read left every
            // kernel: every other tuple is the whole one with code taken out and stands at or under its figure
            // (`docs/research/2026-11-13-amd-integrated-register-tiers.md`). `visibility` under the rule since a
            // hit's vertex colours and a relief's layers' frame stand past the ground stack; the water and media
            // at what they hold since each walk resolves its hit once ("A walk resolves the surface it ends on
            // once"); the landing under the rule since it reads no normal map and makes its lobe diffuse, 112 at
            // most (`docs/research/2026-11-19-one-kernel-registers.md`); `direct` at the 144 it holds; `panes` with no
            // scratch since a layer under the water takes its column's shafts from `media`
            // (`docs/research/2026-11-12-panes-column-ratio.md`), and at 256 in every tuple, four waves as at 248,
            // since a path end's rays are asked at their turns
            // (`docs/research/2026-11-15-radv-wave-tiers.md`).
            constexpr std::array<Row, 4> sAmd{ {
                { "water", {}, { 0, 177, 2048 } },
                { "direct", {}, { 0, 144, 2048 } },
                { "media", {}, { 0, 192, 2048 } },
                { "panes", {}, { 0, 256, 2048 } },
            } };
            // Measured on a high-end NVIDIA laptop GPU under driver 620.70.06, in every tuple. Its compiler keeps
            // what a stage's registers do not hold in shared memory, up to a figure its register count sets —
            // 4224 bytes a workgroup at 80 registers, 4992 at 96, 6272 at 128 — and what is over in a lane's
            // local memory, its scratch: one spill in two places, which it takes over more registers
            // (`docs/research/2026-11-14-nvidia-register-spills.md`). So a row holds a stage's scratch at what it
            // is and not at none, and its registers at the count the compiler steps to, 80, 96 or 128. The maps
            // decide that count in most stages, and a tuple without them holds fewer registers and more shared
            // memory: here a tuple with code taken out is not held by the tuple it was taken from. Measured
            // again once the stages carried less through their rays and their resolve
            // (`docs/research/2026-11-15-radv-wave-tiers.md`): no stage stands at 168 registers, the eye's at 96
            // in every tuple, and `panes` holds its 6272 bytes and up to 96 of a lane's local memory in all. With
            // a mod's maps read in every tuple with the maps (`docs/research/2026-11-19-one-kernel-registers.md`) the
            // landing, which reads no normal map and makes its lobe diffuse, holds in every tuple what it held without
            // the maps; and the eye's stage and the water's, which store the solid they end on and resolve none of
            // it, 94 and 128 registers in every tuple, the trace's own 80 where it has no sea with 1152 bytes of
            // shared memory. Measured again under 620.73.04, where `direct` keeps 3456 bytes in shared memory with the
            // maps and 2688 without, `panes` up to 80 of a lane's local memory, and the bounce with the maps 112
            // registers and 96 with a relief. The landing with 896 bytes of shared memory since a see-through surface's
            // glow is weighted as a blend in display values draws it: a spill held in shared memory, which no bench
            // showed
            // (`docs/research/2026-11-14-nvidia-register-spills.md`).
            // `fog integrate` holds its column depths, floats a thread read at an index the tent's loop leaves
            // unrolled, which the compiler keeps in shared memory and not a lane's local memory. `lamp lists`
            // holds its tile's tables, three words a lamp a tile's rays can reach and the list,
            // `3 × 128 × 4 + 8 × 4` and the count.
            constexpr std::array<Row, 12> sNvidia{ {
                { "visibility", {}, { 0, 96, 1152 } },
                { "water", {}, { 0, 128, 640 } },
                { "direct", {}, { 0, 128, 3456 } },
                { "direct", { .mMaps = false }, { 0, 96, 2688 } },
                { "media", {}, { 0, 128, 5504 } },
                { "panes", {}, { 80, 128, 6272 } },
                { "landing", {}, { 0, 96, 896 } },
                { "bounce", { .mMaps = false }, { 0, 80, 0 } },
                { "bounce", { .mMaps = true }, { 0, 96, 896 } },
                { "bounce", { .mMaps = true, .mRelief = false }, { 0, 112, 0 } },
                { "fog integrate", {}, { 0, 128, 1792 } },
                { "lamp lists", {}, { 0, 128, 1584 } },
            } };
            // Measured on the handheld's integrated GPU under RADV, Mesa 26.2.0, and compiled again by Mesa 26.2.7
            // for the same card under its drm-shim, in every tuple: a row holds the more of the two. A stage's
            // registers stand a step of sixteen under where they stood, and its waves one over, since its rays
            // are asked at their turns, a hit's rows are read again past the ground stack, the bounce's
            // hand-over is stored ahead of its ray and the eye's peel resolves once
            // (`docs/research/2026-11-15-radv-wave-tiers.md`). `media` and `panes` at what they held. A mod's maps
            // are read in every tuple since `docs/research/2026-11-19-one-kernel-registers.md`: the handheld's Mesa
            // holds the water's stage with a relief at 160, a step over the shim's. The landing at what it held without
            // the maps in every tuple, since it reads no normal map and makes its lobe diffuse. The eye's stage at 96
            // and the water's at 144 in every tuple, since they store the solid they end on and resolve none of it,
            // which `direct` resolves and shades: 128 with the maps, and 144 with a relief, whose march's depth stands
            // live through the rest of the resolve to be stored.
            constexpr std::array<Row, 13> sRadv{ {
                { "visibility", {}, { 0, 96, 2048 } },
                { "water", {}, { 0, 144, 2048 } },
                { "water", { .mRelief = true }, { 0, 160, 2048 } },
                { "direct", { .mRelief = true }, { 0, 144, 2048 } },
                { "direct", { .mMaps = false }, { 0, 96, 2048 } },
                { "media", {}, { 0, 176, 2048 } },
                { "panes", {}, { 0, 256, 2048 } },
                { "panes", { .mMaps = false }, { 0, 192, 2048 } },
                { "panes", { .mSea = false, .mRelief = true }, { 0, 192, 2048 } },
                { "bounce", { .mMaps = true }, { 0, 112, 2048 } },
                { "bounce", { .mMaps = false }, { 0, 80, 2048 } },
                { "landing", {}, { 0, 96, 2048 } },
                { "unbounced", {}, { 0, 80, 2048 } },
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
                    .mSea = (bits & 1u) != 0,
                    .mMaps = (bits & 2u) != 0,
                    .mRelief = (bits & 4u) != 0,
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
                    std::int32_t alike = 0;
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

            // **No ceiling lets a lane hold scratch the device would warn of** at a start
            // (`StatisticNames::mScratchFreeBytes`): what the gate passes, a player's log is quiet about.
            const std::uint64_t scratchFree = getDevice().getPhysicalDevice().getDriver().mStatistics.mScratchFreeBytes;
            EXPECT_LE(measured->mRule.mScratch, scratchFree) << "the rule's scratch";
            for (const Row& row : measured->mRows)
            {
                EXPECT_LE(row.mCeiling.mScratch, scratchFree) << "the scratch of a row of `" << row.mName << "`";
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

        /// **The sun is the frame's and not the tuple's**: the water's walk runs where the tuple has a sea, sun or
        /// none; the shafts through it (`media`) where it has a sea and the sun is up, and nowhere else; every other
        /// stage whatever the frame. A tuple's kernels are made for the sun up, so its media kernel is made with its
        /// sea.
        TEST_F(RtxVisibilityKernelsTest, theSunDecidesTheShaftsAndTheSeaTheWaterAndNothingElse)
        {
            using Kernel = VisibilityPass::Kernel;
            const VisibilityVariant sea{ .mSea = true };
            const VisibilityVariant dry{ .mSea = false };
            for (const bool sun : { false, true })
            {
                SCOPED_TRACE(sun);
                EXPECT_TRUE(VisibilityPass::runsStage(Kernel::Water, sea, sun));
                EXPECT_FALSE(VisibilityPass::runsStage(Kernel::Water, dry, sun));
                EXPECT_EQ(VisibilityPass::runsStage(Kernel::Media, sea, sun), sun);
                EXPECT_FALSE(VisibilityPass::runsStage(Kernel::Media, dry, sun));
                for (const Kernel always : { Kernel::Visibility, Kernel::Direct, Kernel::Panes, Kernel::Bounce,
                         Kernel::Landing, Kernel::Unbounced })
                {
                    EXPECT_TRUE(VisibilityPass::runsStage(always, dry, sun));
                    EXPECT_TRUE(VisibilityPass::runsStage(always, sea, sun));
                }
            }
        }

        /// **Tuples a stage reads alike share its pipeline** (`VisibilityPass::canonicalOf`): the unbounced
        /// stage reads the sea, so a tuple without one runs a pipeline of its own; the eye's stage reads a relief,
        /// so a tuple without one does too, and the maps in every tuple, so a tuple without them runs the pipeline
        /// with them. Of the stage pipelines listed, 7 are shared: the media stage reads neither the maps nor a
        /// relief, so its three tuples, each with a sea, run one pipeline; the landing reads no normal map and so no
        /// relief (`NORMAL_MAP_UNREAD`), so its four tuples with the maps run two; the eye's stage's two tuples with
        /// neither, one with a sea and one without, run their tuples' with the maps; and the water's stage's tuple
        /// with neither runs the one with the maps.
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
            EXPECT_NE(of("unbounced", VisibilityVariant{ .mSea = false }), whole);
            const VisibilityVariant flat{ .mRelief = false };
            EXPECT_NE(of("visibility", flat), of("visibility", full));
            EXPECT_EQ(of("visibility", VisibilityVariant{ .mMaps = false, .mRelief = false }), of("visibility", flat));
            EXPECT_NE(of("visibility", flat), nullptr);

            std::vector<const Pipeline*> distinct;
            distinct.reserve(kernels.size());
            for (const VisibilityPass::NamedKernel& kernel : kernels)
                distinct.push_back(kernel.mPipeline);
            std::ranges::sort(distinct);
            const auto [first, last] = std::ranges::unique(distinct);
            distinct.erase(first, last);
            EXPECT_EQ(kernels.size() - distinct.size(), 7u) << "the stage pipelines shared tuples save";
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
