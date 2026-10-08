#pragma once

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

        /// What was kept since the last `clear`, for the owner to hand over.
        std::vector<osg::ref_ptr<const osg::Referenced>>& get() { return mHeld; }

        /// Drops every reference kept, where the caller stands: an owner with nothing to hand it to.
        void clear() { mHeld.clear(); }

    private:
        std::vector<osg::ref_ptr<const osg::Referenced>> mHeld;
    };
}
