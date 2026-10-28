#include "visibilitypass.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <volk.h>

#include <components/rtx/common/parallel.hpp>
#include <components/rtx/environment/wavecascade.hpp>
#include <components/rtx/frame/bluenoise.hpp>
#include <components/rtx/frame/camera.hpp>
#include <components/rtx/frame/specularalbedo.hpp>
#include <components/rtx/renderer/framezone.hpp>
#include <components/rtx/scene/lightgrid.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/shaders/camera.h>
#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/scene.h>
#include <components/rtx/shaders/sky.h>
#include <components/rtx/shaders/wave.h>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/device/validation.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/pipeline/pipeline.hpp>
#include <components/rtxvulkan/scene/devicescene.hpp>
#include <components/rtxvulkan/scene/scenebuffers.hpp>
#include <components/rtxvulkan/shaders/shared/bindings.h>
#include <components/rtxvulkan/shaders/shared/fogvolume.h>
#include <components/rtxvulkan/shaders/shared/tables.h>
#include <components/rtxvulkan/shaders/shared/traceconstants.h>

#include "fogvolume.hpp"
#include "gbuffer.hpp"
#include "ripplepass.hpp"
#include "spritebin.hpp"
#include "tracemedia.hpp"
#include "tuplequeue.hpp"
#include "wavepass.hpp"

namespace Rtx
{
    namespace
    {
        /// Every trace module a kernel of the pass runs, which its shader code reads once.
        constexpr std::array<std::string_view, 11> sTraceModules{ "visibility.comp.spv", "direct.comp.spv",
            "media.comp.spv", "panes.comp.spv", "bounce.comp.spv", "landing.comp.spv", "fogscatter.rgen.spv",
            "fogdepth.rgen.spv", "spritecomposite.rgen.spv", "spriteshelter.rgen.spv", "spriteemitters.rgen.spv" };

        /// The words of the whole tuple, which the kernels made once and not per tuple run under:
        /// the sun, the moons, the sea and the maps all standing, and nothing counted.
        constexpr std::array<std::uint32_t, Shaders::SPEC_COUNT> sWholeSky = [] {
            std::array<std::uint32_t, Shaders::SPEC_COUNT> words{};
            words[Shaders::SPEC_HAS_SUN] = 1u;
            words[Shaders::SPEC_HAS_MOONS] = 1u;
            words[Shaders::SPEC_HAS_SEA] = 1u;
            words[Shaders::SPEC_HAS_MAPS] = 1u;
            return words;
        }();

        /// Whether every table has an address, and each is aligned as the reference that reads it
        /// declares. Debug-only, through the assert that calls it.
        [[maybe_unused]] bool everyTableAddressed(const Shaders::GpuTables& tables)
        {
            const auto at
                = [](std::uint64_t address, std::uint32_t align) { return address != 0 && address % align == 0; };

            return at(tables.mNormalBlocks, Shaders::TABLE_ALIGN_BLOCKS)
                && at(tables.mTangentBlocks, Shaders::TABLE_ALIGN_BLOCKS)
                && at(tables.mTexCoordBlocks, Shaders::TABLE_ALIGN_BLOCKS)
                && at(tables.mColourBlocks, Shaders::TABLE_ALIGN_BLOCKS)
                && at(tables.mPositionBlocks, Shaders::TABLE_ALIGN_BLOCKS)
                && at(tables.mIndexBlocks, Shaders::TABLE_ALIGN_BLOCKS) && at(tables.mMeshes, Shaders::TABLE_ALIGN_ROWS)
                && at(tables.mInstances, Shaders::TABLE_ALIGN_ROWS)
                && at(tables.mMaterials, Shaders::TABLE_ALIGN_MATERIALS)
                && at(tables.mLayers, Shaders::TABLE_ALIGN_LAYERS) && at(tables.mMasks, Shaders::TABLE_ALIGN_ROWS)
                && at(tables.mLights, Shaders::TABLE_ALIGN_ROWS) && at(tables.mLightCells, Shaders::TABLE_ALIGN_ROWS)
                && at(tables.mLightList, Shaders::TABLE_ALIGN_ROWS) && at(tables.mBlueNoise, Shaders::TABLE_ALIGN_ROWS)
                && at(tables.mSpecularAlbedo, Shaders::TABLE_ALIGN_ROWS)
                && at(tables.mSprites, Shaders::TABLE_ALIGN_ROWS) && at(tables.mEmitters, Shaders::TABLE_ALIGN_ROWS)
                && at(tables.mTextureTexels, Shaders::TABLE_ALIGN_ROWS)
                && at(tables.mEmitterFrames, Shaders::TABLE_ALIGN_ROWS)
                && at(tables.mSpriteTileList, Shaders::TABLE_ALIGN_ROWS)
                && at(tables.mSpritePresence, Shaders::TABLE_ALIGN_ROWS);
        }

