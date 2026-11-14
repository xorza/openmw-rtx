#include "frametimer.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <format>
#include <iterator>
#include <optional>
#include <utility>

#include <apps/openmw/mwrender/renderer.hpp>
#include <components/rtx/common/clock.hpp>

namespace MWRender
{
    namespace
    {
        /// How much frame time closes a second of the title. A second, so the figure moves as often
        /// as a clock's.
        constexpr double sSecondMs = 1000.0;
    }

    std::optional<double> FrameTimer::enter(const std::chrono::steady_clock::time_point now)
    {
        const std::optional<double> since
            = mEntered.has_value() ? std::optional(Rtx::since(*mEntered, now)) : std::nullopt;
        mEntered = now;
        return since;
    }

    void FrameTimer::addInterfacePresent(const std::chrono::steady_clock::duration took)
    {
        mPresentMs += std::chrono::duration<double, std::milli>(took).count();
        mInterfacePresent += took;
    }

    FrameTimer::Gap FrameTimer::gapTo(const std::chrono::steady_clock::time_point now, const AsideSpan& aside)
    {
        // In the clock's own ticks, so the three parts sum to the gap exactly. Both spans lie between the world's
        // frame leaving and the next arriving, and the interface's presents inside its own span (`AsideSpan`).
        const std::chrono::steady_clock::duration update = (now - mLeft) - aside.mInterface - aside.mHidden;
        const std::chrono::steady_clock::duration interface = aside.mInterface
            - std::exchange(mInterfacePresent, std::chrono::steady_clock::duration::zero());
        assert(update >= std::chrono::steady_clock::duration::zero());
        assert(interface >= std::chrono::steady_clock::duration::zero());

        using Milliseconds = std::chrono::duration<double, std::milli>;
        return Gap{ .mUpdateMs = Milliseconds(update).count(),
            .mInterfaceMs = Milliseconds(interface).count(),
            .mHiddenMs = Milliseconds(aside.mHidden).count() };
    }

    double FrameTimer::takePresent()
    {
        return std::exchange(mPresentMs, 0.0);
    }

    bool FrameTimer::addFrame(const double frameMs)
    {
        mSummedMs += frameMs;
        mWorstMs = std::max(mWorstMs, frameMs);
        ++mFrames;

        if (mSummedMs < sSecondMs)
            return false;

        mSecondMeanMs = mSummedMs / mFrames;
        mSecondWorstMs = mWorstMs;

        mSummedMs = 0.0;
        mWorstMs = 0.0;
        mFrames = 0;

        return true;
    }

    Misc::CStringView FrameTimer::writeTitle(const std::string_view note)
    {
        // Within the room the string was made with, so a title never allocates.
        const auto room = [&] { return static_cast<std::ptrdiff_t>(sTitleLengthMost - mTitle.size()); };
        mTitle.clear();

        std::format_to_n(std::back_inserter(mTitle), room(), "{}", std::string_view(sWindowTitle));
        if (mSecondMeanMs > 0.0)
            std::format_to_n(std::back_inserter(mTitle), room(), " - {:.0f} fps, {:.1f} ms, worst {:.1f} ms",
                sSecondMs / mSecondMeanMs, mSecondMeanMs, mSecondWorstMs);

        if (!note.empty())
            std::format_to_n(std::back_inserter(mTitle), room(), " - {}", note);

        return mTitle;
    }
}
