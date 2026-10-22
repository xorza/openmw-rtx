#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/common/job.hpp>
#include <components/rtx/common/jobprogress.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/shaders/visibility.h>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/frameslots.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/pipeline/pipeline.hpp>
#include <components/rtxvulkan/pipeline/shadercode.hpp>
#include <components/rtxvulkan/pipeline/tracepipeline.hpp>

#include "gbuffer.hpp"

namespace Rtx
{
    class Device;
    class DeviceScene;
    class FogVolume;
    class GpuTimer;
    class Image;
    struct SpriteTables;
    class TraceMedia;

    /// What one trace is of and where its results go but for the chain's own images: the scene
    /// and the media, which slot of the chain's state it takes, and the census and glare counts it
    /// sums into. What `TraceMedia::describe` answers, the same for a frame and a picture inside
    /// the interface but for the arguments.
    struct TraceSubject
    {
        /// What the rays meet, at the copy of its tables the scene's last placement wrote: the
        /// structure, the tables a hit reads through the frame block, and the texture array. Read
        /// where it is used, so no two launches of a trace can be handed two copies of it.
        const DeviceScene* mScene = nullptr;

        /// What every trace reads beside its scene: the sea, the wake in it and the fog's field. One
        /// for everything traced.
        const TraceMedia* mMedia = nullptr;

        /// Which of the chain's sprite bins this trace records into and reads: the frame's own
        /// slot in the world's chain, so the frame behind keeps its bin, and the first in the
        /// pictures' chain. Not which copy of the air it writes, which is a turn a trace
        /// (`FogVolume::turn`) and so stays paired across a frame that traced nothing.
        FrameSlot mTraceSlot;

        /// What the trace sums its census into: the frame's own, or for a picture inside the
        /// interface one nothing reads — bound because the shader writes it regardless.
        const Buffer* mCounts = nullptr;

        /// Whether this camera draws sprites. One that draws none still bins its tiles, with no
        /// sprite in them, so each learns which media and additive surfaces a ray through it can
        /// meet (`TraceChain::record`).
        bool mDrawsSprites = true;

        /// The two counts the eye's launch adds to: the frame's, `SunGlarePass::getCounts`, or
        /// for a picture inside the interface its own that nothing reads, as with the census.
        const Buffer* mSunGlare = nullptr;

        /// Whether this trace has a sea: a water surface in the scene the eye can meet, or a level
        /// the camera can be under, which a cell with no surface can still submerge the eye in.
        /// The one answer the synthesis, the ripple step and `HAS_SEA` all read, so the kernel
        /// never samples tiles that nothing wrote this frame.
        bool mSea = false;

        /// Whether the scene places a material that wears a map (`InstanceCounts::mMapped`), which
        /// is the only way a surface comes to have a lobe. The one answer `HAS_MAPS` and the glossy
        /// filter both read, so the filter runs over every frame whose kernel can reflect.
        bool mMapped = false;

        /// Whether the scene holds a lamp, which the shadow denoiser reads: a lamp's light is one
        /// of the two it shadows, the sky's source the other.
        bool mLamps = false;
    };

    /// What every launch of one trace binds: its subject, and the chain's channels and air, which
    /// `TraceChain::record` names as it makes this. Whole from the moment it is made, and one value
    /// handed to every launch of the trace and to the display after it, so the chain's images
    /// cannot be handed to one launch and left out of the next.
    struct VisibilityInputs
    {
        TraceSubject mSubject;

        /// Where the trace leaves its channels, all in `VK_IMAGE_LAYOUT_GENERAL` and at least as
        /// large as the camera. Channels and not a picture, because the indirect term has to
        /// survive to the filter with the albedo still divided out.
        const GBuffer& mChannels;

        /// Where the air in front of this camera is integrated, before the trace reads it. Sized to
        /// the camera, as the channels are, and so the chain's and not the pass's.
        const FogVolume& mFogVolume;
    };

