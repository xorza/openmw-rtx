#pragma once

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <osg/Vec2d>

#include <components/dlss/dlss.hpp>
#include <components/rtx/common/cardaddress.hpp>
#include <components/rtx/common/jobprogress.hpp>
#include <components/rtx/common/namedenum.hpp>
#include <components/rtx/frame/camera.hpp>
#include <components/rtx/frame/frameoptions.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/shaders/gbuffer.h>
#include <components/rtx/shaders/visibility.h>
#include <components/rtx/world/frameworld.hpp>

#include "framedigest.hpp"
#include "framesinflight.hpp"
#include "framezone.hpp"
#include "guirenderer.hpp"
#include "memoryreport.hpp"
#include "picturerenderer.hpp"
#include "shaderset.hpp"
#include "slot.hpp"
#include "verticalsync.hpp"

struct SDL_Window;

namespace Rtx
{
    /// Which of the trace's kernels a host waits for (`Renderer::awaitKernels`).
    ///
    /// **A frame draws on the full kernels until its own tuple is made**, as Godot's ubershaders and
    /// Dolphin's hybrid mode draw until the specialized shader is compiled
    /// (`docs/architecture.md`, "Every driver draws its first frames on the full kernels"): the
    /// full tuple takes about ten seconds cold, every tuple about a minute.
    enum class KernelSet : std::uint8_t
    {
        /// The kernels a frame draws with: the full tuple's and the ones no tuple changes. What a
        /// played start waits for.
        Drawing,

        /// Every tuple's as well, which a run that measures or compares waits for, so two runs
        /// change kernel at no frame.
        Every,
    };

    struct SkyContent;

    /// How much of the graphics API's own checking a run loads. One level and not three switches,
    /// because each finer check needs the one under it and the two finer ones are never loaded
    /// together: what each costs and why is the backend's to say.
    enum class ValidationLevel
    {
        Off,

        /// The API's core checks.
        On,

        /// The core checks and the checks of how the device's work is ordered.
        Sync,

        /// The core checks and the checks of what the shaders do on the device.
        Gpu,
    };

    /// How a level is spelled on a command line and in a report. The one list of the names, for
    /// the reason `sUpscaleNames` gives.
    inline constexpr NamedEnum sValidationNames{ std::array{
        std::pair{ ValidationLevel::Off, std::string_view("off") },
        std::pair{ ValidationLevel::On, std::string_view("on") },
        std::pair{ ValidationLevel::Sync, std::string_view("sync") },
        std::pair{ ValidationLevel::Gpu, std::string_view("gpu") },
    } };

    /// Whether the graphics API's validation runs without anyone asking: on outside a Release build,
    /// off there because it costs half the frame rate and allocates on the frame path. The build decides
    /// and no setting does, because a setting would put a developer's diagnostic in a player's
    /// configuration file.
    inline constexpr bool sValidationByDefault = OPENMW_RTX_VALIDATION_BY_DEFAULT;

    /// Developer instrumentation. Nobody enables any of this in a run they care about the frame rate
    /// of, and a backend reads whichever of it its API offers.
    struct ValidationOptions
    {
        ValidationLevel mLevel = ValidationLevel::Off;

        /// Stop the process on the first error. Off for a test suite, which provokes errors
        /// deliberately.
        bool mAbortOnError = true;

        /// Whether somebody asked for this by name rather than a build turning it on. A run that
        /// demanded the validation and cannot have it fails naming what is missing, because an empty
        /// log reads as a clean pass; a build that switched it on by default only warns.
        bool mDemanded = false;

        /// Whether every queue submit is logged as it is made, with what the submitting thread was
        /// doing (`Crash::NoteScope`), and watched to its end before the next: a diagnostic for a
        /// device that stops answering, or a machine that stops with it, whose log then ends on the
        /// submit that never finished. Each costs the overlap of the CPU with the GPU.
        bool mWatchSubmits = false;

