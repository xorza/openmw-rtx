#pragma once

#include <optional>

#include <osg/FrameStamp>
#include <osg/ref_ptr>

#include "posecull.hpp"

namespace osg
{
    class Drawable;
}

namespace Rtx
{
    class Traversals;

    /// Brings a deforming drawable's own vertices to the pose the picture shows, for a CPU
    /// intersection that reads them: `SceneUtil::RigGeometry` and `MorphGeometry` pose that copy
    /// inside a cull traversal alone, and the trace culls nothing, because it skins on the device.
    /// The crosshair, activation and a script's ray met every actor in its bind pose without it.
    ///
    /// **Once per frame, however many rays reach the drawable**: a frame is posed at one number of
    /// the shared sequence, and both deforming drawables refuse to pose twice at one number. So
    /// a pose is paid per actor a ray reaches, where the rasterizer pays one per actor it draws.
    class DrawablePoser
    {
    public:
        /// @param traversals the sequence every walk of the world takes its numbers from.
        explicit DrawablePoser(Traversals& traversals);

        /// Poses `drawable` as the world stands at `frame`. A drawable that does not deform hears
        /// nothing.
        void pose(osg::Drawable& drawable, const osg::FrameStamp& frame);

    private:
        Traversals& mTraversals;
        PoseCull mCull;

        /// What `SceneUtil::FrameTimeSource` reads off the cull: the frame's simulation time, under
        /// the traversal number the frame is posed at.
        osg::ref_ptr<osg::FrameStamp> mStamp = new osg::FrameStamp;

        /// The frame the number in `mCull` was taken for, or nothing before the first.
        std::optional<unsigned int> mFrame;
    };
}
