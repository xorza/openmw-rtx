#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <osg/Vec2i>

#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/renderer/shaderdirectory.hpp>
#include <components/rtx/world/frameworld.hpp>

#include "mirrorknobs.hpp"
#include "rtxbackend.hpp"

namespace MWRender
{
    struct FrameContext;
    struct FrameReport;

    /// Everything a run decides once, before anything is built, that the game's renderer reads:
    /// the harness fills one from its command line and a played session from `[RTX]`. One struct
    /// and not a field list copied from the request to the renderer, so a knob added here reaches
    /// both hosts by being here.
    struct RunSetup
    {
        /// The profile, the layers and the budget, which the renderer hands the backend whole.
        Rtx::RunProfile mRun;

        /// How much world the mirror builds and what of it, as the run decided: the harness from
        /// its command line, a played session from `[RTX] distant land cells` and the paging's
        /// two. Here and not written into the registry by the harness, because the registry is the
        /// player's and a knob of a run travels with the run.
        MirrorKnobs mMirror{};

        /// What draws the frames.
        RtxBackend mBackend = RtxBackend::Vulkan;

        /// Whether the window stays hidden, which saves a present per frame and nothing else.
        bool mHeadless = false;

        /// The window's size in pixels, where the run states one: the harness's, whose window shows
        /// the frame pixel for pixel. Nothing for a played session, whose window `[Video] window
        /// width/height` sizes in the desktop's unit.
        std::optional<osg::Vec2i> mWindowPixels{};

        /// Whether the interface is drawn over the frame: the played game's and a watched window's.
        /// A run that writes a picture of the world draws none unless it asks, because whatever a
        /// script puts on the screen lands in the picture — a mod's notice in a window of its own,
        /// which no switch of the HUD's reaches.
        bool mInterface = true;

        /// Whether each walk waits for the cell it adopts, or nothing to let the frame clock's stated
        /// step decide (`Misc::LoopClock::getStatedStep`): a stated step waits. Settled is what makes two processes
        /// draw one picture; a run timing the streaming path says no (`Rtx::CellRing::setSettled`).
        std::optional<bool> mSettled{};

        /// The shaders the renderer reads, where not the game's under the resources
        /// (`Rtx::shaderDirectory`): the harness's sets, which count their stores that were not
        /// finite into the census for every run but a measured one, and keep their source for a
        /// profiler that shows a shader's lines. Never a played session's: the driver's cache is
        /// keyed on the modules, and the game's carry neither.
        std::optional<Rtx::ShaderSet> mShaders{};

        /// Where the content store stands (`ContentStore::Store`), or nothing for none: a played
        /// session's under the user's cache directory, the harness's where `--content-cache` says,
        /// and none for a test's renderer, which computes every ask.
        std::optional<std::filesystem::path> mContentStore{};

        /// Which kernels the start waits for (`Rtx::KernelSet`): the ones a frame draws with for a
        /// played session, and every tuple's for a run that measures or compares, so two runs change
        /// kernel at no frame.
        Rtx::KernelSet mKernels = Rtx::KernelSet::Drawing;
    };

    /// **What the run was set up with, for the log**: the backend, the window, the layers, the
    /// breadcrumbs, the upscaler, the kernels waited for, the store and the memory, and whether the
    /// build validates by default. Said once, where the renderer is made.
    std::string describe(const RunSetup& setup);

    /// `validation` raised to what the environment asks of the layers.
    ///
    /// **The two finer levels, asked for by name and never on by themselves.** The build decides
    /// whether the layers load; these decide what they check, and each costs far more than the
    /// core checks do — synchronization validation tracks every access of every resource, and
    /// the GPU-assisted layer instruments every shader. They are here because the harness names
    /// a level on its command line and the game has none, and `Rtx::sValidationByDefault` says
    /// why two hosts of one renderer must not disagree about the layers. What they answer is
    /// the fault a core-clean run still ends in: a device lost with an address and nothing else.
    ///
    /// **The GPU-assisted layer takes the process down on its own**, which is why it is a level
    /// of its own and never paired with the other: over a window `vkWaitForFences` comes back
    /// `VK_ERROR_DEVICE_LOST` on three runs of four, somewhere inside a minute, with nothing
    /// wrong in the frame, and headless it has aborted inside the layer's own thread. So
    /// `OPENMW_RTX_SYNC_VALIDATION` is the one to reach for in the game, and
    /// `OPENMW_RTX_GPU_VALIDATION` is there for a session willing to tell the losses apart.
    /// Either raises the level whatever the build said, which is what lets a Release build be
    /// asked one question without being rebuilt. `OPENMW_RTX_WATCH_SUBMITS` is asked for by name
    /// for the same reason: a diagnostic the game has no command line to ask for.
    Rtx::ValidationOptions askedValidation(Rtx::ValidationOptions validation);

    /// The backend `OPENMW_RTX_BACKEND` names, and the device's where it names none.
    ///
    /// **Asked for by name, as the layers' finer levels are** (`askedValidation`): a
    /// run of the game on a machine with no device — upstream's integration tests on CI — has no
    /// command line of its own, and a setting would put a game that draws nothing one menu away
    /// from a player.
    RtxBackend askedBackend();

    /// What a played session is made with, where the harness installed nothing: what `[RTX]`
    /// leaves a player, through the one derivation (`RtxSettings`), and the played answer to
    /// everything else. The knobs a measurement turns — albedo, the filter, the
    /// exposure, the crossings — are a run's, handed to the constructor by the harness that
    /// makes one, and a settings file cannot reach them: one that could once turned a played
    /// game into a fixed-step run for good. A played session shows every frame and sums none,
    /// and measures its exposure off each. The layers are the build's, which
    /// `Rtx::sValidationByDefault` says is the one thing that should decide them for a session
    /// with no command line. The clock is the wall: the eye adapts in real time and the
    /// upscaler tunes itself against how fast a motion vector was travelled, so each reader
    /// times what it is about — a setting that could state a step once made a played game step
    /// by frames, and at two hundred of them a second the world ran three times over.
    RunSetup playedRunSetup(const std::filesystem::path& cachePath);

