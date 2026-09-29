#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include <components/rtx/environment/frameworld.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/mirror/cells/cellworld.hpp>
#include <components/rtx/renderer/renderer.hpp>

namespace MWRender
{
    struct FrameContext;
    struct FrameReport;

    /// How fast a measured run steps the world unless it states otherwise, in frames a second: world
    /// time and not wall time, so ten seconds is the same six hundred frames on a build that draws
    /// them in four seconds and on one that takes twenty. Sixty because that is what the frame
    /// budget is written against. A default a run states (`RunSetup::mStep`), and never read in
    /// place of the step a run stated.
    inline constexpr float sStepRate = 60.0f;

    /// How long one of those frames stands for, which is what a measured frame tells the renderer.
    inline constexpr float sStepSeconds = 1.0f / sStepRate;

    /// Everything a run decides once, before anything is built, that the game's renderer reads:
    /// the harness fills one from its command line and a played session from `[RTX]`. One struct
    /// and not a field list copied from the request to the renderer, so a knob added here reaches
    /// both hosts by being here.
    struct RunSetup
    {
        /// The knobs the frames are traced under, every one of them stated.
        Rtx::RenderProfile mProfile;

        /// Which validation layers the run asked for. Carried here and never in a settings file,
        /// for the reason `sValidationByDefault` gives.
        Rtx::ValidationOptions mValidation;

        /// How much world the mirror builds and what of it, as the run decided: the harness from
        /// its command line, a played session from `[RTX] distant land cells` and the paging's
        /// two. Here and not written into the registry by the harness, because the registry is the
        /// player's and a knob of a run travels with the run.
        Rtx::MirrorKnobs mMirror;

        /// Whether the window stays hidden, which saves a present per frame and nothing else.
        bool mHeadless = false;

        /// Whether the interface is drawn over the frame: the played game's and a watched window's.
        /// A run that writes a picture of the world draws none unless it asks, because whatever a
        /// script puts on the screen lands in the picture — a mod's notice in a window of its own,
        /// which no switch of the HUD's reaches.
        bool mInterface = true;

        /// How long every frame stands for, in seconds, or nothing to time each one off the wall.
        /// Everything the world animates steps by it, so ten seconds of world is six hundred frames
        /// on every machine, and two runs of one build are the same run — which is what every run
        /// that measures or writes a picture wants. A window somebody watches wants the wall, as
        /// the played game has it, or the world runs as fast as the card draws. A run's and never
        /// a setting's: a file that could state a step once turned a played game into a
        /// fixed-step run for good.
        std::optional<float> mStep;

        /// What one frame of world counts for where a run turns seconds into frames — a span, a
        /// flight, a turning sky: the stated step, or where the wall decides, the step a measured
        /// run states by default.
        float getWorldStep() const { return mStep.value_or(sStepSeconds); }

        /// Whether each walk waits for the cell it adopts, or nothing to let `mStep` decide: a
        /// stated step waits. Settled is what makes two processes draw one picture; a run timing the
        /// streaming path says no (`Rtx::CellRing::setSettled`).
        std::optional<bool> mSettled;

        /// `RendererOptions::mMemoryBudget`: the harness's, for a run that asks what a smaller
        /// card does, and never a played session's.
        std::optional<std::uint64_t> mMemoryBudget;

        /// Whether the renderer reads its shaders with their source in them, for a profiler that
        /// shows a shader's lines (`Rtx::shaderDirectory`). The harness's, and never a played
        /// session's: the driver's cache is keyed on the modules without it.
        bool mShaderSource = false;
    };

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