        /// Whether the queue writes a buffer marker at each zone it passes, so a lost device names
        /// the last zone each stage reached (AMD's markers, which RADV has too): two writes a zone,
        /// which cost nothing a bench can see, so on in every build.
        bool mMarkers = true;

        /// The same through NVIDIA's checkpoints, which the driver keeps for every command buffer
        /// that sets one: on where the build validates, and asked for by name in any other
        /// (`OPENMW_RTX_BREADCRUMBS`), so a shipped build can report where its device was lost.
        bool mCheckpoints = sValidationByDefault;
    };

    /// What a run decides once of how the renderer works: the knobs the frames are traced under,
    /// which layers watch, and how much video memory it may take. One record, held whole by the
    /// backend's options and by each host's run and assigned whole, so a knob of the run is declared
    /// once.
    struct RunProfile
    {
        /// Everything the run decided once about how the picture is made. The upscaling mode is
        /// fixed for the renderer's lifetime bar `Renderer::setUpscale`, and a build that has no
        /// upscaler refuses anything but `Off` at construction.
        RenderProfile mProfile{};

        /// What the graphics API's validation checks. Carried by the run and never in a settings
        /// file, for the reason `sValidationByDefault` gives.
        ValidationOptions mValidation{};

        /// The video memory the renderer takes its budget to be, in bytes, where the device states
        /// more; nothing to take the device's word. For a run that asks what a smaller card does
        /// with a place: textures are held to a smaller side first, and what still does not fit is
        /// refused. What the frame itself holds is never refused. The harness's, never a played session's.
        std::optional<std::uint64_t> mMemoryBudget{};

        /// The upscaling modes whose frame targets the reserve keeps room for beside the running
        /// mode's, for a run that moves between them (`Renderer::setUpscale`): the reserve stands at
        /// the largest of them from the start, so a move between two leaves the world as it stood.
        /// The harness's, and never a played session's: a player's reserve is the running mode's
        /// alone, which is room the textures stand larger in.
        std::vector<Upscale> mReservedUpscales{};

        /// Where the driver's own code of every pipeline is written as text, a file a pipeline, or
        /// empty for none: what a profiler's instruction view shows, on a device no profiler there
        /// reads. The harness's, and never a played session's.
        std::filesystem::path mPipelineCode{};

        /// Whether each zone of a frame also reads the device's own counters — the instructions it
        /// ran, how busy its units were, how its caches hit (`GpuZones::counters`) — where the device
        /// has them: what a profiler shows of a dispatch, on a device no profiler there reads. The
        /// harness's, and never a played session's: the queries add work to every zone.
        bool mZoneCounters = false;
    };

    /// What a renderer's frames report, chosen once as it is made: the three figures a report reads
    /// and nothing a player does ever reads, made together or not at all.
    enum class Reporting
    {
        /// None of them: the counts' atomics, the zones' timestamps and the digest's pass and buffers
        /// are never made.
        Played,

        /// All of them.
        Measured,
    };

    struct RendererOptions
    {
        /// The compiled shaders of whichever backend this is, which outlive the renderer, and whether
        /// they count their stores that were not finite — `FrameResult::mNotFinite`. The game's
        /// do not, and nor do a measured run's, whose figures are of the game's kernels.
        ShaderSet mShaders;

        /// Where a backend keeps what it compiled, so that a later run need not compile it again.
        /// The user's cache directory (`ConfigurationManager::getCachePath`), because what goes here
        /// is regenerable and worth tens of megabytes. Empty keeps nothing.
        std::filesystem::path mCacheDirectory{};

        /// Where DLSS's files are, as the host's configuration names them: the runtime folder the
        /// launcher downloads the feature library into, and the game's own folder. Empty folders
        /// leave DLSS uninstalled.
        DlssLibrary::Folders mDlss{};

        /// The frame's size: what `readPixels` gives back, and what a window shows scaled to fit.
        /// What it is traced at follows from `mRun.mProfile.mUpscale`.
        std::uint32_t mWidth = 1920;
        std::uint32_t mHeight = 1080;