    /// What a trace can be told at compile time, and so what keys a pipeline. Each is only ever
    /// false where the shader's own test already answers no, so a variant takes out dead code and
    /// never an answer. `lib/variants.glsl` says what each removes, and why that is the same
    /// arithmetic and not always the same bits.
    struct VisibilityVariant
    {
        bool mSun = true;
        bool mMoons = true;
        bool mSea = true;
        bool mMaps = true;

        /// What this frame is. `sea` is `TraceSubject::mSea`, for the reason given there, and
        /// `mapped` whether the scene places a material with a normal or a specular map, which is
        /// the scene's answer, because which materials a camera's rays meet is not known until they
        /// are traced.
        static VisibilityVariant resolve(const Shaders::VisibilityConstants& frame, bool sea, bool mapped);

        /// Which of the table's pipelines this tuple is.
        std::uint32_t index() const;

        /// What a capture calls this tuple of `kernel`.
        std::string describe(std::string_view kernel) const;

        /// How many tuples there are, and so how long the table is.
        static constexpr std::uint32_t sCount = 16;
    };

    /// One ray per pixel against the top-level structure. Everything it needs arrives at record
    /// time, so nothing is allocated per frame; the frame's own description lives in a buffer this
    /// owns because it outgrew what a push constant may carry.
    class VisibilityPass
    {
    public:
        /// Uploads the blue-noise tile and the lobe's table, which the pass owns because they belong
        /// to the sampler and to the surface model and not to the scene or the camera, and starts
        /// making the kernels, which it returns without: `kernels` waits for them.
        ///
        /// @param textureLayout the layout of the bindless array this will be handed at record
        ///        time, because a pipeline layout names every set it will ever see.
        /// @param channelLayout the same, for the set a `GBuffer` hands over.
        /// @param volumeLayout the same again, for the set a `FogVolume` hands over.
        /// @param counting whether the frame counts for the host — the hits and the values that
        ///        were not finite — a harness facility, specialized away rather than branched on.
        /// @param specialize whether to make a kernel per tuple behind the full tuple's, or the full
        ///        tuple's alone and answer every frame with it — `RenderProfile::mSpecializeKernels`.
        VisibilityPass(const Device& device, const SetLayout& textureLayout, const SetLayout& channelLayout,
            const SetLayout& volumeLayout, bool counting, bool specialize);

        /// Where the blue-noise tile is, named for the next submit: what the frame block carries,
        /// and the tone pass's dither draws from.
        VkDeviceAddress getBlueNoise() const { return mBlueNoise.addressFor(); }

        /// Waits `patience` at most for `set`'s kernels, and says how many are made — every tuple's
        /// counted as one; throws what making one threw. For a host drawing a loading screen while it
        /// waits. Recording needs no wait of its own: every launch reads its kernel through
        /// `kernels`, which waits for the full tuple, and a frame takes its own tuple only once it is
        /// made (`variantFor`).
        JobProgress awaitKernels(KernelSet set, std::chrono::milliseconds patience) const;

        /// A kernel the pass made, and which of its kernels it is.
        struct NamedKernel
        {
            std::string_view mName;
            const Pipeline* mPipeline = nullptr;
        };

        /// Every kernel the pass made, once the compile is over: for what is asked of a kernel and
        /// not of a frame, which is what its compiler made of it (`Device::compiledUseOf`).
        void listKernels(std::vector<NamedKernel>& out) const;

        /// Writes the frame's block: `constants` with what only the passes know filled in — the
        /// tiles' widths, the lamps' grid, the froxel grid and where every table is, the bin's
        /// sprites among them. Before every launch of this frame, `recordSpriteShelter` first,
        /// because that one runs before the bin and reads the block like the rest.
        ///
        /// @param sprites where this trace's sprites and tiles are, off the bin the chain recording
        ///        the trace owns and filled ahead of it.
        void writeFrame(VkCommandBuffer commands, const VisibilityInputs& inputs, const SpriteTables& sprites,
            const Shaders::VisibilityConstants& constants) const;

