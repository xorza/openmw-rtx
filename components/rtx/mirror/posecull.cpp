#include "posecull.hpp"

#include <osg/CullSettings>
#include <osg/Matrix>
#include <osg/Transform>
#include <osg/Viewport>
#include <osgUtil/RenderStage>
#include <osgUtil/StateGraph>

namespace Rtx
{
    PoseCull::PoseCull()
    {
        setCullingMode(osg::CullSettings::NO_CULLING);
        setStateGraph(new osgUtil::StateGraph);

        // Nothing will ever be drawn out of it, but `accept` pushes the drawable's own state set on
        // the way past, and a state set naming a render bin sends the visitor to `_currentRenderBin`
        // — which a good deal of Morrowind's content names, the error marker a missing model
        // resolves to among them.
        setRenderStage(new osgUtil::RenderStage);

        // `CullStack` reads the back of each of these without checking, so they are pushed once and
        // never popped: an empty stack is not a permissive one, it is a crash.
        pushViewport(new osg::Viewport(0, 0, 1, 1));
        pushProjectionMatrix(new osg::RefMatrix);
        pushModelViewMatrix(new osg::RefMatrix, osg::Transform::ABSOLUTE_RF);
    }

    void PoseCull::apply(osg::Node& node)
    {
        const NodeKind kind = mKinds.of(node);
        if (kind == NodeKind::ParticleProcessor || kind == NodeKind::ParticleUpdater)
            return;

        osgUtil::CullVisitor::apply(node);
    }
}
