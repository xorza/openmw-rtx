#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

namespace MWRender
{
    /// The order the pictures inside the interface are drawn in, the rules of `ViewQueue` apart from
    /// what a view is: any `View` that says whether it is a picture of the world (`isOfWorld`),
    /// whether it waits for its ground to stand (`waitsForGround`), and draws (`draw`) or is given
    /// up (`abandon`). The views are borrowed: `forget` keeps that sound.
    template <class View>
    class ViewSchedule
    {
    public:
        /// Takes a view off both lists, because it is going away — the one being drawn included.
        void forget(View& view)
        {
            std::erase(mDeferred, &view);

            // Nulled and not erased: a view can go from inside its own draw, while `draw` walks
            // this list, and replacing a pointer leaves that walk's iterators where they were.
            std::replace(mDrawing.begin(), mDrawing.end(), &view, static_cast<View*>(nullptr));
        }

        /// Queues `view` for the next flush. Asked twice in one frame is drawn once.
        void redraw(View& view)
        {
            if (std::find(mDeferred.begin(), mDeferred.end(), &view) == mDeferred.end())
                mDeferred.push_back(&view);
        }

        /// Draws the pictures asked for since the last flush: every subject's and up to `worldViews`
        /// of the world's, the rest waiting for the next. A picture of the world whose ground
        /// `ground` has not stood yet waits for it and counts nothing against the budget. Each
        /// view drawn is handed `drawn`.
        template <class Ground, class... Drawn>
        void draw(const std::uint32_t worldViews, const Ground& ground, Drawn&... drawn)
        {
            assert(mDrawing.empty() && "a flush inside a flush");

            mDrawing.swap(mDeferred);
            mDeferred.clear();

            std::uint32_t world = 0;
            for (View* view : mDrawing)
            {
                if (view == nullptr)
                    continue;

                if (view->isOfWorld() && (world == worldViews || view->waitsForGround(ground)))
                {
                    mDeferred.push_back(view);
                    continue;
                }

                if (view->isOfWorld())
                    ++world;

                view->draw(drawn...);
            }

            mDrawing.clear();
        }

        /// Whether a flush is in progress, which a flush inside a flush must not be.
        bool isDrawing() const { return !mDrawing.empty(); }

        bool hasDeferred() const { return !mDeferred.empty(); }

        /// Gives up every picture of the world still queued (`View::abandon`), for the reason
        /// `ViewQueue::leaveScene` gives; a subject's stays queued.
        void leaveScene()
        {
            assert(!isDrawing() && "a scene left inside a flush");

            std::erase_if(mDeferred, [](View* view) {
                if (!view->isOfWorld())
                    return false;

                view->abandon();
                return true;
            });
        }

    private:
        /// Pictures asked for and not yet drawn, in the order asked.
        std::vector<View*> mDeferred;

        /// The list a flush walks, swapped out of `mDeferred` so a redraw cannot grow what is being
        /// iterated. Kept, because this sits on the frame path.
        std::vector<View*> mDrawing;
    };
}