        /// Zeroes, in the bin's own table, every falling sprite that stands under a roof — one ray
        /// straight up apiece, `spriteshelter.rgen`. After `writeFrame` and the bin's `take`, and
        /// before its `record`, so the shade counts no sheltered drop as a layer and the bin lists
        /// none. Nothing for a frame the block says has no shelter in it.
        ///
        /// @param count how many sprites the bin took, which is the launch's width.
        void recordSpriteShelter(VkCommandBuffer commands, const VisibilityInputs& inputs,
            const Shaders::VisibilityConstants& constants, std::uint32_t count, GpuTimer* timer) const;

        /// Writes a `Shaders::GpuEmitterFrame` for each of the `count` emitters the bin took,
        /// `spriteemitters.rgen`. After `writeFrame`, whose block names the rows, and before the
        /// trace that reads them.
        void recordSpriteEmitters(
            VkCommandBuffer commands, const VisibilityInputs& inputs, std::uint32_t count, GpuTimer* timer) const;

        /// The tuple a frame of these inputs runs: the one its constants call for where it is made,
        /// and the full tuple until then. Asked once a frame, and handed to `writesOf` and `record`,
        /// so the two agree whichever tuple lands between them. Resolved from the constants this
        /// frame is about to be traced with, and from nothing kept between frames: a dusk moves the
        /// tuple and a doorway moves it again.
        VisibilityVariant variantFor(
            const VisibilityInputs& inputs, const Shaders::VisibilityConstants& constants) const;

        /// What `record` writes of the channels a trace may leave out, in `run`'s kernels: the
        /// lobe's where they hold the maps' code — a scene that wears a map, or the full tuple — and
        /// the puffs' where a puff can be met, `puffs` (`SpriteTables::mPuffs`, which `writeFrame`
        /// states as `mPuffsInFrame`).
        static ChannelWrites writesOf(VisibilityVariant run, bool puffs);

        /// Records the trace in `variant`'s kernels, which `variantFor` answered. After `writeFrame`,
        /// which is what every launch here reads.
        ///
        /// @param timer where the zones this records go, or nothing where nobody is counting.
        void record(VkCommandBuffer commands, const VisibilityInputs& inputs, VisibilityVariant variant,
            const Shaders::VisibilityConstants& constants, GpuTimer* timer) const;

        /// Composites the puffs over `shown`, in place, at the picture's own extent: the
        /// sprites' shape and the cloud shells marched there against the bin the trace binned over its
        /// own grid, and the sprites' light read off the layer the trace left in `Channel::Puffs`. After
        /// whatever denoised and upscaled the frame, because neither should touch a particle —
        /// `spritecomposite.rgen` says what an upscaler's overlay costs.
        ///
        /// @param shown the frame as it will be shown, in `GENERAL`.
        /// @param eyes the frame's eyes as the trace cast them, at the traced extent; the composite
        ///        is handed them on the shown grid.
        /// @param extent how much of `shown` the picture is, from its corner: the whole of a
        ///        frame's, and a picture's own size inside an image that may be larger. The block is
        ///        the one the trace wrote, so the bin is read from there.
        void recordSpriteComposite(VkCommandBuffer commands, const VisibilityInputs& inputs, const Image& shown,
            const Shaders::Eyes& eyes, VkExtent2D extent, GpuTimer* timer) const;

    private:
        /// The trace's stages first, in the order a frame runs them, each its own index into
        /// `Kernels::mStages`; the launches and the dispatches beside them after.
        enum class Kernel
        {
            Visibility,
            Direct,
            Media,
            Panes,
            Bounce,
            Landing,
            Scatter,
            Depth,
            Integrate,
            SpriteComposite,
            SpriteShelter,
            SpriteEmitters,
        };

