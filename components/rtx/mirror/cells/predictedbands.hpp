#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <vector>

#include <osg/Vec3f>

namespace Rtx
{
    /// **Where the game predicts the eye will stand**, a centre a band of cells the streamer stands hidden around: one
    /// slot a view the preloader holds (`MWRender::TracedTerrain::createView`), filled as it preloads into the view and
    /// emptied as it resets or lets go of it, which is the game's own word that a position is predicted no more. The
    /// preload worker writes it and the frame reads it, under its one lock; bounded by the views the preloader holds.
    class PredictedBands
    {
    public:
        using Slot = std::uint32_t;

        /// A slot of no centre, for a view made. Any thread.
        Slot open();

        /// The centre the slot's view was preloaded around. Any thread.
        void place(Slot slot, const osg::Vec3f& centre);

        /// No centre, for a view reset. Any thread.
        void clear(Slot slot);

        /// The slot back, for a view let go of. Any thread.
        void close(Slot slot);

        /// Appends every centre a slot holds, in the slots' order.
        void collect(std::vector<osg::Vec3f>& into) const;

    private:
        mutable std::mutex mMutex;
        std::vector<std::optional<osg::Vec3f>> mCentres;
        std::vector<bool> mOpen;
        std::vector<Slot> mFree;
    };
}