        /// What the run decided once: the profile, the validation and the budget.
        RunProfile mRun{};

        /// Where the frame is shown, or null for a renderer that only reads pixels back. A window
        /// and not a surface, because a surface is a thing an API has.
        SDL_Window* mWindow = nullptr;

        /// How a present paces the frame, where there is a window. Off for a window somebody
        /// steers by hand and for a measured run, which is why off is the default; the game hands
        /// its own setting over, and `Renderer::setVerticalSync` follows a change to it.
        VerticalSync mVerticalSync = VerticalSync::Off;

        /// What the frames report to the host: the hits each frame counts (`FrameResult::mHits`),
        /// the zones it times on the device (`FrameResult::mGpu`), a picture and a digest a frame
        /// may ask back (`FrameOptions::mReadBack`), and what the driver's compiler made of each
        /// pipeline. Measured by default, so a reader who forgets it gets numbers rather than a
        /// silent nought. Not a knob of the run's picture, which is why it is not in the profile. A
        /// played frame still opens its zones, which name the work a lost device was on
        /// (`Device::checkpoint`): what goes is the timestamps and the read of them.
        Reporting mReporting = Reporting::Measured;
    };

    /// What a backend reports about the scene it took. The harness's summary line, as a struct.
    struct SceneStats
    {
        InstanceCounts mInstances;

        /// How many instance slots the scene holds, placed or not: every one it ever handed out,
        /// since a dropped placement keeps its slot for its return. What the placed ones are
        /// packed out of before the top level is built.
        std::uint32_t mInstanceSlots = 0;

        /// What the renderer holds in acceleration structures and in scene tables: what a video
        /// memory budget is spent against, at the high-water mark, because an allocator gives a
        /// block back only when it empties whole.
        std::uint64_t mStructureBytes = 0;

        /// What the structures occupy inside that; a structure copied tight gives its loose room back.
        std::uint64_t mStructureLiveBytes = 0;

        std::uint64_t mTableBytes = 0;

        /// What the structures are worked in beside the two above: the rows, their packing, the
        /// poses and the builds' scratch, which the device holds for the scene as surely.
        std::uint64_t mWorkBytes = 0;

        /// What the structures answered for and not yet copied tight would come to, or nought where
        /// the device would not say: the difference from `mStructureBytes` is what compacting gives.
        std::uint64_t mCompactableBytes = 0;
        std::uint64_t mCompactableNowBytes = 0;

        /// How many refitted structures the rota has built whole again since the scene was made. A
        /// refit keeps a structure's shape as its placements move and so loses its quality, which a
        /// rebuild on a rota wins back; how often is the backend's.
        std::uint64_t mRebuilt = 0;

        /// Every texture the renderer holds and what those come to, from one walk of the array, so
        /// the two cannot disagree about which slots they counted.
        std::uint32_t mTextureCount = 0;
        std::uint64_t mTextureBytes = 0;
        TextureSourceBytes mTextureBytesBySource;

        /// How many of those stand smaller than their files: held to a smaller side where the
        /// device had no room for them as the files are, or past the side it takes.
        std::uint32_t mReducedTextureCount = 0;
    };

    /// One stretch of a frame, measured by the device's own clock — what each dispatch and each
    /// structure build cost, which a wall clock around a submit cannot tell.
    struct GpuSpan
    {
        FrameZone mZone = FrameZone::Count;
        double mMs = 0.0;
    };

    /// The most of the device's counters one zone reads (`RunProfile::mZoneCounters`).
    inline constexpr std::uint32_t sMaxZoneCounters = 16;

    /// The most zones one frame may open: every zone there is, the tone pass of a summed run again,
    /// a second placement's, `Blas` to `Ground`, where a caller places the world again before it
    /// traces, which the game never does, and the bounce's and the landing's again for each bounce
    /// ray past the first. A frame that opens more is a broken contract, which `GpuTimer` asserts.
    inline constexpr std::uint32_t sMaxGpuZones = static_cast<std::uint32_t>(sFrameZoneCount) + 1
        + (static_cast<std::uint32_t>(FrameZone::Ground) - static_cast<std::uint32_t>(FrameZone::Blas) + 1)
        + 2 * (Shaders::MAX_BOUNCE_RAYS - 1);

