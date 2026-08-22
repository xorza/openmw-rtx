#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <components/rtx/renderer.hpp>
#include <components/rtx/upscale.hpp>

#include "frametimes.hpp"
#include "posedactors.hpp"
#include "views.hpp"

namespace Rtx
{
    struct ValidationOptions;
}

namespace RtxTool
{
    class World;

    /// A profiling run, over a list of places.
    struct BenchRequest
    {
        /// Which suite this came from, for the report and the record. Empty where the views were
        /// named on the command line instead.
        std::string mSuite;

        /// The places to run, in the order they are run in.
        std::vector<View> mViews;

        std::filesystem::path mShaderDirectory;

        /// Where to write the run as a record, or empty for none.
        std::filesystem::path mJson;

        /// perf's control fifo, or empty where the run is not being profiled.
        ///
        /// A recording bounded by this holds the measured frames of every place and nothing
        /// between them, so what the profile attributes time to is what the report's figures came
        /// from. See `PerfControl`.
        std::filesystem::path mPerfControl;

        std::uint32_t mWidth = 1920;
        std::uint32_t mHeight = 1080;
        float mFieldOfView = 60.0f;
        int mRadius = 4;

        /// How many seconds of *world* each place is run for.
        ///
        /// **World time and not wall time, which is the whole of what makes two runs comparable.**
        /// The world steps a sixtieth of a second per frame however long the frame took, so ten
        /// seconds is six hundred frames — the same six hundred frames, with the same particles in
        /// the same places and the same sample in each pixel, on a build that draws them in four
        /// seconds and on one that takes twenty. A run against the clock would animate further on
        /// the faster build and measure a different scene.
        float mSeconds = 10.0f;

        /// Frames drawn and thrown away before each place is measured, in seconds of world.
        ///
        /// **This machine's GPU idles at 315 MHz and ramps under load**, and the first submits of a
        /// scene also pay for its residency. A cold frame has timed five times a warm one, which is
        /// wider than most changes worth measuring.
        float mWarmup = 1.0f;

        /// Measured frames per place, overriding `mSeconds` where it is not zero.
        std::uint32_t mFrames = 0;

        /// Whether the run is shown while it happens. A window presents through a mailbox
        /// swapchain, so it does not pace the loop; what it costs is one present per frame, and it
        /// is the only way to see that a place is being profiled facing a wall.
        bool mWindow = true;

        Rtx::Upscale mUpscale = Rtx::Upscale::Off;
        float mDelight = 1.0f;
        bool mFilter = true;
        std::optional<float> mExposure;

        std::string mWeather = "Clear";
        float mHour = 12.0f;

        ActorRequest mActors;

        /// How many frames each place is measured over, which is `mFrames` or what `mSeconds` comes
        /// to at the rate the world steps.
        std::uint32_t getMeasured() const;

        /// How many are drawn and discarded first.
        std::uint32_t getWarmup() const;
    };

    /// What one place came to.
    struct BenchPlace
    {
        std::string mView;
        std::string mCell;
        std::string mNote;

        /// What `setScene` cost: every bottom-level structure built and every texture uploaded.
        /// The same cost a cell arriving in the game pays.
        double mBuildMs = 0.0;

        std::uint32_t mFrames = 0;
        double mWallSeconds = 0.0;

        /// The whole per-frame cost, and the two shares of it worth telling apart.
        ///
        /// **`mTrace` is the renderer drawing and `mPlace` is the renderer being told what moved** —
        /// the top level rebuilt and every skinned mesh's structure refitted. What is left over is
        /// the harness standing in for the game: posing the actors, running the emitters and walking
        /// the graph again. Lumping the three would hide which of them a place is slow because of.
        FrameTimes mFrame;
        FrameTimes mTrace;
        FrameTimes mPlace;

        /// What the device itself says each stretch of the frame cost, most expensive first. Empty
        /// where the device cannot write timestamps.
        std::vector<GpuZone> mGpu;

        /// What fraction of primary rays hit something, as a percentage. A place profiled facing a
        /// wall is fast and means nothing, and this is what says so without opening a window.
        double mHitPercent = 0.0;

        Rtx::SceneStats mScene;
    };

    /// Runs `request` and reports. Returns a process exit status.
    int runBench(World& world, const Rtx::ValidationOptions& validation, const BenchRequest& request);
}
