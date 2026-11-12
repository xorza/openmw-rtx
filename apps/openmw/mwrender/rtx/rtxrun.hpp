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
#include <components/rtx/renderer/shaderset.hpp>
#include <components/rtx/world/frameworld.hpp>

#include "mirrorknobs.hpp"
#include "rtxbackend.hpp"

namespace MWRender
{
    struct FrameContext;
    struct FrameReport;

    /// Everything a run decides once, before anything is built, that the game's renderer reads: the harness
    /// fills one from its command line and a played session from `[RTX]`, so a knob added here reaches both.
    struct RunSetup
    {
        /// The profile, the layers and the budget, which the renderer hands the backend whole.
        Rtx::RunProfile mRun;

        /// How much world the mirror builds and what of it. Here and not written into the registry by the
        /// harness, because the registry is the player's and a knob of a run travels with the run.
        MirrorKnobs mMirror{};

        /// What draws the frames.
        RtxBackend mBackend = RtxBackend::Vulkan;

        /// Whether the window stays hidden, which saves a present per frame and nothing else.
        bool mHeadless = false;

        /// The window's size in pixels, where the run states one: the harness's shows the frame pixel for
        /// pixel. Nothing for a played session, whose window `[Video] window width/height` sizes in the
        /// desktop's unit.
        std::optional<osg::Vec2i> mWindowPixels{};

        /// Whether the interface is drawn over the frame: the played game's and a watched window's. A run that
        /// writes a picture of the world draws none unless it asks, because whatever a script puts on the
        /// screen, which no HUD switch reaches, lands in the picture.
        bool mInterface = true;

        /// Whether each walk waits for the cell it adopts, or nothing to let the frame clock's stated
        /// step decide (`Misc::LoopClock::getStatedStep`): a stated step waits. Settled is what makes two processes
        /// draw one picture; a run timing the streaming path says no (`Rtx::CellRing::setSettled`).
        std::optional<bool> mSettled{};

        /// The shaders the renderer reads, where not the ones the game compiles in (`shippedShadersOf`): the
        /// harness's sets, which count non-finite stores into the census and keep their source for a profiler,
        /// and outlive the renderer. Never a played session's: the driver's cache is keyed on the modules, and
        /// the game's carry neither.
        std::optional<Rtx::ShaderSet> mShaders{};

        /// Where the content store stands (`ContentStore::Store`), or nothing for none: a played session's
        /// under the user's cache directory, the harness's where `--content-cache` says.
        std::optional<std::filesystem::path> mContentStore{};

        /// Which kernels the start waits for (`Rtx::KernelSet`): a played session's are the ones a frame
        /// draws with, a run that measures or compares waits for every tuple's so two runs change kernel at
        /// no frame.
        Rtx::KernelSet mKernels = Rtx::KernelSet::Drawing;
    };

    /// What the run was set up with, for the log; said once, where the renderer is made.
    std::string describe(const RunSetup& setup);

    /// `validation` raised to what the environment asks of the layers. The two finer levels are asked for by
    /// name and never on by themselves: each costs far more than the core checks, and the game has no command
    /// line to name one. The GPU-assisted layer takes the process down on its own (over a window
    /// `vkWaitForFences` returns `VK_ERROR_DEVICE_LOST` on three runs of four with nothing wrong in the
    /// frame), so `OPENMW_RTX_SYNC_VALIDATION` is the one to reach for in the game, and
    /// `OPENMW_RTX_GPU_VALIDATION` is for a session willing to tell the losses apart. Either raises the level
    /// whatever the build said, as do `OPENMW_RTX_WATCH_SUBMITS` and `OPENMW_RTX_BREADCRUMBS` for their
    /// diagnostics.
    Rtx::ValidationOptions askedValidation(Rtx::ValidationOptions validation);

    /// The backend `OPENMW_RTX_BACKEND` names, Vulkan where it names none. An environment variable and not a
    /// setting: a game on a machine with no device, as upstream's integration tests on CI, has no command
    /// line, and a setting would put a game that draws nothing one menu away from a player.
    RtxBackend askedBackend();