    /// Where the device spent a frame, in the order the work was recorded, or nothing where it
    /// cannot write timestamps or the renderer was made to play (`RendererOptions::mReporting`).
    /// Owned by the report rather than borrowed from the timer that measured it: with
    /// `sFramesInFlight` in flight, the frame that takes this frame's slot begins its timer before
    /// this report is read.
    class GpuZones
    {
    public:
        void clear()
        {
            mCount = 0;
            mCounterNames = {};
            mCounters = {};
        }

        void add(const GpuSpan& span)
        {
            assert(mCount < sMaxGpuZones && "more zones than a frame may open");
            mSpans[mCount++] = span;
        }

        std::span<const GpuSpan> spans() const { return { mSpans.data(), mCount }; }

        /// What the device calls each counter the zones read, or nothing where they read none: the
        /// backend's names, which stand for as long as it does.
        std::span<const std::string_view> counterNames() const { return mCounterNames; }

        /// What the counters read over each span, `counterNames().size()` a span in the order of
        /// `spans`, or nothing where they read none.
        ///
        /// **The renderer's memory and not the report's**, so a played session's report, copied a
        /// few times a frame, carries no room for what it never reads. The timer writes it only when
        /// it resolves its slot's next frame, so it stands as `FrameResult::mPixels` does: until the
        /// `renderFrame` after the one the report was collected before.
        std::span<const double> counters() const { return mCounters; }

        void setCounters(std::span<const std::string_view> names, std::span<const double> values)
        {
            assert(names.size() <= sMaxZoneCounters && "more counters than a zone reads");
            assert(values.size() == names.size() * mCount && "not one reading of each counter a span");
            mCounterNames = names;
            mCounters = values;
        }

    private:
        std::array<GpuSpan, sMaxGpuZones> mSpans{};
        std::uint32_t mCount = 0;
        std::span<const std::string_view> mCounterNames;
        std::span<const double> mCounters;
    };

    /// The most shader modules one census tells apart: `Shaders::CENSUS_KERNELS`, which the backend
    /// holds to this.
    inline constexpr std::uint32_t sMaxCensusKernels = 96;

    /// Where a shader module stored a value that was not finite: the invocation that did, whose x and y a
    /// kernel over a frame's pixels takes from the pixel, and a launch from its ray. A vertex stage names none.
    struct NotFiniteAt
    {
        std::uint32_t mX = 0;
        std::uint32_t mY = 0;

        bool operator==(const NotFiniteAt&) const = default;
    };

    /// One shader module's stores that were not finite, and where the first frame they came in stored its first:
    /// the topmost, then the leftmost invocation, where a census says where (`Shaders::Census::mFirst`).
    struct NotFiniteStores
    {
        /// The module's name, which the backend keeps for as long as it lives.
        std::string_view mKernel;
        std::uint32_t mStores = 0;
        std::optional<NotFiniteAt> mFirst;
    };

    /// Stores whose value was a NaN or an infinity, by the shader module that wrote them — the
    /// backend's census, which every store of a float is counted into. Every one is a logic error: a
    /// history that takes one keeps it and spreads it to its neighbours a frame, so the picture goes
    /// black in blocks from there, and nothing on the way refuses it. Holds the modules that wrote
    /// one, in the order they were first added. Summed over a stop for `Check::Finite`.
    class NotFinite
    {
    public:
        /// Adds `stores` against `kernel`, beside what it holds already, and where they were first stored: the
        /// first a kernel came with is kept, the frame before's over the frame after's.
        void add(std::string_view kernel, std::uint32_t stores, std::optional<NotFiniteAt> first = std::nullopt)
        {
            if (stores == 0)
                return;

            for (std::uint32_t at = 0; at < mCount; ++at)
            {
                if (mKernels[at].mKernel == kernel)
                {
                    mKernels[at].mStores += stores;
                    if (!mKernels[at].mFirst.has_value())
                        mKernels[at].mFirst = first;
                    return;
                }
            }

            assert(mCount < sMaxCensusKernels && "more modules than a census tells apart");
            mKernels[mCount++] = NotFiniteStores{ .mKernel = kernel, .mStores = stores, .mFirst = first };
        }

