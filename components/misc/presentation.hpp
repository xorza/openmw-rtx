#pragma once

#include <string_view>

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

        /// How many frame pixels a unit of the interface takes: `setting` times `displayScale`, the
        /// window pixels the desktop gives a point of its own interface, over the window pixels a
        /// frame pixel covers. So the interface keeps one size on the display, whatever the frame
        /// it is drawn into and the window that shows it, as the desktop's own interface does.
        float interfaceScale(float setting, float displayScale) const;

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
    /// leftover halved and rounded down, by the rule `MWRender::ScreenshotManager` cuts by; worked in
    /// whole numbers where it works in double, so the two can part by a pixel where the double
    /// lands a hair under a whole one. Sides of nought or less are asked of nothing.
    Crop cropToAspect(osg::Vec2i frame, osg::Vec2i asked);

    /// Which of the three a resolution menu offers stands picked: the display's own, a mode the
    /// display lists, or two sides typed in.
    enum class ResolutionPick
    {
        Native,
        Listed,
        Custom,
    };

    /// `[Video] resolution x/y` as a menu picked it: nought by nought for Native, which `present`
    /// reads as the drawable; for a listed mode, the two sides `listed` opens with, as
    /// `getResolutionText` writes them, and nought by nought where it opens with none; and `custom`
    /// for sides typed in.
    osg::Vec2i resolutionPicked(ResolutionPick pick, std::string_view listed, osg::Vec2i custom);

    /// How a menu shows `stored`, the setting: Native for a side of nought, a listed mode where
    /// `listed` says the display lists one with these sides, and typed in otherwise.
    ResolutionPick resolutionPickOf(osg::Vec2i stored, bool listed);
}