        /// One kernel to make: which of the pass's launches, and for the two tables, which tuple.
        struct Wanted
        {
            Kernel mKernel = Kernel::Visibility;
            VisibilityVariant mVariant{};
        };

        /// Starts making every kernel this pass can ever need, on threads of their own, because the
        /// frame path must not be able to compile: the trace took 2.8 seconds on a cold cache, and
        /// a frame that stopped for one was `Xid 109, CTX SWITCH TIMEOUT` and a device reset. Off
        /// the caller's thread, because the full tuple takes ten seconds cold and every tuple a
        /// minute, and the window has to go on answering meanwhile. In parallel, because the
        /// driver's cache is internally synchronised, and `PipelineCache` outlives the process.
        void compileEvery(const SetLayout& textureLayout);

        /// Makes the one kernel `wanted` names, into its slot. On a hand of `compileEvery`'s, each
        /// writing a slot no other hand does.
        void compile(const Wanted& wanted, const SetLayout& textureLayout);

        /// The shared sets every kernel of the pass reads. A pipeline layout names every set it will
        /// ever be handed, and the kernels are handed the same.
        SharedSetLayouts sharedSets(const SetLayout& textureLayout) const;

        /// Writes the frame's own block into `mConstants`, barriered against both the dispatch
        /// before it and the one after.
        void writeConstants(VkCommandBuffer commands, const Shaders::VisibilityConstants& described) const;

        /// Pushes set zero — everything both passes read — and binds the three sets nothing pushes.
        /// Any of the pipelines here, because the volume reads the same world the trace does.
        ///
        /// @param shown the composite's frame as shown, and nothing for every other launch, whose
        ///        set does not declare it.
        void pushInputs(VkCommandBuffer commands, const Pipeline& pipeline, const VisibilityInputs& inputs,
            const Image* shown = nullptr) const;

        /// Whether every kernel of `tuple` is made, by the background compile: never the full
        /// tuple's, which `kernels` waits for.
        bool isMade(VisibilityVariant tuple) const;

        /// One stage's kernels, a tuple each.
        using TupleKernels = std::array<std::unique_ptr<ComputePipeline<NoConstants>>, VisibilityVariant::sCount>;

        /// How many of `Kernel` are the trace's stages: `Visibility` to `Landing`.
        static constexpr std::size_t sStageCount = 6;

        /// `stage`'s kernel for `variant`, which `variantFor` answered.
        static const ComputePipeline<NoConstants>& kernelOf(const TupleKernels& stage, VisibilityVariant variant);

        /// Whether `variant` runs the media stage: a sun to cast shafts, and a sea to cast them in.
        static bool hasMedia(VisibilityVariant variant);

        /// The launch that fills the fog volume's froxels for `variant`, a room's included: the
        /// volume walks the lamps once per froxel where the closed form would be a lamp reservoir
        /// and a shadow ray per pixel. **The maps are not a question it asks**, so a tuple is
        /// answered by its twin without them where that one is made, and by the full tuple's until
        /// then.
        const TracePipeline<NoConstants>& scatterPipelineFor(VisibilityVariant variant) const;

        const Device& mDevice;

        Buffer mBlueNoise;
        Buffer mSpecularAlbedo;

        /// This frame's `VisibilityConstants`, on the device: a push constant until they passed
        /// 256 bytes. Written with `vkCmdUpdateBuffer`, which runs in queue order, so one buffer
        /// serves every frame.
        Buffer mConstants;

        /// Fixed for the life of the pass, where the four in `VisibilityVariant` are the frame's:
        /// what counts is which binary was built and not what is being looked at. As the word the
        /// kernels are specialized with.
        std::uint32_t mCounting = 0;

        /// Whether the tables below fill with a kernel per tuple behind the full tuple's, or hold the
        /// full tuple's alone.
        bool mSpecialize = true;

