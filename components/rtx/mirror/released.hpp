#pragma once

#include <iterator>
#include <span>
#include <vector>

#include <osg/Referenced>
#include <osg/ref_ptr>

namespace Rtx
{
    /// What the mirror lets go of that the game made — the roots it froze, and the drawables, state
    /// sets, images and callbacks it held — kept until its owner hands it to whatever releases the
    /// game's own (`SceneUtil::UnrefQueue`), so the last reference to an unloaded cell is not dropped,
    /// and its destructors run, on the frame thread. Cleared and refilled, so a frame that lets go of
    /// nothing allocates nothing.
    class Released
    {
    public:
        template <class T>
        void keep(const osg::ref_ptr<T>& object)
        {
            if (object != nullptr)
                mHeld.emplace_back(object);
        }

        /// What was kept since the last `take` or `clear`.
        std::span<const osg::ref_ptr<const osg::Referenced>> get() const { return mHeld; }

        bool isEmpty() const { return mHeld.empty(); }

        /// Everything kept, moved out for the owner to hand over in a buffer of exactly its size,
        /// and the sink left empty with its room: grown to the busiest frame so far and never
        /// shrunk, so a frame that lets go of one object allocates one slot and not the busiest
        /// frame's. Nothing, and no allocation, where nothing was kept.
        std::vector<osg::ref_ptr<const osg::Referenced>> take()
        {
            std::vector<osg::ref_ptr<const osg::Referenced>> taken(
                std::make_move_iterator(mHeld.begin()), std::make_move_iterator(mHeld.end()));
            mHeld.clear();
            return taken;
        }

        /// Moves everything `from` kept into this sink, and leaves `from` empty with its room: a
        /// picture's sweep handed on with the world's.
        void keepAll(Released& from)
        {
            mHeld.insert(
                mHeld.end(), std::make_move_iterator(from.mHeld.begin()), std::make_move_iterator(from.mHeld.end()));
            from.mHeld.clear();
        }

        /// Drops every reference kept, where the caller stands: an owner with nothing to hand it to.
        void clear() { mHeld.clear(); }

    private:
        std::vector<osg::ref_ptr<const osg::Referenced>> mHeld;
    };
}
