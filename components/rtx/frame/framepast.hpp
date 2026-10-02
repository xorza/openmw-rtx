#pragma once

#include <cstdint>

namespace Rtx
{
    /// What happened between the last traced frame and this one that no motion vector describes,
    /// as the host tells it. Only the simulation knows: a cell load looks like a step from here.
    enum class HistoryLoss : std::uint8_t
    {
        None,

        /// A jump inside one worldspace: a door, a teleport, a clock jump. The water's wake stays,
        /// as the rasterizer keeps its ripples over a teleport.
        Cut,

        /// Another worldspace, whose water the old wake would ring on in.
        Worldspace,
    };

    /// Which histories a frame is traced without. **The one table every owner of a history reads
    /// its own column of**, so no owner works out for itself what the last frame was.
    struct FramePast
    {
        /// Whatever reprojects the frame before: the trace's surfaces, the denoisers and the
        /// upscaler.
        bool mReprojectionLost = false;

        /// The eye's adaptation: the measured exposure and the glare fader's eased share.
        bool mEyeLost = false;

        /// The water's wake.
        bool mWaterLost = false;

        /// What a loss the host told costs.
        static constexpr FramePast of(const HistoryLoss loss)
        {
            return FramePast{
                .mReprojectionLost = loss != HistoryLoss::None,
                .mEyeLost = loss != HistoryLoss::None,
                .mWaterLost = loss == HistoryLoss::Worldspace,
            };
        }

        /// What a new extent or upscale mode costs: the reprojection alone. An eye adapted to the
        /// picture at one size is adapted to it at another, so a mode changed in the menu does not
        /// snap the brightness.
        static constexpr FramePast resized() { return FramePast{ .mReprojectionLost = true }; }

        /// What a new world costs, and the first frame a renderer traces: everything.
        static constexpr FramePast everything()
        {
            return FramePast{ .mReprojectionLost = true, .mEyeLost = true, .mWaterLost = true };
        }

        constexpr FramePast& operator|=(const FramePast& other)
        {
            mReprojectionLost = mReprojectionLost || other.mReprojectionLost;
            mEyeLost = mEyeLost || other.mEyeLost;
            mWaterLost = mWaterLost || other.mWaterLost;
            return *this;
        }

        bool operator==(const FramePast& other) const = default;
    };
}