        /// The second of the two sets bound after the pushed one, which the renderer owns for its
        /// whole life. The first is the scene's and arrives with the frame — `mTextureLayout`.
        const SetLayout* mChannelLayout = nullptr;

        /// The third of the sets nothing pushes, which the fog volume owns. Held for the reason
        /// `mChannelLayout` is.
        const SetLayout* mVolumeLayout = nullptr;

        /// Every kernel the pass launches, filled by `compileEvery`'s hands, each writing slots no
        /// other hand does.
        struct Kernels
        {
            /// Each stage's kernels, by its `Kernel`, a pipeline per tuple: the media stage's for the
            /// tuples `hasMedia` names alone.
            std::array<TupleKernels, sStageCount> mStages;

            /// The same table for the launch that fills the froxels, made for the tuples without
            /// maps — `scatterPipelineFor` says why. A launch and not a dispatch, and so is the
            /// column pass under it: `fogscatter.rgen` says what a ray query answers inside a
            /// dispatch when another process shares the card.
            std::array<std::unique_ptr<TracePipeline<NoConstants>>, VisibilityVariant::sCount> mScatter;

            /// The launch that finds where each column's ray stops, which no tuple changes: it traces
            /// and shades nothing.
            std::unique_ptr<TracePipeline<NoConstants>> mDepth;

            /// The launch that composites the puffs over the shown frame, which takes no tuple
            /// either: it reads the bin and the air and traces nothing. A launch and not a dispatch
            /// for the reason `spritecomposite.rgen` gives.
            std::unique_ptr<TracePipeline<Shaders::PuffConstants>> mSpriteComposite;

            /// The launch over the sprite list that keeps the rain from under the roofs. One, like the
            /// composite's: it reads the structure and the tables and has no opinion about the sky.
            std::unique_ptr<TracePipeline<NoConstants>> mSpriteShelter;

            /// The launch over the emitters that writes what each is for this camera, for the same
            /// reason.
            std::unique_ptr<TracePipeline<NoConstants>> mSpriteEmitters;

            /// The pass that integrates the columns, which takes no tuple at all: every question was
            /// answered by the pass that filled the froxels.
            std::unique_ptr<ComputePipeline<NoConstants>> mIntegrate;
        };

        /// The kernels, once the compile is over: waits for it, and throws what it threw, every time
        /// it is asked — the one way to them, so no launch can be recorded from a table still being
        /// filled. A shared future's read once it is ready.
        const Kernels& kernels() const;

        /// Every trace module the kernels run, read once with its interface by the compile before its
        /// hands start, and shared by them: on the compile's thread, so a file that cannot be read
        /// is thrown to every ask of the kernels as a kernel that cannot be made is. Before the
        /// compile, so its hands are joined before the code goes. **Let go by the last compile once
        /// its hands are joined**: a few megabytes nothing reads again.
        std::optional<ShaderCode> mCode;

        Kernels mKernels;

        /// Which tuples the background compile made whole, each set by the hand that made the
        /// tuple's last kernel after it wrote them, and read by the frame that picks its tuple
        /// (`variantFor`) before it reads them.
        std::array<std::atomic<bool>, VisibilityVariant::sCount> mMade{};

        /// The tuple a frame last wanted and found unmade (`variantFor`), whose kernels the
        /// background compile's hands take first — Dolphin's and Godot's way with the shader a frame
        /// is waiting on. `VisibilityVariant::sCount` for none. A frame of the game's own wants
        /// what its sky and its content say, and only the compile's order stood between them.
        mutable std::atomic<std::uint32_t> mWanted{ VisibilityVariant::sCount };

        /// The compile: the full tuple's and the singles' hands, counted as each kernel lands; and
        /// behind it every other tuple's, counted a tuple at a time. Last, so they are stopped and
        /// joined before the table they fill goes, the tuples' first.
        Job mCompiling;
        Job mSpecializing;
    };
}
