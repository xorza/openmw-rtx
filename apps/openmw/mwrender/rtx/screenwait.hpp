#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string_view>

#include <components/rtx/common/jobprogress.hpp>

namespace Loading
{
    class Listener;
}

namespace MWRender
{
    /// How a wait under the loading screen ended.
    enum class ScreenWait : std::uint8_t
    {
        Done,

        /// The player asked to quit, and the job is left running: its owner stops it.
        Quit,
    };

    /// What `awaitOnScreen` waits for, and under what.
    struct ScreenWaitAsk
    {
        std::string_view mLabel;

        /// What the step does and why it takes its time, empty for none.
        std::string_view mNote;

        /// How far the job came, once it is done or the patience handed it passed.
        std::function<Rtx::JobProgress(std::chrono::milliseconds)> mAwait;

        /// Whether the player asked to quit, which the screen's own draw learns of the window.
        std::function<bool()> mQuits;
    };

    /// Waits a job out under the loading screen, and leaves it where the player asks to quit: a cold preparation
    /// takes minutes, and a close request was ignored for all of them.
    ///
    /// **Under a frame of the loading screen's, which every report asks for, the count moved or not**
    /// (`Loading::Listener::increaseProgress`): the draw pumps the window's events and beats the crash catcher's
    /// heart, and `setProgress` skips it unless the bar moved a pixel, so the tail of a preparation, the bar still for
    /// seconds on its largest files, ran with neither and into the hang limit. The screen paces its own draws.
    ScreenWait awaitOnScreen(Loading::Listener& listener, const ScreenWaitAsk& ask);
}
