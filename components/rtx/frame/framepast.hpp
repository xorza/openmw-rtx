#pragma once

#include <cstdint>
#include <string>

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

        /// Why, as bits of `Cause`: what the log says of a reset, which a picture that jumped is
        /// read against.
        std::uint8_t mCauses = 0;

        enum Cause : std::uint8_t
        {
            Cut = 1,
            Worldspace = 2,
            Resized = 4,
            NewWorld = 8,
        };

        /// What a loss the host told costs.
        static constexpr FramePast of(const HistoryLoss loss)
        {
            return FramePast{
                .mReprojectionLost = loss != HistoryLoss::None,
                .mEyeLost = loss != HistoryLoss::None,
                .mWaterLost = loss == HistoryLoss::Worldspace,
                .mCauses = static_cast<std::uint8_t>(loss == HistoryLoss::Cut ? Cut
                        : loss == HistoryLoss::Worldspace                     ? Worldspace
                                                                              : 0),
            };
        }

        /// What a new extent or upscale mode costs: the reprojection alone. An eye adapted to the
        /// picture at one size is adapted to it at another, so a mode changed in the menu does not
        /// snap the brightness.
        static constexpr FramePast resized() { return FramePast{ .mReprojectionLost = true, .mCauses = Resized }; }

        /// What a new world costs, and the first frame a renderer traces: everything.
        static constexpr FramePast everything()
        {
            return FramePast{ .mReprojectionLost = true, .mEyeLost = true, .mWaterLost = true, .mCauses = NewWorld };
        }

        /// Whether any history is lost.
        constexpr bool losesAny() const { return mReprojectionLost || mEyeLost || mWaterLost; }

        constexpr FramePast& operator|=(const FramePast& other)
        {
            mReprojectionLost = mReprojectionLost || other.mReprojectionLost;
            mEyeLost = mEyeLost || other.mEyeLost;
            mWaterLost = mWaterLost || other.mWaterLost;
            mCauses = static_cast<std::uint8_t>(mCauses | other.mCauses);
            return *this;
        }

        bool operator==(const FramePast& other) const = default;
    };

    /// **A reset as the log says it**: its causes and what each history it cost, "a cut and a new
    /// extent: the reprojection and the eye". Empty where nothing is lost.
    std::string describe(const FramePast& past);
}
