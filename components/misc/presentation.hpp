#ifndef OPENMW_COMPONENTS_MISC_PRESENTATION_H
#define OPENMW_COMPONENTS_MISC_PRESENTATION_H

#include <osg/Vec2f>
#include <osg/Vec2i>

namespace Misc
{
    /// Where a frame of one size lands in a window of another. The renderer draws the world and the
    /// interface at `mFrame`, the resolution the settings ask for, and shows it scaled into
    /// `mShown*` with the aspect kept, and black beside it. Every reader of a size asks this, and
    /// none reads `[Video] resolution x/y` itself.
    struct Presentation
    {
        /// The window's size in pixels.
        osg::Vec2i mDrawable;

        /// What the renderer draws, in pixels.
        osg::Vec2i mFrame;

        /// Where the frame lands, in the drawable's pixels from its top left corner: centred, in
        /// whole pixels, and as large as the drawable lets it be at the frame's aspect.
        osg::Vec2i mShownOrigin;
        osg::Vec2i mShownSize;

        /// A point in the drawable's pixels, as a point of the frame. A point beside the frame, on
        /// a black bar, is the nearest point of the frame's edge, from 0 to `mFrame` inclusive.
        osg::Vec2f toFrame(osg::Vec2f drawable) const;

        /// A point of the frame, as a point in the drawable's pixels.
        osg::Vec2f toDrawable(osg::Vec2f frame) const;

        /// The drawable's pixels a pixel of the frame covers: the ratio of the side that fills the
        /// drawable, which is exact, where the other side's was rounded to a whole pixel.
        float shownScale() const;

        bool operator==(const Presentation& other) const = default;
    };

    /// The presentation of a frame `asked` in a window of `drawable` pixels. An `asked` with a side
    /// of nought is Native: the frame is the drawable. A side of `drawable` under one counts as one,
    /// because a window being minimised still has a frame to keep.
    Presentation present(osg::Vec2i asked, osg::Vec2i drawable);

    /// Where a picture at the aspect of another size is cut out of a frame.
    struct Crop
    {
        osg::Vec2i mOrigin;
        osg::Vec2i mSize;

        bool operator==(const Crop& other) const = default;
    };

    /// The middle of a `frame` at the aspect of `asked`: what a save's thumbnail is cut from, so a
    /// wide frame's thumbnail is not the whole of it squashed. In whole pixels and centred, the
    /// leftover halved and rounded down, as `MWRender::ScreenshotManager` cuts it; worked in whole
    /// numbers, where a float would round a 16K side. Sides of nought or less are asked of nothing.
    Crop cropToAspect(osg::Vec2i frame, osg::Vec2i asked);

    /// How many frame pixels a unit of the interface takes: `setting` times the frame pixels that
    /// would fall on one of the display's points if the frame filled the display, and never less
    /// than `setting`. So the interface keeps its size on the display where the frame is finer than
    /// the display's points, the way a display's own density scales it at its own resolution, and
    /// is drawn a frame pixel a unit where the frame is coarser, the way Morrowind drew it at any
    /// resolution. The window plays no part: the interface is part of the frame, and a window
    /// resized scales both together. A display with no size gives `setting`.
    float interfaceScale(float setting, osg::Vec2i frame, osg::Vec2i displayPoints);
}

#endif
