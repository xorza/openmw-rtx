#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <osg/Vec2f>

#include "viewschedule.hpp"

namespace osg
{
    class FrameStamp;
}

namespace Rtx
{
    class CellRing;
    class Released;
}

namespace MWRender
{
    class TracedOverlay;
    class TracedView;

    /// The pictures inside the interface that the ray tracer holds — the views and the map's
    /// overlay — and the views asked for since the last frame: a view is drawn in the next frame's
    /// window, after the world's placement and before its trace, where the copy of the tables a
    /// picture reads is the frame's own. Drawn where asked, a picture made a wait of every
    /// placement that could still follow it. The views are borrowed: the caller owns every one, and
    /// `forget` keeps that sound.
    class ViewQueue
    {
    public:
        /// A name no view of this queue has had, for a view's texture: MyGUI keys its textures by
        /// name, so each view names its own.
        std::string nameView() { return "rtx offscreen view " + std::to_string(mNamed++); }

        /// A view made, from its constructor: kept on the list of the pictures alive, which
        /// `findWorldView` answers from.
        void adopt(TracedView& view);

        /// Takes a view off every list, because it is going away — the one being drawn included.
        void forget(TracedView& view);

        /// Queues `view` for the next flush. Asked twice in one frame is drawn once.
        void redraw(TracedView& view);

        /// Draws the pictures asked for since the last flush: every subject's and up to
        /// `worldViews` of the world's, the rest waiting for the next. A fresh load asks for nine
        /// map tiles at once and a cell crossing for a row of three; a picture of a subject is
        /// never held back. A picture of the world whose ground `ring` has not stood yet waits for
        /// it — `TracedView::waitsForGround`. A subject is posed at `posing`, and what its sweep lets
        /// go of goes into `releasing` — `TracedView::draw`.
        void draw(std::uint32_t worldViews, const osg::FrameStamp& posing, const Rtx::CellRing& ring,
            Rtx::Released& releasing);

        /// Whether a flush is in progress, which a flush inside a flush must not be.
        bool isDrawing() const { return mSchedule.isDrawing(); }

        bool hasDeferred() const { return mSchedule.hasDeferred(); }

        /// The game has left the scene every picture of the world asked so far shows: the last of
        /// its cells unloaded, as a door, a teleport or a load does (`notifyWorldspaceChanged`).
        /// Every such picture still queued is given up (`TracedView::abandon`).
        void leaveScene();

        /// The picture of the world taken straight down over `over`, or null where none is alive:
        /// the local map's tile of the cell the point is in, found by the renderer that drew it.
        TracedView* findWorldView(const osg::Vec2f& over) const;

        /// An overlay made and going, from its constructor and its destructor.
        void adoptOverlay(TracedOverlay& overlay);
        void forgetOverlay(TracedOverlay& overlay);

        /// Finishes every overlay's paints whose pictures have come back: once a frame, after the
        /// frame behind was collected, which is what brought them.
        void finishOverlays();

    private:
        /// How many views `nameView` has named.
        unsigned int mNamed = 0;

        /// Every picture alive, in the order made.
        std::vector<TracedView*> mViews;

        /// Which of them are asked for and in what order they are drawn.
        ViewSchedule<TracedView> mSchedule;

        /// Every overlay alive, in the order made. The game makes one.
        std::vector<TracedOverlay*> mOverlays;
    };
}
