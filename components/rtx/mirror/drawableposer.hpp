#pragma once

#include <optional>

#include <osg/FrameStamp>
#include <osg/ref_ptr>

#include "nodekind.hpp"
#include "posecull.hpp"

namespace osg
{
    class Drawable;
}

namespace Rtx
{
    class Traversals;

    /// Brings a deforming drawable's own vertices to the pose the picture shows, for a CPU intersection that reads
    /// them: `SceneUtil::RigGeometry` and `MorphGeometry` pose that copy inside a cull traversal alone, and the trace
    /// culls nothing, because it skins on the device. Without it the crosshair, activation and a script's ray met every
    /// actor in its bind pose.
    ///
    /// **Once per frame, however many rays reach the drawable**: both deforming drawables refuse to pose twice at one
    /// frame number, so a pose is paid per actor a ray reaches, where the rasterizer pays one per actor it draws.
    class DrawablePoser
    {
    public:
        /// @param traversals the sequence every walk of the world takes its numbers from.
        explicit DrawablePoser(Traversals& traversals);

        /// Poses `drawable` as the world stands at `frame`. A drawable that does not deform hears nothing, and neither
        /// does a rig no update traversal bound to its skeleton yet: a part attached in this frame's logic binds in the
        /// frame's update, and posed before it the rig searches a path that holds itself alone. Its ray meets the bind
        /// pose, as the rasterizer's does.
        void pose(osg::Drawable& drawable, const osg::FrameStamp& frame);

    private:
        Traversals& mTraversals;
        PoseCull mCull;
        NodeKinds mKinds;

        /// What `SceneUtil::FrameTimeSource` reads off the cull: the frame's simulation time, under
        /// the traversal number the frame is posed at.
        osg::ref_ptr<osg::FrameStamp> mStamp = new osg::FrameStamp;

        /// The frame the number in `mCull` was taken for, or nothing before the first.
        std::optional<unsigned int> mFrame;
    };
}
