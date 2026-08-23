#ifndef GAME_RENDER_OFFSCREENVIEW_H
#define GAME_RENDER_OFFSCREENVIEW_H

#include <osg/Matrixf>
#include <osg/Node>
#include <osg/Vec3f>
#include <osg/Vec4f>

namespace MyGUI
{
    class ITexture;
}

namespace osg
{
    class Group;
}

namespace MWRender
{

    /// A picture of part of the world taken somewhere other than the eye, described in numbers the
    /// game already has.
    ///
    /// **Nothing here is an OpenGL decision.** Sample counts, colour formats, depth conventions,
    /// render order, cull modes and blend functions are the renderer's, worked out from what was
    /// asked for rather than passed down through it. A ray tracer answers the same request with
    /// rays, and none of those words mean anything to it.
    ///
    /// **It describes what the two character previews want and no more.** The local map wants an
    /// orthographic picture of a piece of the world that is already lit, which is a second unlike
    /// caller and a reason to grow this — when that caller exists. See `docs/rtx/gui.md` §4.
    struct OffscreenViewSpec
    {
        /// The subtree to draw. Every `redraw()` updates it and then draws it, so an update
        /// callback here is where a view that follows something inside it works out where to look
        /// from — the only moment at which it can.
        osg::Group& mScene;

        /// The image, in pixels. `setExtent` may go on to fill less of it than this.
        int mWidth = 0;
        int mHeight = 0;

        /// Only the nodes these bits select; `vismask.hpp`.
        unsigned int mMask = ~0u;

        /// Vertical, in degrees.
        float mFieldOfView = 0.f;
        float mNear = 1.f;
        float mFar = 10000.f;

        /// Behind everything, and seen through whatever the picture does not cover: the GUI
        /// composites the result rather than filling a widget with it.
        osg::Vec4f mClearColour;

        /// The only light there is, pointing towards where it comes from.
        osg::Vec3f mSunDirection;
        osg::Vec4f mSunDiffuse;
        osg::Vec4f mSunAmbient;
    };

    /// One such picture, alive for as long as the GUI shows it.
    class OffscreenView
    {
    public:
        virtual ~OffscreenView() = default;

        OffscreenView(const OffscreenView&) = delete;
        OffscreenView& operator=(const OffscreenView&) = delete;

        /// Where the picture is taken from. Takes effect on the next `redraw()`.
        virtual void setView(const osg::Matrixf& view) = 0;

        /// Fill only this much of the image and leave the rest at the clear colour. The inventory
        /// doll, whose window resizes while the texture behind it does not.
        virtual void setExtent(int width, int height) = 0;

        /// The subtree is not the same subtree any more — geometry added, removed or replaced,
        /// rather than moved. What that costs is the renderer's business; a pose change is not it.
        virtual void sceneChanged() = 0;

        /// Update the subtree and draw it again. Not per frame: a doll is redrawn when the player
        /// puts something on.
        virtual void redraw() = 0;

        /// What is at this point of the picture, in normalised device coordinates, as the path
        /// through the subtree to whatever was hit.
        ///
        /// **Against the drawn picture and not the current one.** Skinned geometry is
        /// double-buffered by frame number, so a query that means "what did I click on" has to name
        /// the frame that was drawn rather than the frame being built.
        virtual bool pick(float x, float y, osg::NodePath& hit) const = 0;

        /// What the GUI shows. **Y-up**, so the widget showing it inverts V; a renderer that would
        /// rather write the other way round still owes this one, because the alternative is every
        /// caller asking which renderer it got.
        virtual MyGUI::ITexture& getTexture() const = 0;

    protected:
        OffscreenView() = default;
    };

}

#endif