        void add(const NotFinite& more)
        {
            for (const NotFiniteStores& kernel : more.kernels())
                add(kernel.mKernel, kernel.mStores, kernel.mFirst);
        }

        std::uint32_t total() const
        {
            std::uint32_t sum = 0;
            for (const NotFiniteStores& kernel : kernels())
                sum += kernel.mStores;
            return sum;
        }

        /// What `kernel` wrote, or nought where it wrote nothing.
        std::uint32_t of(std::string_view kernel) const
        {
            for (const NotFiniteStores& held : kernels())
                if (held.mKernel == kernel)
                    return held.mStores;
            return 0;
        }

        std::span<const NotFiniteStores> kernels() const { return { mKernels.data(), mCount }; }

    private:
        std::array<NotFiniteStores, sMaxCensusKernels> mKernels{};
        std::uint32_t mCount = 0;
    };

    /// One image of what a counting renderer kept of a frame (`NotFiniteFrame`).
    struct NotFiniteImage
    {
        /// How far a window reaches from its centre, its side, and its texels.
        static constexpr std::uint32_t sRadius = 4;
        static constexpr std::uint32_t sSide = 2 * sRadius + 1;
        static constexpr std::uint32_t sTexels = sSide * sSide;

        /// What a capture and a dump call it: a channel's name, `composed` for the frame the composite wrote,
        /// or a denoiser image's. Text that lives as long as the program.
        std::string_view mName;

        /// Whether its texels are the words a pass packed, and not floats.
        bool mWords = false;

        /// Its first texel that was not finite, the topmost and then the leftmost; never for words, which hold
        /// whatever their pass packed.
        std::optional<NotFiniteAt> mFirst;

        /// Its window around the frame's centre, a row at a time from the top left, four words a texel: a
        /// float's bits as the shaders load the texel, or the words. Nought past the frame's edge.
        std::array<std::array<std::uint32_t, 4>, sTexels> mTexels{};
    };

    /// What a renderer whose shaders count kept of the first frame whose census counted a store that was not
    /// finite by the time the denoiser's answers were read (`Renderer::takeNotFiniteFrame`): its images around
    /// the first texel that held one. The census's report comes back frames late, after later frames wrote
    /// over every one of them, so the renderer keeps them as the frame runs.
    struct NotFiniteFrame
    {
        /// Which frame, as `FrameResult::mFrame` numbers it, and the extent its images were read over.
        std::uint64_t mFrame = 0;
        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;

        /// The texel the windows stand around: the first image's first texel that was not finite, in
        /// `mImages`' order. Nothing where none held one, and then no window holds anything: what the census
        /// counted was written over by then, or was written into none of these images.
        std::optional<NotFiniteAt> mCentre;

        /// The images the frame had, in the order the frame writes them: the trace's channels, the
        /// denoiser's in the order its stages run, and the composed frame last.
        std::vector<NotFiniteImage> mImages;
    };

    struct FrameResult
    {
        /// Primary rays that hit something: what tells "the cell rendered" from "the camera faced
        /// away" without opening the image. Nought where the renderer plays (`RendererOptions::mReporting`).
        std::uint32_t mHits = 0;

        /// What the frame wrote that was not finite, by shader module, with what the frames before
        /// it wrote whose reports the renderer dropped uncollected, from a full ring or for a new
        /// world. Nothing where the shaders do not count (`ShaderSet::mCensus`).
        NotFinite mNotFinite;

        /// Pixels whose walk the trace cut short: a logic error, logged the first time it happens.
        std::uint32_t mWalksCut = 0;

