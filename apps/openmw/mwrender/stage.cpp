#include "stage.hpp"

#include <osg/Camera>
#include <osg/Group>

#include <osgGA/EventQueue>

#include <osgUtil/IncrementalCompileOperation>
#include <osgUtil/UpdateVisitor>

#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>

namespace MWRender
{
    Stage::Stage(osgViewer::Viewer& viewer)
        : mViewer(viewer)
    {
    }

    Stage::~Stage() = default;

    osg::Camera& Stage::getCamera() const
    {
        return *mViewer.getCamera();
    }

    osg::FrameStamp& Stage::getFrameStamp() const
    {
        return *mViewer.getFrameStamp();
    }

    osgGA::EventQueue& Stage::getEvents() const
    {
        return *mViewer.getEventQueue();
    }

    osgUtil::UpdateVisitor& Stage::getUpdateVisitor() const
    {
        return *mViewer.getUpdateVisitor();
    }

    osg::Stats& Stage::getStats() const
    {
        return *mViewer.getViewerStats();
    }

    osg::Group& Stage::getSceneRoot() const
    {
        return *mViewer.getSceneData()->asGroup();
    }

    void Stage::setSceneRoot(osg::Group& root)
    {
        mViewer.setSceneData(&root);
    }

    void Stage::advance(double simulationTime)
    {
        mViewer.advance(simulationTime);
    }

    void Stage::eventTraversal()
    {
        mViewer.eventTraversal();
    }

    void Stage::updateTraversal()
    {
        mViewer.updateTraversal();
    }

    void Stage::renderTraversals()
    {
        mViewer.renderingTraversals();
    }

    void Stage::suspendDraw()
    {
        mViewer.stopThreading();
    }

    void Stage::resumeDraw()
    {
        mViewer.startThreading();
    }

    osgUtil::IncrementalCompileOperation* Stage::getCompileOperation() const
    {
        return mViewer.getIncrementalCompileOperation();
    }

    void Stage::setCompileOperation(osgUtil::IncrementalCompileOperation* operation)
    {
        mViewer.setIncrementalCompileOperation(operation);
    }

    void Stage::setScreenCapture(osgViewer::ScreenCaptureHandler& handler)
    {
        mScreenCapture = &handler;
        mViewer.addEventHandler(&handler);
    }

    void Stage::captureNextFrame()
    {
        mScreenCapture->setFramesToCapture(1);
        mScreenCapture->captureNextFrame(mViewer);
    }
}
