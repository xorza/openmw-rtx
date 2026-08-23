#pragma once

#include <osg/Transform>
#include <osg/Viewport>
#include <osgUtil/CullVisitor>
#include <osgUtil/RenderStage>
#include <osgUtil/StateGraph>

namespace RtxBridge
{
    /// A cull traversal that culls nothing and draws nothing.
    ///
    /// **For the things that answer only to an `osgUtil::CullVisitor`.** `SceneUtil::RigGeometry`
    /// and `SceneUtil::MorphGeometry` skin inside `accept`, by casting the visitor to one and
    /// handing it the pose they have just written; under anything else they hand back whatever the
    /// *rasterizer's* cull last computed, which for anyone off screen is the pose they had when
    /// last visible — wrong in every reflection they appear in and every shadow they cast. State
    /// sets that `SceneUtil::StateSetUpdater` only produces during cull are the other.
    ///
    /// **What it is not is a rendering cull.** Culling is off, because a ray tracer decides what
    /// exists and the answer is everything; the drawables it reaches are dropped rather than binned.
    /// Nor is it something for a whole graph to be walked with: `Terrain::TerrainDrawable::cull`
    /// puts the chunk in a render bin and never applies it, so the ground would simply disappear,
    /// and terrain LOD would be chosen from an eye point such a walk has no business having.
    ///
    /// A real `CullVisitor` rather than something claiming to be one: those casts are unchecked, and
    /// a plain visitor with its type set to `CULL_VISITOR` is wrong in the way that runs correctly
    /// on an untextured test quad.
    ///
    /// Whoever uses it owes it a frame stamp and a traversal number — the first because
    /// `SceneUtil::FrameTimeSource` reads the simulation time off it without checking there is one,
    /// the second because a skeleton will not move its bones for a number it has already seen.
    class PoseCull : public osgUtil::CullVisitor
    {
    public:
        PoseCull()
        {
            setCullingMode(osg::CullSettings::NO_CULLING);
            setStateGraph(new osgUtil::StateGraph);

            // Nothing will ever be drawn out of it, but `accept` pushes the drawable's own state set
            // on the way past, and a state set naming a render bin sends the visitor to
            // `_currentRenderBin` — which a good deal of Morrowind's content names, the error marker
            // a missing model resolves to among them.
            setRenderStage(new osgUtil::RenderStage);

            // `CullStack` reads the back of each of these without checking, so they are pushed once
            // and never popped: an empty stack is not a permissive one, it is a crash.
            pushViewport(new osg::Viewport(0, 0, 1, 1));
            pushProjectionMatrix(new osg::RefMatrix);
            pushModelViewMatrix(new osg::RefMatrix, osg::Transform::ABSOLUTE_RF);
        }

        /// The pose is read off the drawable afterwards, so there is nothing to do with it here.
        void apply(osg::Drawable&) override {}
    };
}