    /// What a played session is made with, where the harness installed nothing: what `[RTX]` leaves a
    /// player, through the one derivation (`RtxSettings`), and the played answer to everything else. The
    /// knobs a measurement turns are a run's and a settings file cannot reach them: one that could once
    /// turned a played game into a fixed-step run for good. The layers are the build's
    /// (`Rtx::sValidationByDefault`). The clock is the wall: the eye adapts in real time and the upscaler
    /// tunes itself against how fast a motion vector was travelled.
    RunSetup playedRunSetup(const std::filesystem::path& cachePath);

    /// A run the harness drives through this renderer, as the renderer sees it per frame: what the run asks
    /// of the frame ahead and what it is fed once the frame is drawn. What a run decides before anything is
    /// built is data beside it in `RtxSetup`, and what it does to the world between frames is the engine
    /// host's (`OMW::EngineHost::beforeFrame`). A played session is a run as well, `PlayedRun`, so the
    /// renderer never asks which host it is under. An interface because the run reads the world and writes
    /// pictures and records nothing a player asks for, so it links into the harness alone.
    class RtxRun
    {
    public:
        virtual ~RtxRun() = default;

        /// Which sample the trace should take, or nothing while no stop is running: the stop's own count and
        /// not the game's frame number, which carries a loading screen's frames and would put two runs of one
        /// binary at different points in the Halton sequence.
        virtual std::optional<std::uint32_t> getSampleFrame() const = 0;

        /// How many frames have gone into the running sum, this one included, or nought where the
        /// stop is not averaging. `RtxTool::Schedule::mAccumulate` says what that is for.
        virtual std::uint32_t getAccumulated() const = 0;

        /// What the frame asks of the reconstruction and of the exposure in place of the profile's, or nothing
        /// for the profile's (`Rtx::FrameOptions`). A reference traces unfiltered and jittered, and the frames
        /// compared with it hold the exposure it measured.
        virtual std::optional<Rtx::ReconstructionRequest> getReconstruction() const = 0;
        virtual std::optional<Rtx::ExposureRule> getExposure() const = 0;

        /// What the stop upscales by in place of the settings' or the command line's, or nothing for that: a
        /// reference is traced with no upscaler. A change rebuilds the targets, as a resize does, so a run
        /// changes it at a stop's edge.
        virtual std::optional<Rtx::Upscale> getUpscale() const = 0;

        /// Whether the stop wants the graph walked a second time, so it can report what that added.
        virtual bool wantsSecondWalk() const = 0;

        /// Whether the frame should leave its picture for its report — `FrameOptions::mReadBack`,
        /// which a run that hashes every frame asks for and nothing a player does ever does.
        virtual bool wantsFrameCopy() const = 0;

        /// Where the air's clocks are to stand this frame in place of where they ran to, or nothing to let them
        /// run: a stop naming a moment holds them there until its first counted frame.
        virtual std::optional<Rtx::AirClock> getHeldAir() const = 0;

        /// Takes one traced frame, and with it whatever the device answered for an earlier one
        /// (`FrameReport::mResult`). Every traced frame and not only the answered ones: an answer comes back
        /// a frame or two later by whether the card had finished, so a run counting answers stood at different
        /// points of its sequence in two runs of one build. The halves meet by frame number
        /// (`FrameReport::mFrame`, `FrameResult::mFrame`).
        virtual void frame(const FrameContext& context, const FrameReport& report) = 0;

        /// What the window's title says after the rate, asked once a second when the title is written; empty
        /// is a title of the rate alone. Into the run's own bytes, so nothing is allocated.
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
    /// Handed to `RtxRenderer`'s constructor beside the `RendererSpec`, as a pointer the harness keeps. A
    /// played session installs none, and `RtxRenderer` makes one from `[RTX]` and the played answers.
    struct RtxSetup
    {
        /// The knobs the run was made with, every one of them stated — the request's own.
        RunSetup mSetup;

        /// The run this process drives. The harness's own, and it outlives the engine.
        RtxRun& mRun;
    };
}
