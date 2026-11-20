#include "screenwait.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>

#include <components/loadinglistener/loadinglistener.hpp>
#include <components/rtx/common/jobprogress.hpp>

namespace MWRender
{
    ScreenWait awaitOnScreen(Loading::Listener& listener, const ScreenWaitAsk& ask)
    {
        Rtx::JobProgress progress = ask.mAwait(std::chrono::milliseconds::zero());
        if (progress.isDone())
            return ScreenWait::Done;

        listener.setLabel(std::string(ask.mLabel));
        if (!ask.mNote.empty())
            listener.setNote(ask.mNote);
        listener.setProgressRange(progress.mCount);

        constexpr std::chrono::milliseconds patience{ 8 };
        std::uint32_t shown = 0;
        ScreenWait ended = ScreenWait::Done;
        while (true)
        {
            const std::uint32_t made = std::max(shown, progress.mMade);
            listener.increaseProgress(made - shown);
            shown = made;
            if (progress.isDone())
                break;
            // After the draw, which is what reads the window's close request.
            if (ask.mQuits())
            {
                ended = ScreenWait::Quit;
                break;
            }
            progress = ask.mAwait(patience);
        }
        if (!ask.mNote.empty())
            listener.setNote({});
        return ended;
    }
}