        /// What the hold's own clock said the hold came to, in milliseconds: what
        /// `RenderProfile::mStressOverlapMs` asked and the tick past it, on a run that holds, and
        /// nought on one that does not. The loop's reading and not the timer's zone around it,
        /// which takes in the launch and the drain.
        double mHeldMs = 0.0;

        /// How long the ring waited for this frame; nought where it was already done.
        double mWaitMs = 0.0;

        /// How many frames the ring held when this one was submitted, this one included — one
        /// where the caller waited the frame behind out first, two where it did not. What the
        /// bench's `overlap` figure is taken from, and the number a gate on two in flight asserts.
        std::uint32_t mInFlight = 0;

        GpuZones mGpu{};

        /// What put this frame back together, as the renderer resolved it.
        Reconstruction mReconstruction;

        /// Which frame this is, counted by the renderer from its first — the number
        /// `Renderer::getFrameCount` was about to hand the frame when it was drawn, so a caller
        /// that noted something about the frame then can find it now.
        std::uint64_t mFrame = 0;

        /// The picture, where `FrameOptions::mReadBack` asked for it, as `readPixels` lays it
        /// out; empty otherwise. The renderer's own memory: a report collected before a
        /// `renderFrame` reads until the `renderFrame` after that one.
        std::span<const std::uint8_t> mPixels;

        /// What the frame traced, digested, where `FrameOptions::mReadBack` asked; nothing
        /// otherwise.
        std::optional<FrameDigest> mDigest;
    };

    /// One frame as a host asks for it: where it is seen from and what the eye draws, the world it
    /// stands in, and what it asks over the profile. **The host states and the renderer
    /// describes**: the renderer lays the world over the viewpoint (`describeWorld`) and samples
    /// what that comes to (`sampleFrame`), so no field of the block it traces has two writers, and
    /// what a host could set and lose is not offered to it.
    struct FrameRequest
    {
        Viewpoint mView;

        /// Which classes the eye draws, `Shaders::MASK_*`, and whether the lamps light anything:
        /// what the game's view mask and its lighting toggle say.
        std::uint32_t mRayMask = Shaders::MASK_EVERY_CLASS;
        bool mLamps = true;

        /// The sampler's frame, which the draws and the jitter walk — `VisibilityConstants::mFrame`.
        std::uint32_t mSampleFrame = 0;

        WorldReading mWorld;

        /// Where the sky's own sheets stand in the world's texture table, as the content was read
        /// (`addSkyContent`): borrowed for the call.
        const SkyContent* mSky = nullptr;

        FrameOptions mOptions;
    };

    /// What a frame was traced with, as the renderer described it: the block before the frame's
    /// sampling, which a harness digests beside the scene, and how far the air stood carried
    /// downwind after it (`FogDrift`), which a harness records to stand the air there again.
    struct FrameTraced
    {
        Shaders::VisibilityConstants mConstants{};
        osg::Vec2d mCarried;
    };

    /// One traced image, whichever API produced it: what holds the scenes and traces the pictures
    /// inside the interface (`PictureRenderer`), what the interface is drawn on (`GuiRenderer`),
    /// what produces a frame, and what a test or a harness reads back. Nothing below this line is
    /// abstracted, so a method here is worth a whole scene or a whole frame, and none is reached per
    /// instance or per pixel. `slot` says which scene throughout: the world's, or one `addViewScene`
    /// handed out for a picture inside the interface.
    class Renderer : public PictureRenderer, public GuiRenderer
    {
    public:
        /// Resizes the frame; what the trace runs at follows from the upscaler, and `getExtents`
        /// says. Where the new targets take more room than the world's content left, or less where
        /// content was held back for want of room, the world goes with the old targets
        /// (`describeHeld` answers nought): the next hand-over builds it again (`SceneUploader`).
        virtual void resize(std::uint32_t width, std::uint32_t height) = 0;

        /// The window is `width` by `height` pixels: the surface follows it, and the frame is shown
        /// in it as `Misc::present` places it, with black beside it. Called every frame by a host
        /// with a window, because a surface that stopped matching the window is remade here; a
        /// renderer with no window has nothing to show and ignores it.
        virtual void showIn(std::uint32_t width, std::uint32_t height) = 0;

