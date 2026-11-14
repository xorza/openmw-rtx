#pragma once

#include <optional>
#include <span>

#include <apps/openmw/mwrender/rtx/framereport.hpp>
#include <apps/rtxtool/instruments/frametimes.hpp>
#include <apps/rtxtool/model/benchrecord.hpp>
#include <apps/rtxtool/model/benchrun.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/world/weather.hpp>

namespace RtxTool
{
    class RunRecord;

    /// What a stop asked for and what it came to, beside the frame it drew. Named for the reason `FrameContext` is:
    /// each is read by one claim and by nothing else, so one value carries them past `StopWriter::write`,
    /// which reads only the frame series. Borrowed and valid for one stop.
    struct StopFacts
    {
        /// Every measured frame's figures, which only the frame series reads.
        const FrameSamples& mSamples;

        /// What the stop's route came to, which only `CrossingsAppend` reads.
        const Crossings& mCrossings;

        /// What the stop asked its camera to be, which only `CameraStands` reads.
        const Stand& mStand;

        /// The weather the stop named, which only `SkyNamed` reads.
        std::optional<Rtx::Weather> mWeather;

        /// How many frames the ring held at each submit, which only `FramesOverlap` reads.
        const Overlap& mOverlap;

        /// What the frames' zones came to, what the hold's own clock read, and how long a hold
        /// the run asked for, which only `QueueHeld` reads.
        std::span<const GpuZone> mZones;
        const HoldTimes& mHold;
        double mHoldAskedMs = 0.0;

        /// What the measured frames wrote that was not finite, summed, which only `Finite` reads.
        const Rtx::NotFinite& mNotFinite;
    };

    /// Asks each of `checks` of what the frame `context` describes was handed and drew, and notes each answer, held
    /// or not, in `record`. Apart from the writer, whose buffers no check reads.
    void askChecks(const MWRender::FrameContext& context, const MWRender::FrameReport& report,
        std::span<const Check> checks, const StopFacts& facts, RunRecord& record);
}
