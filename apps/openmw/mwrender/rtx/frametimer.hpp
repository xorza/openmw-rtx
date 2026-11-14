#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <components/misc/cstringview.hpp>

#include <apps/openmw/mwrender/framesaside.hpp>

namespace MWRender
{
    /// Where one frame began and ended inside the ray tracer, what it presented, and what the
    /// renderer says about its own speed. Stamps and not a report: the gap between one frame of the
    /// world leaving the renderer and the next arriving is `Rtx::Timing::Update` and what stood aside
    /// in it, and only the object that stamps both ends can measure it.
    class FrameTimer
    {
    public:
        /// Opens a frame. @return how long since the last frame opened, in milliseconds, and nothing
        /// on the first. Every frame the renderer is handed opens one, traced or not.
        std::optional<double> enter(std::chrono::steady_clock::time_point now);

        /// Stamps where the world's frame left the renderer. Every path out of one calls it, so what the next frame
        /// measures is the game's own loop and never the renderer's tail.
        void leave(std::chrono::steady_clock::time_point now) { mLeft = now; }

        /// The gap between the world's last frame leaving and the next arriving at `now`, in milliseconds, split
        /// into what stood `aside` — the interface's own frames less their presents, which `Present` holds, and
        /// what stood hidden — and the game's update, which is the rest.
        struct Gap
        {
            double mUpdateMs = 0.0;
            double mInterfaceMs = 0.0;
            double mHiddenMs = 0.0;
        };
        Gap gapTo(std::chrono::steady_clock::time_point now, const AsideSpan& aside);

        /// Adds what the world's present cost.
        void addPresent(double ms) { mPresentMs += ms; }

        /// Adds what one present of the interface's own frame cost: one of `Present`'s, and inside the
        /// interface's span, which a loading screen presents as often as it likes inside.
        void addInterfacePresent(std::chrono::steady_clock::duration took);

        /// What the presents since the last frame came to, and starts the sum again.
        double takePresent();

        /// Adds one frame's whole time to the second being counted. @return whether that second has
        /// run out, which is when the title is written again.
        ///
        /// **A second and not a frame**, because a figure that changes sixty times a second cannot be
        /// read. **The mean beside the worst**, because the mean alone is the figure that hides a
        /// stutter. A median would need the second's frames kept and sorted, and a sort landing on
        /// one frame in sixty is the spike a frame path is not allowed to carry.
        bool addFrame(double frameMs);

        /// The longest title, past which a note is cut short: more than a window's bar shows.
        static constexpr std::size_t sTitleLengthMost = 127;

        /// The window title for the second that ran out, as the C string a window takes. Never allocates:
        /// the text is written into this object's own string, made with room for `sTitleLengthMost`.
        ///
        /// @param note what the run says of where it stands, after the rest, or empty for nothing.
        Misc::CStringView writeTitle(std::string_view note);

    private:
        std::optional<std::chrono::steady_clock::time_point> mEntered;
        std::chrono::steady_clock::time_point mLeft;
        double mPresentMs = 0.0;
        std::chrono::steady_clock::duration mInterfacePresent{};

        /// The second being counted.
        double mSummedMs = 0.0;
        double mWorstMs = 0.0;
        std::uint32_t mFrames = 0;

        /// The last second that ran out, which the title describes: no frames until one has.
        double mSecondMeanMs = 0.0;
        double mSecondWorstMs = 0.0;

        /// What the window's title is written into, once a second.
        std::string mTitle = [] {
            std::string title;
            title.reserve(sTitleLengthMost);
            return title;
        }();
    };
}
