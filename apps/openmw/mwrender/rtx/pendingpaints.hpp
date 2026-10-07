#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <apps/openmw/mwrender/offscreenview.hpp>
#include <components/sceneutil/imageregion.hpp>

namespace osg
{
    class Image;
}

namespace MWRender
{
    /// The tiles asked for the map's overlay whose pictures have not come back off the device yet,
    /// in the order asked. A tile's picture comes back frames after it was traced, so a paint is
    /// kept until its copy is there (`TracedOverlay::finish`).
    ///
    /// **Kept until the copy lands, whoever else lets go of the tile.** A paint holds its view, and
    /// a view that stands is drawn: `ViewQueue` draws it once the ring stands the ground under it,
    /// or once the ring stops asking for that ground. A paint dropped where the local map let go
    /// first left a cell crossed quickly after a load black on the world map for the session. What
    /// does stop one is the overlay's own reset — a clear, a save read in — which drops every
    /// paint (`clear`).
    class PendingPaints
    {
    public:
        /// Asks for `tile`'s copy and keeps the paint until it lands. The last word for a rectangle
        /// wins, so a cell crossed twice before its picture came back is painted once, from the
        /// later tile.
        void add(const SceneUtil::ImageRegion& destination, std::shared_ptr<OffscreenView> tile)
        {
            // Asked for here and read at `finish`: the copy costs a transfer off the device per
            // redraw of the tile from now on, which is the price upstream paid to copy the overlay
            // back.
            tile->keepCopy();

            const auto same = std::find_if(mPending.begin(), mPending.end(),
                [&](const Pending& pending) { return pending.mDestination == destination; });
            if (same != mPending.end())
                same->mTile = std::move(tile);
            else
                mPending.push_back(Pending{ .mDestination = destination, .mTile = std::move(tile) });
        }

        /// Hands every paint whose picture has come back to `paint`, as its destination and the
        /// picture, and lets go of it. The rest wait.
        template <class Paint>
        void finish(Paint&& paint)
        {
            std::erase_if(mPending, [&](Pending& pending) {
                const osg::Image* drawn = pending.mTile->getCopy();
                if (drawn == nullptr)
                    return false;

                paint(pending.mDestination, *drawn);
                return true;
            });
        }

        void clear() { mPending.clear(); }

        std::size_t size() const { return mPending.size(); }

    private:
        struct Pending
        {
            SceneUtil::ImageRegion mDestination;
            std::shared_ptr<OffscreenView> mTile;
        };

        std::vector<Pending> mPending;
    };
}