    /// A run the harness drives through this renderer, as the renderer sees it per frame: what
    /// the run asks of the frame ahead and what it is fed once the frame is drawn. What a run
    /// decides before anything is built — the window, the layers, the clock — is data beside it in
    /// `RtxSetup`, and what it does to the world between frames is the engine host's
    /// (`OMW::EngineHost::beforeFrame`) and never this renderer's. The harness implements it and
    /// owns it, and reads the run's answer once `Engine::go` has returned. A played session is a
    /// run as well, `PlayedRun`, so the renderer never asks which host it is under.
    ///
    /// **An interface, because the run is the harness's and the renderer is the game's.** The run
    /// reads the world through `MWBase::Environment` and writes pictures, sheets and records that
    /// nothing a player does ever asks for, so it links into the harness alone; what the renderer
    /// needs of it fits in the questions below.
    class RtxRun
    {
    public:
        virtual ~RtxRun() = default;

        /// Which sample the trace should take, or nothing while no stop is running: the stop's own
        /// count and not the game's frame number, which carries every frame a loading screen drew
        /// and would put two runs of one binary at different points in the Halton sequence.
        virtual std::optional<std::uint32_t> getSampleFrame() const = 0;

        /// How many frames have gone into the running sum, this one included, or nought where the
        /// stop is not averaging. `RtxTool::Schedule::mAccumulate` says what that is for.
        virtual std::uint32_t getAccumulated() const = 0;

        /// What the frame asks of the reconstruction and of the exposure in place of the profile's,
        /// or nothing for the profile's — `Rtx::FrameOptions` says why the two are carried per
        /// frame. A reference traces unfiltered and jittered, and the frames compared with it hold
        /// the exposure it measured, so a picture and its reference come off one renderer.
        virtual std::optional<Rtx::ReconstructionRequest> getReconstruction() const = 0;
        virtual std::optional<Rtx::ExposureRule> getExposure() const = 0;

        /// What the stop upscales by in place of what the settings or the command line asked, or
        /// nothing for that: a reference is traced with no upscaler, whatever the frame it is
        /// compared with is upscaled by. A change rebuilds the targets, as a resize does, so a run
        /// changes it at a stop's edge and not inside one.
        virtual std::optional<Rtx::Upscale> getUpscale() const = 0;

        /// Whether the stop wants the graph walked a second time, so it can report what that added.
        virtual bool wantsSecondWalk() const = 0;

        /// Whether the frame should leave its picture for its report — `FrameOptions::mReadBack`,
        /// which a run that hashes every frame asks for and nothing a player does ever does.
        virtual bool wantsFrameCopy() const = 0;

        /// Where the air's clocks are to stand this frame, in place of where they ran to, or nothing
        /// to let them run: what a stop naming a moment holds them at until its first counted
        /// frame, so a picture of the air somebody saw is drawn at the moment they saw it rather
        /// than at wherever this session's frames happened to carry it.
        virtual std::optional<Rtx::AirClock> getHeldAir() const = 0;

        /// Takes one traced frame, and with it whatever the device answered for an earlier one —
        /// `FrameReport::mResult`, set where an answer came back this frame. Every traced frame and
        /// not only the answered ones, because an answer comes back a frame later or two by
        /// whether the card had finished when the frame after asked, and a run that counted
        /// answers stood at different points of its sequence in two runs of one build. The two
        /// halves meet by frame number, `FrameReport::mFrame` and `FrameResult::mFrame`.
        virtual void frame(const FrameContext& context, const FrameReport& report) = 0;

        /// What the window's title says after the rate: where the run stands, as the run notes it
        /// — the weather and the hour, for a window whose keys turn both. Asked once a second,
        /// when the title is written; empty is a title of the rate alone. Into the run's own
        /// bytes, so nothing is allocated for a line the title bar shows.
        virtual std::string_view describeTitle() = 0;
    };

    /// The run a played session is: every answer the played one, and nothing noted from any
    /// frame. The renderer holds one for a session that installed no run of its own.
    class PlayedRun final : public RtxRun
    {
    public:
        std::optional<std::uint32_t> getSampleFrame() const override { return std::nullopt; }
        std::uint32_t getAccumulated() const override { return 0; }
        std::optional<Rtx::ReconstructionRequest> getReconstruction() const override { return std::nullopt; }
        std::optional<Rtx::ExposureRule> getExposure() const override { return std::nullopt; }
        std::optional<Rtx::Upscale> getUpscale() const override { return std::nullopt; }
        bool wantsSecondWalk() const override { return false; }
        bool wantsFrameCopy() const override { return false; }
        std::optional<Rtx::AirClock> getHeldAir() const override { return std::nullopt; }
        void frame(const FrameContext& context, const FrameReport& report) override {}
        std::string_view describeTitle() override { return {}; }
    };

    /// What the harness installs before the engine starts, where the harness started this process:
    /// everything a run decides once, as data, and the run itself for what it answers per frame.
    /// Handed to `RtxRenderer`'s constructor beside the `RendererSpec`, as a pointer the harness
    /// keeps, so who owns it is readable off the signature. A played
    /// session installs none, and `RtxRenderer` makes one from `[RTX]` and the played answers.
    struct RtxSetup
    {
        /// The knobs the run was made with, every one of them stated — the request's own.
        RunSetup mSetup;

        /// The run this process drives. The harness's own, and it outlives the engine.
        RtxRun& mRun;
    };
}
