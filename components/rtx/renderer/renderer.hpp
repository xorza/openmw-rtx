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
#include <osg/Vec3f>

#include <components/rtx/common/index.hpp>
#include <components/rtx/common/jobprogress.hpp>
#include <components/rtx/common/namedenum.hpp>
#include <components/rtx/frame/camera.hpp>
#include <components/rtx/frame/frameoptions.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/renderer/framezone.hpp>
#include <components/rtx/renderer/shaderdirectory.hpp>
#include <components/rtx/scene/mesh.hpp>
#include <components/rtx/scene/refusal.hpp>
#include <components/rtx/scene/structurerevision.hpp>
#include <components/rtx/shaders/visibility.h>
#include <components/rtx/world/frameworld.hpp>
#include <components/sdlutil/vsyncmode.hpp>

#include "framedigest.hpp"
#include "framesinflight.hpp"
#include "guirenderer.hpp"
#include "memoryreport.hpp"
#include "slot.hpp"

struct SDL_Window;

namespace Rtx
{
    class SceneDesc;
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
    };

    /// Whether the graphics API's validation runs without anyone asking: on outside a Release build,
    /// off there because it costs half the frame rate and allocates on the frame path. The build decides
    /// and no setting does, because a setting would put a developer's diagnostic in a player's
    /// configuration file.
    inline constexpr bool sValidationByDefault = OPENMW_RTX_VALIDATION_BY_DEFAULT;

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
        /// with a place: content stops where it would stop there — textures held to a smaller side
        /// first — and what still does not fit is refused as it would be. What the frame itself
        /// holds is never refused, whatever this says. The harness's, and never a played session's.
        std::optional<std::uint64_t> mMemoryBudget{};
    };

    struct RendererOptions
    {
        /// Where the build wrote the compiled shaders for whichever backend this is, and whether
        /// they count their stores that were not finite — `FrameResult::mNotFinite`. The game's
        /// do not, and nor do a measured run's, whose figures are of the game's kernels.
        ShaderSet mShaders;

        /// Where a backend keeps what it compiled, so that a later run need not compile it again.
        /// The user's cache directory (`ConfigurationManager::getCachePath`), because what goes here
        /// is regenerable and worth tens of megabytes. Empty keeps nothing.
        std::filesystem::path mCacheDirectory{};

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
        SDLUtil::VSyncMode mVerticalSync = SDLUtil::VSyncMode::Disabled;

        /// Whether the frame counts for the host the primary rays that hit anything —
        /// `FrameResult::mHits`. On by default, so a reader who forgets it gets a number rather than a
        /// silent nought; the game clears it. Not a knob of the run's picture, which is why it is
        /// not in the profile.
        bool mCounting = true;
    };

    /// What a backend holds in one of its slots, as it says so itself. A slot and a scene are one
    /// to one, so nothing here has to name which scene.
    struct SceneHeld
    {
        /// `SceneDesc::getIdentity` of the description the slot was built from: an uploader
        /// handing another description to a slot has to build, because the structures and the
        /// texture array are the first description's, and appending the second's arrivals onto
        /// them would begin past the end of its own table. Nought where nothing was built, which
        /// no description's identity is.
        std::uint64_t mIdentity = 0;

        /// `SceneDesc::getStructureRevision` as it stood at the last `setScene` or `extendScene`.
        StructureRevision mStructureRevision;

        /// How long the slot's texture array is: the table's length, holes included, and not the
        /// tally, which `SceneStats::mTextureCount` is. Every arrival names its own slot, so nothing
        /// appends by it; it is the one view of the array's length, which an array built short of
        /// the table would get wrong.
        std::uint32_t mTextureCount = 0;
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

    /// The most zones one frame may open: every zone there is, and room past them for a pass
    /// recorded in batches, which opens its zone once a batch.
    inline constexpr std::uint32_t sMaxGpuZones = 40;
    static_assert(sFrameZoneCount <= sMaxGpuZones, "a zone a frame opens that the timer cannot hold");

    /// Where the device spent a frame, in the order the work was recorded, or nothing where it
    /// cannot write timestamps. Owned by the report rather than borrowed from the timer that
    /// measured it: with `sFramesInFlight` in flight, the frame that takes this frame's slot begins its
    /// timer before this report is read.
    class GpuZones
    {
    public:
        void clear() { mCount = 0; }

        void add(const GpuSpan& span)
        {
            assert(mCount < sMaxGpuZones && "more zones than a frame may open");
            mSpans[mCount++] = span;
        }

        std::span<const GpuSpan> spans() const { return { mSpans.data(), mCount }; }

    private:
        std::array<GpuSpan, sMaxGpuZones> mSpans{};
        std::uint32_t mCount = 0;
    };

    /// The most shader modules one census tells apart: `Shaders::CENSUS_KERNELS`, which the backend
    /// holds to this.
    inline constexpr std::uint32_t sMaxCensusKernels = 96;

    /// One shader module's stores that were not finite.
    struct NotFiniteStores
    {
        /// The module's name, which the backend keeps for as long as it lives.
        std::string_view mKernel;
        std::uint32_t mStores = 0;
    };

    /// Stores whose value was a NaN or an infinity, by the shader module that wrote them — the
    /// backend's census, which every store of a float is counted into. Every one is a logic error: a
    /// history that takes one keeps it and spreads it to its neighbours a frame, so the picture goes
    /// black in blocks from there, and nothing on the way refuses it. Holds the modules that wrote
    /// one, in the order they were first added. Summed over a stop for `Check::Finite`.
    class NotFinite
    {
    public:
        /// Adds `stores` against `kernel`, beside what it holds already.
        void add(std::string_view kernel, std::uint32_t stores)
        {
            if (stores == 0)
                return;

            for (std::uint32_t at = 0; at < mCount; ++at)
            {
                if (mKernels[at].mKernel == kernel)
                {
                    mKernels[at].mStores += stores;
                    return;
                }
            }

            assert(mCount < sMaxCensusKernels && "more modules than a census tells apart");
            mKernels[mCount++] = NotFiniteStores{ .mKernel = kernel, .mStores = stores };
        }

        void add(const NotFinite& more)
        {
            for (const NotFiniteStores& kernel : more.kernels())
                add(kernel.mKernel, kernel.mStores);
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

    struct FrameResult
    {
        /// Primary rays that hit something: what tells "the cell rendered" from "the camera faced
        /// away" without opening the image. Nought where `RendererOptions::mCounting` was cleared.
        std::uint32_t mHits = 0;

        /// What the frame wrote that was not finite, by shader module. Nothing where the shaders
        /// do not count (`ShaderSet::mCensus`).
        NotFinite mNotFinite;

        /// What the hold's own clock said the hold came to, in milliseconds: what
        /// `RenderProfile::mStressOverlapMs` asked and the tick past it, on a run that holds, and
        /// nought on one that does not. The loop's reading and not the timer's zone around it,
        /// which takes in the launch and the drain: this is the figure that says the loop did as
        /// it was told, and the zone's excess over it is the card's.
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

    /// The light a picture inside the interface stands in, which no world describes: a flat sun
    /// and an ambient, as the rasterizer lights its previews, in the renderer's irradiance. Drawn
    /// with no sky source's shadow, the doll's and the map's alike, as the rasterizer draws them.
    struct PictureLight
    {
        /// Toward the sun, unit, or nought for none.
        osg::Vec3f mDirection;
        osg::Vec3f mIrradiance;
        osg::Vec3f mAmbient;
    };

    /// What a picture inside the interface is asked for, beyond where its camera stands. How much
    /// of the texture the picture fills, from its top-left corner, is the camera's own extent; the
    /// rest is left at `mClear`, whose alpha below one is a picture over the interface's own. The
    /// inventory doll's window resizes and the texture behind it does not.
    struct GuiTraceOptions
    {
        /// What the rest of the texture holds, red first: transparent black for a picture the GUI
        /// composites over what is behind it.
        std::array<float, 4> mClear{};

        /// What to trace against: a slot `Renderer::addViewScene` gave out, or the world's for the
        /// one the frame is drawn from. A map tile is a picture of the world; a doll is not.
        SceneSlot mScene = SceneSlot::world();

        /// Whether to leave a copy of the whole texture where `takeGuiCopy` can hand it to the host,
        /// which is the one time a picture inside the interface comes back to main memory.
        bool mReadBack = false;

        /// Which classes the picture draws, `Shaders::MASK_*`, and whether the lamps light it: a map
        /// tile leaves the actors out, and the rasterizer lights its previews by no lamp.
        std::uint32_t mRayMask = Shaders::MASK_EVERY_CLASS;
        bool mLamps = true;

        PictureLight mLight{};
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

    /// One traced image, whichever API produced it: what a scene is handed to, what the interface
    /// is drawn on, what produces a frame, and what a test or a harness reads back. Nothing below
    /// this line is abstracted — buffers, memory, command buffers and pipelines belong to a backend
    /// outright — so a method here is worth a whole scene or a whole frame, and none is reached per
    /// instance or per pixel. `slot` says which scene throughout: the world's, or one
    /// `addViewScene` handed out for a picture inside the interface.
    class Renderer : public GuiRenderer
    {
    public:
        /// Builds everything a scene needs, replacing whatever was there, and places it. `textures`
        /// are decoded already, each naming the slot it stands in (`TextureData::mSlot`), and must
        /// outlive the call.
        virtual void setScene(SceneSlot slot, const SceneDesc& scene, std::span<const TextureData> textures) = 0;

        /// The same scene with more in it: geometry and textures appended, nothing renumbered, at
        /// the cost of a cell and never of `setScene`. `arrived` is the textures the scene gained
        /// since the last call, each naming the slot it stands in — a slot a departure freed, or
        /// one past the count this already holds.
        ///
        /// **Appends and does not place**: the caller places after it (`placeScene`), as on a frame
        /// where nothing arrived, because the top level names every instance and an arrival changed
        /// the list.
        virtual void extendScene(SceneSlot slot, const SceneDesc& scene, std::span<const TextureData> arrived) = 0;

        /// What this slot was last built from, and how far it has been extended since.
        virtual SceneHeld describeHeld(SceneSlot slot) const = 0;

        /// What the last `setScene` or `extendScene` of `slot` could not stand for the device's
        /// sake — a texture past the side it takes, a texture or a mesh it had no room for — for
        /// the scene's owner to report with the rest of what content was refused
        /// (`SceneDesc::refusals`). A texture refused draws the stand-in, and a mesh refused is
        /// left out. Valid until the next call of either for `slot`.
        virtual std::span<const Refusal> getRefusals(SceneSlot slot) const = 0;

        /// Destroys the images of the texture slots a scene gave up. The slots keep their place,
        /// as they do in the scene's own table, and no live material names a freed one.
        virtual void dropTextures(SceneSlot slot, std::span<const Index> textures) = 0;

        /// The same scene with its instances and lights somewhere else and its actors in a new
        /// pose: rebuilds only what says where things are, plus the structure of each mesh
        /// `getDeformed` names. `scene` must be the scene `setScene` was given, because the
        /// placements index into structures this already holds. The world's ripples are read here
        /// too, beside its lights and sprites: what disturbed the water is a per-frame list of the
        /// scene like the other three, and the next `renderFrame` presses what the last placement
        /// or `setScene` read.
        virtual void placeScene(SceneSlot slot, const SceneDesc& scene) = 0;

        /// A scene of its own for a picture inside the interface — the inventory doll, the race
        /// preview — which a ray the frame sends must not find. Slots given back are taken over
        /// before the table grows.
        virtual SceneSlot addViewScene() = 0;

        virtual void dropViewScene(SceneSlot slot) = 0;

        /// Traces the scene from `camera` into a GUI texture rather than into the frame: a map
        /// tile, the inventory doll. Not the frame's chain — nothing upscales or averages and the
        /// exposure is one, because a still has no previous frame. Recorded and not run: the picture
        /// rides the next submit, reads the copy of the scene its last placement wrote, and the next
        /// placement of that scene waits for the frame it rode.
        virtual void traceGuiTexture(GuiSlot texture, const Viewpoint& view, const GuiTraceOptions& options) = 0;

        /// The copy the last `traceGuiTexture` with `mReadBack` left of `texture`, four bytes a
        /// pixel, tightly packed, row zero first, into `into` as far as it reaches. False until the
        /// copy arrived, which is two frames on, and never a wait.
        virtual bool takeGuiCopy(GuiSlot texture, std::span<std::uint8_t> into) = 0;

        /// Submits every picture recorded and not yet carried and waits for them, for a harness or a
        /// test standing outside any frame. A game never calls it.
        virtual void finishGuiTraces() = 0;

        /// Resizes the frame; what the trace runs at follows from the upscaler, and `getExtents`
        /// says.
        virtual void resize(std::uint32_t width, std::uint32_t height) = 0;

        /// The window is `width` by `height` pixels: the surface follows it, and the frame is shown
        /// in it as `Misc::present` places it, with black beside it. Called every frame by a host
        /// with a window, because a surface that stopped matching the window is remade here; a
        /// renderer with no window has nothing to show and ignores it.
        virtual void showIn(std::uint32_t width, std::uint32_t height) = 0;

        /// How hard the upscaler works, which decides what the frame is traced at. Rebuilds every
        /// target, so the old extents describe a camera nothing will accept. Throws where the mode
        /// cannot be reached, and a caller that offers the mode catches it and stays where it was.
        virtual void setUpscale(Upscale upscale) = 0;

        /// How the presented image meets the monitor's refresh. Costs the presentation's rebuild, so a
        /// settings-change call and not a frame one.
        virtual void setVerticalSync(SDLUtil::VSyncMode mode) = 0;

        /// `RenderProfile::mAnisotropy`, changed while the frames run: a menu change. Every
        /// texture's binding is written again into each copy of a scene's texture set, at the
        /// placement that next writes that copy.
        virtual void setAnisotropy(std::uint32_t anisotropy) = 0;

        /// `RenderProfile::mGamma`, changed while the frames run: a slider moved in the menu, and
        /// the next frame's picture has it. A finite number greater than nought: a caller that reads
        /// it from a file or a command line refuses anything else first.
        virtual void setGamma(float gamma) = 0;

        /// How many of the kernels a trace needs are made, waiting `patience` at most for the rest,
        /// and rethrowing what making one threw. The renderer starts making them as it is made, on
        /// threads of its own, and returns without them — ten seconds on a cold cache — so a host
        /// shows this on a loading screen rather than a window that stopped answering. Every call
        /// that traces waits for them first, so a host that never asks is right all the same, and
        /// held on its first trace instead.
        virtual JobProgress awaitKernels(std::chrono::milliseconds patience) = 0;

        /// Traces one frame; `setScene` first, which is an assert. Returns before the device has
        /// drawn it, so the caller can place the next one meanwhile, and `finishFrame` reads back
        /// what it came to, the reconstruction it resolved among it (`FrameResult::mReconstruction`):
        /// one road for that, the frame's own result. At most `sFramesInFlight` frames are in flight.
        /// What comes back at once is what the frame was described as, which the host holds no copy
        /// of. The air is carried on by the reading's clock, from where the last frame or `holdAir`
        /// left it.
        virtual FrameTraced renderFrame(const FrameRequest& request) = 0;

        /// Stands the air where `air` says it was carried, as of its sky clock, so the next frame at
        /// that clock is drawn in the air a harness recorded rather than wherever this session's
        /// frames carried it.
        virtual void holdAir(const AirClock& air) = 0;

        /// Closes the frame this frame's placements of the world opened, with no trace: where a
        /// placement is not followed by `renderFrame`, because the host refused the camera. Without
        /// it the frame stays open into the host's next, and every placement after takes one more
        /// command buffer. The frame is submitted and numbered and comes back with no report.
        /// Nothing where no placement opened a frame.
        virtual void skipFrame() = 0;

        /// How many frames were closed, by `renderFrame` or by `skipFrame`, which is the number the
        /// next one carries in `FrameResult::mFrame`.
        virtual std::uint64_t getFrameCount() const = 0;

        /// What the oldest unreported frame came to, waiting for it where it is still in flight, or
        /// nothing where every frame drawn was reported. After `renderFrame` that is the frame just
        /// submitted, which a screenshot and a test want.
        virtual std::optional<FrameResult> finishFrame() = 0;

        /// What the oldest unreported frame came to, waiting only where the ring has no room for
        /// the frame about to be placed, or nothing where none has finished. Before `placeScene`,
        /// this is what keeps `sFramesInFlight` in flight: the frame behind stays on the device while the
        /// next is placed, and the report is the frame before it. `finishFrame` there instead waits
        /// the frame behind out on every frame, so the device idles from its last pass until the
        /// next placement is submitted — a gap a device-bound frame pays in full.
        virtual std::optional<FrameResult> collectFrame() = 0;

        /// Shows the frame `renderFrame` just produced. A surface that stopped matching the window
        /// is remade by the next `showIn`, which the host calls every frame; no answer comes back
        /// here, because the renderer keeps that fact itself. No window is an assert.
        virtual void presentFrame() = 0;

        /// Multi-line report: the device and what it can trace with.
        virtual std::string describeDevice() const = 0;

        /// The knobs the frames are traced under now: what the renderer was made with, and then
        /// whatever a setting moved since. The one copy, so a stop that writes a picture by the
        /// same rules reads the rules the game draws by.
        virtual const RenderProfile& getProfile() const = 0;

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

    protected:
        Renderer() = default;
    };
}
