#include "presentation.hpp"

#include <algorithm>
#include <cstdint>

namespace Misc
{
    namespace
    {
        /// `numerator / denominator` to the nearest whole number, halves up, for the positive
        /// sizes `present` divides: exact, where a float would round a 16K side.
        int divideRounded(std::int64_t numerator, std::int64_t denominator)
        {
            return static_cast<int>((2 * numerator + denominator) / (2 * denominator));
        }
    }

    osg::Vec2f Presentation::toFrame(const osg::Vec2f drawable) const
    {
        osg::Vec2f frame;
        for (int axis = 0; axis < 2; ++axis)
        {
            const float along = (drawable[axis] - static_cast<float>(mShownOrigin[axis]))
                * static_cast<float>(mFrame[axis]) / static_cast<float>(mShownSize[axis]);
            frame[axis] = std::clamp(along, 0.f, static_cast<float>(mFrame[axis]));
        }
        return frame;
    }

    osg::Vec2f Presentation::toDrawable(const osg::Vec2f frame) const
    {
        osg::Vec2f drawable;
        for (int axis = 0; axis < 2; ++axis)
            drawable[axis] = static_cast<float>(mShownOrigin[axis])
                + frame[axis] * static_cast<float>(mShownSize[axis]) / static_cast<float>(mFrame[axis]);
        return drawable;
    }

    Presentation present(const osg::Vec2i asked, const osg::Vec2i drawable)
    {
        Presentation presentation;
        presentation.mDrawable = osg::Vec2i(std::max(drawable.x(), 1), std::max(drawable.y(), 1));
        presentation.mFrame = asked.x() > 0 && asked.y() > 0 ? asked : presentation.mDrawable;

        const std::int64_t frameX = presentation.mFrame.x();
        const std::int64_t frameY = presentation.mFrame.y();
        const std::int64_t drawableX = presentation.mDrawable.x();
        const std::int64_t drawableY = presentation.mDrawable.y();

        // The side whose ratio of drawable to frame is the smaller one fills the drawable, and the
        // other follows at the frame's aspect, never under a pixel nor over the drawable.
        osg::Vec2i& shown = presentation.mShownSize;
        if (drawableX * frameY <= drawableY * frameX)
            shown = osg::Vec2i(presentation.mDrawable.x(),
                std::clamp(divideRounded(frameY * drawableX, frameX), 1, presentation.mDrawable.y()));
        else
            shown = osg::Vec2i(std::clamp(divideRounded(frameX * drawableY, frameY), 1, presentation.mDrawable.x()),
                presentation.mDrawable.y());

        presentation.mShownOrigin = (presentation.mDrawable - shown) / 2;
        return presentation;
    }

    float interfaceScale(const float setting, const osg::Vec2i frame, const osg::Vec2i displayPoints)
    {
        if (displayPoints.x() <= 0 || displayPoints.y() <= 0)
            return setting;

        const float across = std::max(static_cast<float>(frame.x()) / static_cast<float>(displayPoints.x()),
            static_cast<float>(frame.y()) / static_cast<float>(displayPoints.y()));
        return setting * std::max(1.f, across);
    }
}