        /// How hard the upscaler works, which decides what the frame is traced at. Rebuilds every
        /// target, so the old extents describe a camera nothing will accept, and lets the world go
        /// where `resize` would. Throws where the mode cannot be reached, and a caller that offers
        /// the mode catches it and stays where it was.
        virtual void setUpscale(Upscale upscale) = 0;

        /// How the presented image meets the monitor's refresh. Costs the presentation's rebuild, so a
        /// settings-change call and not a frame one.
        virtual void setVerticalSync(VerticalSync mode) = 0;

        /// `RenderProfile::mAnisotropy`, changed while the frames run: a menu change. Every
        /// texture's binding is written again into each copy of a scene's texture set, at the
        /// placement that next writes that copy.
        virtual void setAnisotropy(std::uint32_t anisotropy) = 0;

        /// `RenderProfile::mGamma`, changed while the frames run: a slider moved in the menu, and
        /// the next frame's picture has it. A finite number greater than nought: a caller that reads
        /// it from a file or a command line refuses anything else first.
        virtual void setGamma(float gamma) = 0;

        /// `SamplingSwitches::mIndirect` of `RenderProfile::mReconstruction`, changed while the frames
        /// run: a box ticked in the menu, and the next frame traces its bounces or stops.
        virtual void setIndirectLighting(bool traced) = 0;

        /// `SamplingSwitches::mBounceRays` of `RenderProfile::mReconstruction`, changed while the frames
        /// run: a slider moved in the menu, and the next frame traces that many. From one to
        /// `Shaders::MAX_BOUNCE_RAYS`: a caller that reads it from a file refuses anything else first.
        virtual void setBounceRays(std::uint32_t rays) = 0;

        /// How many of `set`'s kernels are made, waiting `patience` at most for the rest, and
        /// rethrowing what making one threw. The renderer starts making them as it is made, on
        /// threads of its own, and returns without them, so a host shows this on a loading screen.
        /// Every call that traces waits for the kernels it draws with first, so a host that never
        /// asks is right all the same, and held on its first trace instead.
        virtual JobProgress awaitKernels(KernelSet set, std::chrono::milliseconds patience) = 0;

        /// Traces one frame; `setScene` first, which is an assert. Returns before the device has
        /// drawn it, so the caller can place the next one meanwhile, and `finishFrame` reads back
        /// what it came to, the reconstruction it resolved among it (`FrameResult::mReconstruction`).
        /// At most `sFramesInFlight` frames are in flight. What comes back at once is what the frame
        /// was described as, which the host holds no copy of. The air is carried on by the reading's
        /// clock, from where the last frame or `holdAir` left it.
        virtual FrameTraced renderFrame(const FrameRequest& request) = 0;

        /// Stands the air where `air` says it was carried, as of its sky clock, so the next frame at
        /// that clock is drawn in the air a harness recorded rather than wherever this session's
        /// frames carried it.
        virtual void holdAir(const AirClock& air) = 0;

        /// Closes the frame this frame's placements of the world opened, with no trace: where a
        /// placement is not followed by `renderFrame`, because the host refused the camera. Without
        /// it the frame stays open into the host's next, and every placement after takes one more
        /// command buffer. Nothing where no placement opened a frame.
        virtual void skipFrame() = 0;

        /// How many frames were closed, by `renderFrame` or by `skipFrame`, which is the number the
        /// next one carries in `FrameResult::mFrame`.
        virtual std::uint64_t getFrameCount() const = 0;

        /// What the oldest unreported frame came to, waiting for it where it is still in flight, or
        /// nothing where every frame drawn was reported. After `renderFrame` that is the frame just
        /// submitted, which a screenshot and a test want.
        virtual std::optional<FrameResult> finishFrame() = 0;

