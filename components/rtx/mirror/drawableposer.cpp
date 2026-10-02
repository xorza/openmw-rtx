#include "drawableposer.hpp"

#include <osg/Drawable>

#include "mirrorpass.hpp"

namespace Rtx
{
    DrawablePoser::DrawablePoser(Traversals& traversals)
        : mTraversals(traversals)
    {
        mCull.setFrameStamp(mStamp);
    }

    void DrawablePoser::pose(osg::Drawable& drawable, const osg::FrameStamp& frame)
    {
        if (mFrame != frame.getFrameNumber())
        {
            mFrame = frame.getFrameNumber();

            const unsigned int number = mTraversals.next();
            mCull.setTraversalNumber(number);
            mStamp->setFrameNumber(number);
            mStamp->setReferenceTime(frame.getReferenceTime());
            mStamp->setSimulationTime(frame.getSimulationTime());
        }

        drawable.accept(mCull);
    }
}