        /// Every stage on every binding, because one description of set zero serves the trace's
        /// launches and the fog volume's dispatch.
        constexpr auto sStages = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR;
        constexpr auto sStorage = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;

        /// Whether a deck that is drawn names a sheet at both ends of its blend. The shader mixes
        /// the two unconditionally — `CloudDeck::mBlend` — so a deck with a sheet and no sheet
        /// ahead is a read of the bindless array at a slot nothing holds, which loses the device.
        /// Debug-only, through the assert that calls it.
        [[maybe_unused]] bool deckNamesBothSheets(const Shaders::CloudDeck& deck)
        {
            return deck.mTexture == Shaders::NO_TEXTURE || deck.mNext != Shaders::NO_TEXTURE;
        }

        /// The structure, the hit counter, the frame itself, the sea and the fog's field, in the
        /// order the shader declares them. The tables a hit reads are in `GpuTables`; the channels
        /// the trace writes are `GBuffer`'s set.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::BIND_COUNT> sBindings = [] {
            std::array<VkDescriptorSetLayoutBinding, Shaders::BIND_COUNT> declared{};
            declared[Shaders::BIND_SCENE] = VkDescriptorSetLayoutBinding{ Shaders::BIND_SCENE,
                VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1, sStages, nullptr };

            // The two storage buffers left: every table the shader reads travels as an address in
            // the frame block, and neither the hit counter — a harness facility — nor the glare
            // fader's query has a table to ride in.
            declared[Shaders::BIND_COUNTS]
                = VkDescriptorSetLayoutBinding{ Shaders::BIND_COUNTS, sStorage, 1, sStages, nullptr };
            declared[Shaders::BIND_SUN_GLARE]
                = VkDescriptorSetLayoutBinding{ Shaders::BIND_SUN_GLARE, sStorage, 1, sStages, nullptr };

            declared[Shaders::BIND_FRAME] = VkDescriptorSetLayoutBinding{ Shaders::BIND_FRAME,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, sStages, nullptr };

            for (const std::uint32_t binding : { Shaders::BIND_WAVE_SURFACE, Shaders::BIND_WAVE_CURVATURE })
                declared[binding] = VkDescriptorSetLayoutBinding{ binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    Shaders::WAVE_CASCADES, sStages, nullptr };

            for (const std::uint32_t binding : { Shaders::BIND_RIPPLE_SURFACE, Shaders::BIND_RIPPLE_CURVATURE })
                declared[binding] = VkDescriptorSetLayoutBinding{ binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                    sStages, nullptr };

            // One volume rather than a cascade of tiles: the air has no near band and no far one, it
            // has a field read at three scales.
            declared[Shaders::BIND_FOG_FIELD] = VkDescriptorSetLayoutBinding{ Shaders::BIND_FOG_FIELD,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, sStages, nullptr };

            return declared;
        }();

