#include "viewqueue.hpp"

#include <algorithm>

#include "tracedoverlay.hpp"
#include "tracedview.hpp"

namespace MWRender
{
    void ViewQueue::adopt(TracedView& view)
    {
        mViews.push_back(&view);
    }

    void ViewQueue::forget(TracedView& view)
    {
        std::erase(mViews, &view);
        mSchedule.forget(view);
    }

    void ViewQueue::redraw(TracedView& view)
    {
        mSchedule.redraw(view);
    }

    void ViewQueue::draw(const std::uint32_t worldViews, const osg::FrameStamp& posing, const Rtx::CellRing& ring,
        Rtx::Released& releasing)
    {
        mSchedule.draw(worldViews, ring, posing, releasing);
    }

    void ViewQueue::leaveScene()
    {
        // **A picture of a scene the game has left is not drawn**, over whatever the backend holds
        // now: a map tile asked as its cell was explored and still waiting when the player stepped
        // through a door came out of the interior, all clear colour, and the world map painted
        // that cell black. Given up here and not when the queue next reaches it, so a scene left
        // and come back to between two flushes finds the tile given up and asks for it again
        // (`LocalMap::requestMap`). Its paint lets go (`PendingPaints::finish`).
        mSchedule.leaveScene();
    }

    TracedView* ViewQueue::findWorldView(const osg::Vec2f& over) const
    {
        const auto found = std::find_if(
            mViews.begin(), mViews.end(), [&](const TracedView* view) { return view->coversFromAbove(over); });
        return found != mViews.end() ? *found : nullptr;
    }

    void ViewQueue::adoptOverlay(TracedOverlay& overlay)
    {
        mOverlays.push_back(&overlay);
    }

    void ViewQueue::forgetOverlay(TracedOverlay& overlay)
    {
        std::erase(mOverlays, &overlay);
    }

    void ViewQueue::finishOverlays()
    {
        for (TracedOverlay* overlay : mOverlays)
            overlay->finish();
    }
}