        /// What the oldest unreported frame came to, waiting only where the ring has no room for
        /// the frame about to be placed, or nothing where none has finished. Called before
        /// `placeScene`, it keeps `sFramesInFlight` in flight: `finishFrame` there instead waits the
        /// frame behind out on every frame, so the device idles until the next placement is submitted.
        virtual std::optional<FrameResult> collectFrame() = 0;

        /// Shows the frame `renderFrame` just produced. A surface that stopped matching the window
        /// is remade by the next `showIn`, which the host calls every frame; no answer comes back
        /// here, because the renderer keeps that fact itself. No window is an assert.
        virtual void presentFrame() = 0;

        /// Waits until what was presented stands no more than two frames ahead of the screen, where
        /// the presents meet the refresh and the device can say when one is shown; returns at once
        /// otherwise, and on a renderer with no window. Asked before a frame's input, so what the
        /// player does reaches the screen two refreshes later and not as many as the queue holds.
        virtual void awaitPresented() = 0;

        /// Multi-line report, a newline between lines and none after the last: the device and what it can
        /// trace with.
        virtual std::string describeDevice() const = 0;

        /// Which card the renderer runs on, for an instrument that reads that card's clock.
        virtual CardAddress getCardAddress() const = 0;

        /// The knobs the frames are traced under now: what the renderer was made with, and then
        /// whatever a setting moved since. The one copy, so a stop that writes a picture by the
        /// same rules reads the rules the game draws by.
        virtual const RenderProfile& getProfile() const = 0;

        /// The upscalers a menu may offer under this renderer, `offeredUpscalers`' answer for its
        /// device: DLSS where the build has it, the card is NVIDIA's and the player's library is
        /// installed, and not where this renderer's attempt at it was refused. Asked by the settings window, which
        /// would otherwise ask the system's adapters and not the one this renderer draws on.
        virtual std::span<const UpscalerChoice> getUpscalers() const = 0;

        /// What the upscaler this renderer resolved its choice to takes from the trace, which decides
        /// who filters the light (`Reconstruction::resolve`): asked by a harness that refuses a run whose
        /// frames ask for unfiltered light where the upscaler cannot give it.
        virtual UpscalerInput getUpscalerInput() const = 0;

        /// Whether instrumentation is actually running, which a missing layer makes different from
        /// having asked. Anything quoting a frame time has to say so.
        virtual bool isValidating() const = 0;

        /// What the renderer has taken from each of the device's memory heaps. Walks every
        /// allocation, so asked once at a place and never once a frame.
        virtual MemoryReport getMemoryReport() const = 0;

        /// What the world scene is made of, as the backend last placed it. Only meaningful once
        /// `Renderer::setScene` has been called for the world.
        virtual const SceneStats& getSceneStats() const = 0;

        /// Copies the picture into `pixels`, four bytes per pixel, tightly packed: the frame last
        /// traced, at the output extent, without the interface drawn over it since — what a
        /// screenshot, a save's thumbnail and a frozen frame show, as the rasterizer's do. Not on a
        /// frame path: it submits a copy and waits for it, so it is not const.
        virtual void readPixels(std::vector<std::uint8_t>& pixels) = 0;

        /// The same picture at sixteen bits a channel, four samples a pixel: written by a frame that
        /// sums (`FrameOptions::mAccumulate`) and by no other, since a mean of frames falls between
        /// the levels a byte holds. Undithered and without the debug lines. Asked after a frame that
        /// did not sum, it is an assert. Submits and waits, as `readPixels` does.
        virtual void readDeepPixels(std::vector<std::uint16_t>& samples) = 0;

        /// What the renderer kept of the first frame that stored a value not finite since the last call
        /// (`NotFiniteFrame`), or nothing where none has or the shaders do not count (`ShaderSet::mCensus`); the
        /// next such frame is kept after it. Waits out every frame in flight first, since a frame after the one
        /// whose report said so may be the one kept: not on a frame path.
        virtual std::optional<NotFiniteFrame> takeNotFiniteFrame() = 0;

    protected:
        Renderer() = default;
    };
}
