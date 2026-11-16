#pragma once

#include <array>

#include <components/sceneutil/nodecallback.hpp>

#include "postprocessor.hpp"

namespace osg
{
    class StateSet;
    class Viewport;
}

namespace MWRender
{
    class PostProcessor;
    class PingPongCull : public SceneUtil::NodeCallback<PingPongCull, osg::Node*, osgUtil::CullVisitor*>
    {
    public:
        explicit PingPongCull(PostProcessor* pp);
        ~PingPongCull() override;
        PingPongCull(const PingPongCull&) = delete;
        PingPongCull& operator=(const PingPongCull&) = delete;

        void operator()(osg::Node* node, osgUtil::CullVisitor* cv);

    private:
        std::array<osg::Matrixf, 2> mLastViewMatrix;
        osg::ref_ptr<osg::StateSet> mViewportStateset;
        osg::ref_ptr<osg::Viewport> mViewport;
        PostProcessor* mPostProcessor;
    };
}
