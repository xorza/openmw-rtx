#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

#include <components/rtx/common/namedenum.hpp>

namespace Rtx
{
    /// Which of a measured frame's figures a row holds. `Wait` is the CPU standing still for the
    /// device — a wait near the frame is a device that cannot keep up, near nought a CPU that
    /// cannot — and `Finish` is the whole of collecting the frame behind, of which `Wait` is the
    /// largest share. `Walk` is the world being mirrored, with `Preprocess` the share of it spent
    /// computing from content that arrived on it. `Place` is the renderer being told what moved,
    /// split into `Bake`, `Textures` and `Upload` because its worst frame is hundreds of times its
    /// median. `Trace` and `Present` are the other two calls into the backend, `Interface` and `Hidden`
    /// what the wall spent between two frames of the world on frames that drew none, and `Update` is
    /// the rest of the loop, with `Sleep` the share of it the frame-rate limiter held the host for.
    /// `Frame` is the whole of it, from one trace's frame opening to the next. Timed rather than
    /// profiled, because most of what a call into the driver costs is inside the driver with no
    /// frame pointer to walk. Together they close the frame: `Frame` less the rest is under 0.05 ms
    /// at every place that stands still, and a row that does not close is a stretch nobody has named.
    enum class Timing : std::uint32_t
    {
        Frame,
        Finish,
        Wait,
        Walk,
        Preprocess,
        Place,
        Bake,
        Textures,
        Upload,
        Trace,

        /// The pictures inside the interface drawn this frame — a doll's walk and placement, a map
        /// tile's recording — which stand between the placement and the trace.
        Views,
        Present,
        Update,

        /// The interface's own frames between two of the world's — a loading screen's, a blocking message
        /// box's, a video's — from the first to the last, less their presents, which `Present` holds: the game
        /// behind its own screens (`MWRender::AsideSpan`).
        Interface,

        /// The loop's frames the window stood hidden through, minimized or covered, which draw nothing and
        /// cost nobody a frame: what a frame's judges take out of it.
        Hidden,

        /// The host held before the frame's input, by the driver or by the limiter: a share of
        /// `Update`, which is the whole gap between two frames and has the hold inside it.
        Sleep,

        /// How many there are, which `NamedEnum` holds the table below to: a timing added here and
        /// not there would index past `FrameSpend::mMs`.
        Count,
    };

    /// What a report heads each row with, and — with `Ms` after it — what the JSON names it. One
    /// table, from which the count and the walk are derived (`Rtx::NamedEnum`).
    inline constexpr NamedEnum sTimings{ std::array{
        std::pair{ Timing::Frame, std::string_view("frame") },
        std::pair{ Timing::Finish, std::string_view("finish") },
        std::pair{ Timing::Wait, std::string_view("wait") },
        std::pair{ Timing::Walk, std::string_view("walk") },
        std::pair{ Timing::Preprocess, std::string_view("preprocess") },
        std::pair{ Timing::Place, std::string_view("place") },
        std::pair{ Timing::Bake, std::string_view("bake") },
        std::pair{ Timing::Textures, std::string_view("textures") },
        std::pair{ Timing::Upload, std::string_view("upload") },
        std::pair{ Timing::Trace, std::string_view("trace") },
        std::pair{ Timing::Views, std::string_view("views") },
        std::pair{ Timing::Present, std::string_view("present") },
        std::pair{ Timing::Update, std::string_view("update") },
        std::pair{ Timing::Interface, std::string_view("interface") },
        std::pair{ Timing::Hidden, std::string_view("hidden") },
        std::pair{ Timing::Sleep, std::string_view("sleep") },
    } };

    inline constexpr std::size_t sTimingCount = sTimings.mNames.size();

    inline constexpr std::size_t indexOf(const Timing timing)
    {
        return static_cast<std::size_t>(timing);
    }

    /// What one measured frame spent on the host, by phase — an array over `Timing`, because that
    /// is what `FrameSamples` holds and what it becomes, and a signature of six doubles in a row is
    /// six chances to hand them over in the wrong order. In the core rather than the bench,
    /// because `SceneUploader` writes `Bake`, `Textures` and `Upload`.
    struct FrameSpend
    {
        std::array<double, sTimingCount> mMs{};

        double& at(const Timing timing) { return mMs[indexOf(timing)]; }
        double at(const Timing timing) const { return mMs[indexOf(timing)]; }

        /// The frame as it stood on the screen: what stood `Hidden` taken out of `Frame` and out of the stretches,
        /// since nobody saw it, so a frame's judges, its leading stretch among them, read only what was seen.
        FrameSpend seen() const
        {
            FrameSpend seen = *this;
            seen.at(Timing::Frame) -= at(Timing::Hidden);
            seen.at(Timing::Hidden) = 0.0;
            return seen;
        }

        /// What this frame came to once `arriving` opened: its own figures with the arriving frame's `Frame`,
        /// `Update`, `Interface`, `Hidden` and `Sleep`. `Frame` runs from one frame's opening to the next, so it
        /// holds the gap the next frame arrived through and this frame's work, which is the span a hitch is read in.
        FrameSpend closedBy(const FrameSpend& arriving) const
        {
            FrameSpend closed = *this;
            for (const Timing timing :
                { Timing::Frame, Timing::Update, Timing::Interface, Timing::Hidden, Timing::Sleep })
                closed.at(timing) = arriving.at(timing);
            return closed;
        }
    };

    /// The three largest stretches `spend` is made of, largest first and a tie in `Timing`'s order, as "update 14.5
    /// finish 2.2 walk 0.6": the figures that sum to the frame, and neither the frame itself nor a share of one of
    /// them, since `Wait` is most of `Finish`, `Upload` most of `Place` and `Sleep` part of `Update`.
    std::string describeStretches(const FrameSpend& spend);

    /// The largest of the stretches `describeStretches` names: where most of the frame went.
    Timing leadingStretch(const FrameSpend& spend);
}