        /// The composite's set: every launch's, and the frame as shown, which it composites the
        /// puffs over. Pushed with the rest because that launch reads everything else in it — the
        /// block, the bin through it, the air.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::BIND_COUNT + 1> sCompositeBindings = [] {
            std::array<VkDescriptorSetLayoutBinding, Shaders::BIND_COUNT + 1> declared{};
            std::ranges::copy(sBindings, declared.begin());
            declared[Shaders::BIND_SHOWN] = VkDescriptorSetLayoutBinding{ Shaders::BIND_SHOWN,
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, sStages, nullptr };
            return declared;
        }();

        /// A table made once on the host — the sampler's tile, the lobe's integrals — for the life
        /// of the pass, in a submit of its own.
        Buffer uploadOnce(const Device& device, std::span<const float> values, std::string_view name)
        {
            Batch batch(device.getPool());
            Buffer table = uploadBuffer(batch, values, VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, name);
            batch.flush();
            return table;
        }

        /// Dispatches the bound stage over `list` (`STAGE_LIST_*`, `STAGE_DISPATCH_LANDING`): the list's
        /// indirect dispatch, which the stage before counted up as it appended.
        void dispatchOver(VkCommandBuffer commands, const GBuffer& channels, const std::uint32_t list)
        {
            const Buffer& tiles = channels.getStageTiles();
            tiles.nameForNext();
            vkCmdDispatchIndirect(commands, tiles.getHandle(), VkDeviceSize{ 4u * list } * sizeof(std::uint32_t));
        }

        /// One of the trace's stages: its module, the name its kernels are made under, the zone it is
        /// timed in, and the list it runs over, or none for one over every pixel.
        struct Stage
        {
            std::string_view mModule;
            std::string_view mName;
            FrameZone mZone;
            std::optional<std::uint32_t> mList;
        };

        /// **The trace's stages in the order a frame runs them**, a row at each stage's `Kernel`
        /// (`docs/rtx/architecture.md`, "A stage a kernel, in one fixed order"), every one a dispatch
        /// and not a launch, since every ray it casts is a ray query (`visibility.comp`). The trace
        /// over every pixel, which lists the tiles the shafts and the layers stand in; the solid's
        /// direct light; the sun's shafts through the water, over their tiles, where `hasMedia`; the
        /// layers laid over that light, over theirs; the bounce, which lists the pixels whose bounce
        /// landed; and its landing, over that list.
        constexpr std::array<Stage, 6> sTraceStages{ {
            { "visibility.comp.spv", "visibility", FrameZone::Trace, std::nullopt },
            { "direct.comp.spv", "direct", FrameZone::Direct, std::nullopt },
            { "media.comp.spv", "media", FrameZone::Media, Shaders::STAGE_LIST_MEDIA },
            { "panes.comp.spv", "panes", FrameZone::Panes, Shaders::STAGE_LIST_PANES },
            { "bounce.comp.spv", "bounce", FrameZone::Bounce, std::nullopt },
            { "landing.comp.spv", "landing", FrameZone::Landing, Shaders::STAGE_DISPATCH_LANDING },
        } };
    }

    VisibilityVariant VisibilityVariant::resolve(
        const Shaders::VisibilityConstants& frame, const bool sea, const bool mapped)
    {
        // A moon that is drawn and a moon that lights are two facts, and the sky needs the first
        // where no surface asks for the second: the game fades both out over the hours around dawn,
        // and a disc still on its way down lights nothing.
        bool moons = false;
        for (const Shaders::MoonDisc& moon : frame.mMoons)
            moons = moons || moon.mAlpha > 0.0f || moon.mSource.mIrradiance != Shaders::vec3();

        return VisibilityVariant{
            // Nought exactly where the sun is not up, and it fades to that across dusk rather than
            // stepping — `VisibilityConstants::mSun`'s irradiance says why there is no second
            // field.
            .mSun = frame.mSun.mIrradiance != Shaders::vec3(),
            .mMoons = moons,

            .mSea = sea,
            .mMaps = mapped,
        };
    }

    std::uint32_t VisibilityVariant::index() const
    {
        return (mSun ? 1u : 0u) | (mMoons ? 2u : 0u) | (mSea ? 4u : 0u) | (mMaps ? 8u : 0u);
    }

    std::string VisibilityVariant::describe(const std::string_view kernel) const
    {
        std::string name(kernel);
        if (mSun)
            name += " sun";
        if (mMoons)
            name += " moons";
        if (mSea)
            name += " sea";
        if (mMaps)
            name += " maps";
        return name;
    }

    VisibilityPass::VisibilityPass(const Device& device, const SetLayout& textureLayout, const SetLayout& channelLayout,
        const SetLayout& volumeLayout, bool counting, const bool specialize)
        : mDevice(device)
        , mBlueNoise(uploadOnce(device, BlueNoise::shared().getValues(), "blue noise"))
        , mSpecularAlbedo(uploadOnce(device, SpecularAlbedo::shared().getValues(), "specular albedo"))
        , mConstants(Buffer::deviceLocal(device, sizeof(Shaders::VisibilityConstants),
              VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, "frame constants"))
        , mCounting(counting ? 1u : 0u)
        , mSpecialize(specialize)
        , mChannelLayout(&channelLayout)
        , mVolumeLayout(&volumeLayout)
    {
        compileEvery(textureLayout);
    }

    void VisibilityPass::compileEvery(const SetLayout& textureLayout)
    {
        // Queued after the full tuple, which takes seconds a kernel where these take tens of
        // milliseconds: a hand takes up whatever is next, and a tuple taken last is the whole batch
        // waiting on one hand.
        constexpr std::array singles{ Kernel::Depth, Kernel::Integrate, Kernel::SpriteComposite, Kernel::SpriteShelter,
            Kernel::SpriteEmitters };

        // **The full tuple first, on every hand**, which every frame draws with until its own is made:
        // its constants are all true and the shaders' own tests answer the rest. Its froxels' launch
        // as well, which answers for every tuple whose own is not made yet (`scatterPipelineFor`).
        static_assert(
            sTraceStages.size() == sStageCount && static_cast<std::size_t>(Kernel::Landing) + 1 == sStageCount,
            "a stage with no row, or a row with no stage");
        const VisibilityVariant full{};
        std::vector<Wanted> drawing;
        drawing.reserve(sStageCount + 1 + singles.size());
        for (std::size_t stage = 0; stage < sStageCount; ++stage)
            drawing.push_back(Wanted{ .mKernel = static_cast<Kernel>(stage), .mVariant = full });
        drawing.push_back(Wanted{ .mKernel = Kernel::Scatter, .mVariant = full });
        for (const Kernel single : singles)
            drawing.push_back(Wanted{ .mKernel = single });

        // So that a hand's validation error reaches whoever asked for these pipelines. The
        // layers report on the thread that made the call, and the log files by thread because the
        // test binary runs tests in parallel against one of them — an error left filed under a
        // hand is one nobody ever collects.
        const std::thread::id caller = std::this_thread::get_id();

        const auto count = static_cast<std::uint32_t>(drawing.size());
        mCompiling.start("kernel compile", count,
            [this, &textureLayout, caller, drawing = std::move(drawing)](const Platform::StopToken& stop, Job& job) {
                mCode.emplace(mDevice, sTraceModules);
                runInParallel(
                    "compile hand", drawing.size(), Hands::Every, stop, [caller] { return AdoptedThread(caller); },
                    [&](const std::size_t at) {
                        compile(drawing[at], textureLayout);
                        job.advance();
                    });

                // A table stopped half way is not one to answer `kernels` with.
                if (stop.stopRequested())
                    throw std::runtime_error("the kernel compile was stopped");
                if (!mSpecialize)
                    mCode.reset();
            });

        if (!mSpecialize)
            return;

        // **Every other tuple behind it, on half the hands**, each made whole before a frame may take
        // it (`variantFor`): the game runs meanwhile, and half the processors are its. After the full
        // tuple, whose compile they would slow. **A kernel a turn**, so a stop waits for one compile
        // and not a tuple's six or seven, and a tuple made when its last kernel lands.
        mSpecializing.start("tuple compile", VisibilityVariant::sCount - 1,
            [this, &textureLayout, caller](const Platform::StopToken& stop, Job& job) {
                while (!mCompiling.waitFor(std::chrono::milliseconds(100)).isDone())
                    if (stop.stopRequested())
                        throw std::runtime_error("the tuple compile was stopped");

                // The sky's tuples after the dark ones: the order matters less than the tuple a
                // frame waits on, which the queue takes first.
                std::vector<Wanted> kernels;
                std::vector<TupleRun> runs;
                for (const bool sun : { false, true })
                    for (const bool moons : { false, true })
                        for (const bool sea : { false, true })
                            for (const bool maps : { false, true })
                            {
                                const VisibilityVariant tuple{
                                    .mSun = sun, .mMoons = moons, .mSea = sea, .mMaps = maps
                                };
                                if (sun && moons && sea && maps)
                                    continue;

                                const std::size_t first = kernels.size();
                                for (std::size_t stage = 0; stage < sStageCount; ++stage)
                                    if (static_cast<Kernel>(stage) != Kernel::Media || hasMedia(tuple))
                                        kernels.push_back(
                                            Wanted{ .mKernel = static_cast<Kernel>(stage), .mVariant = tuple });
                                if (!tuple.mMaps)
                                    kernels.push_back(Wanted{ .mKernel = Kernel::Scatter, .mVariant = tuple });
                                runs.push_back(TupleRun{ .mTuple = tuple.index(),
                                    .mItems = static_cast<std::uint32_t>(kernels.size() - first) });
                            }
                TupleQueue queue(runs, VisibilityVariant::sCount);

                // Each turn claims one kernel and every kernel is one turn's, so no turn finds none.
                runInParallel(
                    "tuple hand", queue.getItemCount(), Hands::Half, stop, [caller] { return AdoptedThread(caller); },
                    [&](std::size_t) {
                        const Wanted& wanted = kernels[queue.claim(mWanted.load(std::memory_order_relaxed))];
                        compile(wanted, textureLayout);
                        if (queue.land(wanted.mVariant.index()))
                        {
                            mMade[wanted.mVariant.index()].store(true, std::memory_order_release);
                            job.advance();
                        }
                    });

                if (stop.stopRequested())
                    throw std::runtime_error("the tuple compile was stopped");
                mCode.reset();
            });
    }

    void VisibilityPass::compile(const Wanted& wanted, const SetLayout& textureLayout)
    {
        const VisibilityVariant variant = wanted.mVariant;

        // One word per `constant_id`. The volume traces no primary ray and so adds no miss, but its
        // froxels are a boundary the finiteness count watches, so it counts under the same word as
        // the trace.
        std::array<std::uint32_t, Shaders::SPEC_COUNT> specialization{};
        specialization[Shaders::SPEC_COUNTING] = mCounting;
        specialization[Shaders::SPEC_HAS_SUN] = variant.mSun ? 1u : 0u;
        specialization[Shaders::SPEC_HAS_MOONS] = variant.mMoons ? 1u : 0u;
        specialization[Shaders::SPEC_HAS_SEA] = variant.mSea ? 1u : 0u;
        specialization[Shaders::SPEC_HAS_MAPS] = variant.mMaps ? 1u : 0u;

        switch (wanted.mKernel)
        {
            case Kernel::Visibility:
            case Kernel::Direct:
            case Kernel::Media:
            case Kernel::Panes:
            case Kernel::Bounce:
            case Kernel::Landing:
            {
                const auto at = static_cast<std::size_t>(wanted.mKernel);
                const Stage& stage = sTraceStages[at];
                mKernels.mStages[at][variant.index()]
                    = std::make_unique<ComputePipeline<NoConstants>>(mDevice, sBindings, sharedSets(textureLayout),
                        *mCode, stage.mModule, variant.describe(stage.mName), specialization, SubgroupSize::ThirtyTwo);
                return;
            }
            case Kernel::Scatter:
                mKernels.mScatter[variant.index()]
                    = std::make_unique<TracePipeline<NoConstants>>(mDevice, sBindings, sharedSets(textureLayout),
                        *mCode, "fogscatter.rgen.spv", variant.describe("fog scatter"), specialization);
                return;
            // From here on no tuple: each is made once, at the whole sky and counting nothing. **The
            // whole sky computes what each tuple would**, since a constant a tuple sets false stands in
            // front of a runtime test that already answers no (`variants.glsl`), and what a tuple
            // would take out is a few hundredths of a millisecond of puffs and emitters, against a
            // pipeline more each to compile cold. Handed those words and not left to the GLSL's
            // defaults, which say the same, so that no constant a module reads takes a default in
            // silence (`specializationDisagreement`).
            case Kernel::Depth:
                mKernels.mDepth = std::make_unique<TracePipeline<NoConstants>>(
                    mDevice, sBindings, sharedSets(textureLayout), *mCode, "fogdepth.rgen.spv", "fog depth", sWholeSky);
                return;
            case Kernel::Integrate:
                mKernels.mIntegrate = std::make_unique<ComputePipeline<NoConstants>>(
                    mDevice, sBindings, sharedSets(textureLayout), "fogintegrate.comp.spv", "fog integrate", sWholeSky);
                return;
            case Kernel::SpriteComposite:
                mKernels.mSpriteComposite
                    = std::make_unique<TracePipeline<Shaders::PuffConstants>>(mDevice, sCompositeBindings,
                        sharedSets(textureLayout), *mCode, "spritecomposite.rgen.spv", "sprite composite", sWholeSky);
                return;
            case Kernel::SpriteShelter:
                mKernels.mSpriteShelter = std::make_unique<TracePipeline<NoConstants>>(mDevice, sBindings,
                    sharedSets(textureLayout), *mCode, "spriteshelter.rgen.spv", "sprite shelter", sWholeSky);
                return;
            case Kernel::SpriteEmitters:
                mKernels.mSpriteEmitters = std::make_unique<TracePipeline<NoConstants>>(mDevice, sBindings,
                    sharedSets(textureLayout), *mCode, "spriteemitters.rgen.spv", "sprite emitters", sWholeSky);
                return;
        }
    }

    JobProgress VisibilityPass::awaitKernels(const KernelSet set, const std::chrono::milliseconds patience) const
    {
        const std::chrono::steady_clock::time_point until = std::chrono::steady_clock::now() + patience;
        const JobProgress drawing = mCompiling.waitFor(patience);
        if (set == KernelSet::Drawing || !mSpecialize)
            return drawing;

        const std::uint32_t tuples = VisibilityVariant::sCount - 1;
        if (!drawing.isDone())
            return JobProgress{ .mMade = drawing.mMade, .mCount = drawing.mCount + tuples };

        const auto left
            = std::chrono::duration_cast<std::chrono::milliseconds>(until - std::chrono::steady_clock::now());
        const JobProgress made = mSpecializing.waitFor(std::max(left, std::chrono::milliseconds::zero()));
        return JobProgress{ .mMade = drawing.mCount + made.mMade, .mCount = drawing.mCount + made.mCount };
    }

    void VisibilityPass::listKernels(std::vector<NamedKernel>& out) const
    {
        const Kernels& made = kernels();
        if (mSpecialize)
            mSpecializing.wait();
        for (std::size_t stage = 0; stage < sStageCount; ++stage)
            for (const std::unique_ptr<ComputePipeline<NoConstants>>& kernel : made.mStages[stage])
                if (kernel != nullptr)
                    out.push_back(NamedKernel{ sTraceStages[stage].mName, kernel.get() });
        for (const std::unique_ptr<TracePipeline<NoConstants>>& kernel : made.mScatter)
            if (kernel != nullptr)
                out.push_back(NamedKernel{ "fog scatter", kernel.get() });
        out.push_back(NamedKernel{ "fog depth", made.mDepth.get() });
        out.push_back(NamedKernel{ "sprite composite", made.mSpriteComposite.get() });
        out.push_back(NamedKernel{ "sprite shelter", made.mSpriteShelter.get() });
        out.push_back(NamedKernel{ "sprite emitters", made.mSpriteEmitters.get() });
        out.push_back(NamedKernel{ "fog integrate", made.mIntegrate.get() });
    }

    const VisibilityPass::Kernels& VisibilityPass::kernels() const
    {
        mCompiling.wait();
        return mKernels;
    }

    bool VisibilityPass::isMade(const VisibilityVariant tuple) const
    {
        return mMade[tuple.index()].load(std::memory_order_acquire);
    }

    VisibilityVariant VisibilityPass::variantFor(
        const VisibilityInputs& inputs, const Shaders::VisibilityConstants& constants) const
    {
        const VisibilityVariant wanted
            = VisibilityVariant::resolve(constants, inputs.mSubject.mSea, inputs.mSubject.mMapped);
        if (isMade(wanted))
            return wanted;

        mWanted.store(wanted.index(), std::memory_order_relaxed);
        return VisibilityVariant{};
    }

    ChannelWrites VisibilityPass::writesOf(const VisibilityVariant run, const bool puffs)
    {
        return ChannelWrites{ .mLobe = run.mMaps, .mPuffs = puffs };
    }

    const ComputePipeline<NoConstants>& VisibilityPass::kernelOf(
        const TupleKernels& stage, const VisibilityVariant variant)
    {
        const std::unique_ptr<ComputePipeline<NoConstants>>& held = stage[variant.index()];
        assert(held != nullptr && "a tuple `compileEvery` did not make");

        return *held;
    }

    bool VisibilityPass::hasMedia(const VisibilityVariant variant)
    {
        return variant.mSun && variant.mSea;
    }

    const TracePipeline<NoConstants>& VisibilityPass::scatterPipelineFor(VisibilityVariant variant) const
    {
        variant.mMaps = false;
        const std::uint32_t at = isMade(variant) ? variant.index() : VisibilityVariant{}.index();
        const std::unique_ptr<TracePipeline<NoConstants>>& held = kernels().mScatter[at];
        assert(held != nullptr && "a tuple `compileEvery` made no scatter kernel for");

        return *held;
    }

    SharedSetLayouts VisibilityPass::sharedSets(const SetLayout& textureLayout) const
    {
        return SharedSetLayouts{ .mTextures = &textureLayout, .mChannels = mChannelLayout, .mVolume = mVolumeLayout };
    }

    void VisibilityPass::writeConstants(VkCommandBuffer commands, const Shaders::VisibilityConstants& described) const
    {
        // A few hundred bytes, so an inline write that runs in queue order, against the launches
        // and the dispatch that read the block as a uniform.
        mConstants.updateInline(commands, Use::sBufferUniformRead, std::as_bytes(std::span(&described, 1)));
    }

    void VisibilityPass::pushInputs(
        VkCommandBuffer commands, const Pipeline& pipeline, const VisibilityInputs& inputs, const Image* shown) const
    {
        assert(inputs.mSubject.mCounts != nullptr && "a launch handed no census");
        const GBuffer& buffer = inputs.mChannels;
        const Buffer& counts = *inputs.mSubject.mCounts;

        const VkAccelerationStructureKHR scene = inputs.mSubject.mScene->getAcceleration().getTopLevel();
        const VkWriteDescriptorSetAccelerationStructureKHR sceneWrite{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
            .pNext = nullptr,
            .accelerationStructureCount = 1,
            .pAccelerationStructures = &scene,
        };
        DescriptorWrites writes(pipeline);
        writes.structure(Shaders::BIND_SCENE, sceneWrite);

        // The two buffers still bound: the hit counter, and the frame block every table is reached
        // through. Nothing bound here may be nothing: a null handle at the dispatch is undefined
        // and cost this renderer a device before the layers were asked.
        assert(!counts.isEmpty() && !mConstants.isEmpty() && "an input bound as nothing");
        writes.buffer(Shaders::BIND_COUNTS, counts.describe());
        writes.buffer(Shaders::BIND_FRAME, mConstants.describe());

        inputs.mSubject.mMedia->bindInto(writes);

        assert(inputs.mSubject.mSunGlare != nullptr && !inputs.mSubject.mSunGlare->isEmpty()
            && "a trace with no glare query to count into");
        writes.buffer(Shaders::BIND_SUN_GLARE, inputs.mSubject.mSunGlare->describe());

        if (shown != nullptr)
        {
            assert(!shown->isEmpty() && "a composite over no frame");
            writes.image(Shaders::BIND_SHOWN, shown->describeStorage());
        }

        pushDescriptors(commands, pipeline, writes);

        // The three sets nothing pushes: the bindless textures a scene brought, the channels the
        // trace writes, and the air in front of the camera. Each is written when what it names is
        // made, and bound as it is.
        bindSets(commands, pipeline,
            SharedSetBinds{ .mTextures = inputs.mSubject.mScene->getTextures(),
                .mChannels = buffer.getSet(),
                .mVolume = inputs.mFogVolume.getSet() });
    }

    void VisibilityPass::writeFrame(VkCommandBuffer commands, const VisibilityInputs& inputs,
        const SpriteTables& sprites, const Shaders::VisibilityConstants& constants) const
    {
        assert(inputs.mSubject.mScene != nullptr && inputs.mSubject.mMedia != nullptr
            && "a trace of no scene, or in no media");

        Shaders::VisibilityConstants described = constants;

        described.mScreen = screenBasisOf(described.mEyes.mWorld.mBasis);
        described.mPreviousScreen = screenBasisOf(described.mPrevious);
        inputs.mSubject.mMedia->describeFrame(described);

        // And where the lamps were binned, off the tables the placement built, for the same reason.
        const LightGrid& lamps = inputs.mSubject.mScene->getBuffers().getLightGrid();
        described.mLightGrid = Shaders::GpuLightGrid{
            .mOrigin = lamps.getOrigin(),
            .mInverseCell = lamps.getInverseCell(),
            .mSize = lamps.getSize(),
        };

        // And how many froxels stand in front of the camera, off the volume that holds them, so the
        // three shaders that divide by it stop asking the driver for a number the host already has.
        described.mFogColumns = Shaders::uvec2(inputs.mFogVolume.getColumns(), inputs.mFogVolume.getRows());
        const VkExtent2D traced
            = inputs.mFogVolume.tracedFor(VkExtent2D{ described.mEyes.mWorld.mWidth, described.mEyes.mWorld.mHeight });
        described.mFogTraced = Shaders::uvec2(traced.width, traced.height);

        // And where every table is. Every address read here names a buffer that is alive when the
        // trace runs, because the placement buried what it displaced in the graveyard and nothing
        // between here and the submit grows a table.
        inputs.mSubject.mScene->describeTables(inputs.mSubject.mScene->getSlot(), described.mTables);
        described.mTables.mBlueNoise = mBlueNoise.addressFor();
        described.mTables.mSpecularAlbedo = mSpecularAlbedo.addressFor();

        // The trace's own, shaded and binned for this camera ahead of it (`SpriteTables`).
        described.mTables.mSprites = sprites.mSprites;
        described.mTables.mEmitterFrames = sprites.mEmitterFrames;
        described.mTables.mSpriteTileList = sprites.mTileList;
        described.mTables.mSpritePresence = sprites.mPresence;
        described.mPuffsInFrame = sprites.mPuffs ? 1u : 0u;

        // Nothing addressed here may be nothing, and every address must be what its reference
        // claims. A descriptor bound as a null handle cost this renderer a device with no message;
        // an address of nought or one off its claimed alignment is the same mistake one step later,
        // and the device says even less about it.
        assert(everyTableAddressed(described.mTables) && "a table addressed as nothing, or not as its block declares");
        assert(deckNamesBothSheets(described.mClouds) && "a deck drawn from one sheet and no sheet ahead");

        writeConstants(commands, described);
    }

    void VisibilityPass::recordSpriteShelter(const VkCommandBuffer commands, const VisibilityInputs& inputs,
        const Shaders::VisibilityConstants& constants, const std::uint32_t count, GpuTimer* const timer) const
    {
        // Nearly every frame: nothing falls, or what falls is the kind a roof does not stop. A
        // frame with sprites and no shelter pays no launch for it.
        if (constants.mShelterHeight <= 0.0f || count == 0)
            return;

        const GpuZone timed(timer, commands, FrameZone::Shelter);

        const auto& shelter = *kernels().mSpriteShelter;
        bind(commands, shelter);
        pushInputs(commands, shelter, inputs);

        // One invocation a sprite, over the bin's own copy of the list.
        shelter.traceRays(commands, count, 1);

        // The shade reads and writes what this zeroed, from a dispatch.
        handOver(commands, Use::sBufferShaderReadWrite, Use::sBufferComputeReadWrite);
    }

    void VisibilityPass::recordSpriteEmitters(const VkCommandBuffer commands, const VisibilityInputs& inputs,
        const std::uint32_t count, GpuTimer* const timer) const
    {
        if (count == 0)
            return;

        const GpuZone timed(timer, commands, FrameZone::Emitters);

        const auto& emitters = *kernels().mSpriteEmitters;
        bind(commands, emitters);
        pushInputs(commands, emitters, inputs);
        emitters.traceRays(commands, count, 1);

        // Read by the trace and by the puffs' composite, both launches.
        handOver(commands, Use::sBufferShaderReadWrite, Use::sBufferShaderRead);
    }

    void VisibilityPass::record(VkCommandBuffer commands, const VisibilityInputs& inputs,
        const VisibilityVariant variant, const Shaders::VisibilityConstants& constants, GpuTimer* timer) const
    {
        assert(inputs.mChannels.getWidth() >= constants.mEyes.mWorld.mWidth
            && inputs.mChannels.getHeight() >= constants.mEyes.mWorld.mHeight);

        assert(inputs.mSubject.mScene != nullptr && inputs.mSubject.mMedia != nullptr
            && "a trace of no scene, or in no media");

        inputs.mFogVolume.begin(commands);

        const TracePipeline<NoConstants>& scatter = scatterPipelineFor(variant);

        const VkExtent2D traced
            = inputs.mFogVolume.tracedFor(VkExtent2D{ constants.mEyes.mWorld.mWidth, constants.mEyes.mWorld.mHeight });
        const std::uint32_t columns = traced.width;
        const std::uint32_t rows = traced.height;

        // Each zone ends before the barrier after it, so a barrier's wait is no zone's time.
        {
            const GpuZone timed(timer, commands, FrameZone::Air);

            // Where each column's ray stops, before anything is drawn along it. One ray a
            // column, and the froxels of the column keep their draws short of the answer.
            const auto& depth = *kernels().mDepth;
            bind(commands, depth);
            pushInputs(commands, depth, inputs);

            depth.traceRays(commands, columns, rows);

            inputs.mFogVolume.depthTaken(commands);

            // The set stays pushed across both launches. Each is addressed through the same layout
            // at the same bind point, so what was pushed for the first is still bound for the
            // second — and pushing set zero again would be six descriptor writes for a pass that
            // reads a handful of images out of another set.
            bind(commands, scatter);

            scatter.traceRays(commands, columns, rows, Shaders::FOG_VOLUME_SLICES);
        }

        inputs.mFogVolume.scattered(commands);

        {
            const GpuZone timed(timer, commands, FrameZone::Column);

            // The integrate pass is a dispatch and reads what the launches wrote, so it is handed
            // the set again at its own bind point.
            const auto& integrate = *kernels().mIntegrate;
            bind(commands, integrate);
            pushInputs(commands, integrate, inputs);

            vkCmdDispatch(commands, groupsFor(columns, Shaders::FOG_COLUMN_WORKGROUP),
                groupsFor(rows, Shaders::FOG_COLUMN_WORKGROUP), 1);
        }

        inputs.mFogVolume.handOver(commands);

        // Each field's half in whole groups of its own (`fieldGroupsAcross`), for every stage.
        const std::uint32_t across
            = 2 * Shaders::fieldGroupsAcross(constants.mEyes.mWorld.mWidth, Shaders::TRACE_WORKGROUP_WIDTH);
        const std::uint32_t down = groupsFor(constants.mEyes.mWorld.mHeight, Shaders::TRACE_WORKGROUP_HEIGHT);

        // Each stage behind everything the one before wrote, a zone each (`sTraceStages`).
        for (std::size_t at = 0; at < sStageCount; ++at)
        {
            if (static_cast<Kernel>(at) == Kernel::Media && !hasMedia(variant))
                continue;
            if (at > 0)
                GBuffer::handToStage(commands);

            const Stage& stage = sTraceStages[at];
            const GpuZone timed(timer, commands, stage.mZone);
            const ComputePipeline<NoConstants>& kernel = kernelOf(kernels().mStages[at], variant);
            bind(commands, kernel);
            pushInputs(commands, kernel, inputs);
            if (stage.mList.has_value())
                dispatchOver(commands, inputs.mChannels, *stage.mList);
            else
                vkCmdDispatch(commands, across, down, 1);
        }

        // The host's read of the count is ordered by whoever reads it: `renderFrame` records
        // `Buffer::orderForHostRead` after every pass that could add to it, and a picture's count
        // is read by nobody.
    }

    void VisibilityPass::recordSpriteComposite(const VkCommandBuffer commands, const VisibilityInputs& inputs,
        const Image& shown, const Shaders::Eyes& eyes, const VkExtent2D extent, GpuTimer* const timer) const
    {
        assert(extent.width <= shown.getWidth() && extent.height <= shown.getHeight()
            && "a picture larger than the image it is drawn into");

        // Its own zone and not the bin's `sprites`, so a report says what the march at the shown
        // extent costs apart from what binning the sprites over the traced one does.
        const GpuZone timed(timer, commands, FrameZone::Puffs);

        const auto& composite = *kernels().mSpriteComposite;
        bind(commands, composite);
        pushInputs(commands, composite, inputs, &shown);
        composite.push(commands,
            Shaders::PuffConstants{ .mShown = Shaders::Eyes{
                                        .mWorld = Shaders::cameraOnGrid(eyes.mWorld, extent.width, extent.height),
                                        .mArms = Shaders::cameraOnGrid(eyes.mArms, extent.width, extent.height),
                                    } });

        // One invocation a traced pixel, which composites the shown pixels over it —
        // `spritecomposite.rgen` says why.
        composite.traceRays(commands, eyes.mWorld.mWidth, eyes.mWorld.mHeight);
    }
}
