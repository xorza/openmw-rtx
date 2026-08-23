#include "stage.hpp"

#include <osg/Camera>
#include <osg/FrameStamp>
#include <osg/Group>
#include <osg/Stats>

#include <osgGA/EventQueue>

#include <osgUtil/UpdateVisitor>

namespace MWRender
{
    Stage::Stage() = default;

    Stage::~Stage() = default;

    void Stage::adopt(osg::Camera& camera, osg::FrameStamp& frameStamp, osgGA::EventQueue& events,
        osgUtil::UpdateVisitor& updateVisitor, osg::Stats& stats)
    {
        mCamera = &camera;
        mFrameStamp = &frameStamp;
        mEvents = &events;
        mUpdateVisitor = &updateVisitor;
        mStats = &stats;
    }

    osg::Camera& Stage::getCamera() const
    {
        return *mCamera;
    }

    osg::FrameStamp& Stage::getFrameStamp() const
    {
        return *mFrameStamp;
    }

    osgGA::EventQueue& Stage::getEvents() const
    {
        return *mEvents;
    }

    osgUtil::UpdateVisitor& Stage::getUpdateVisitor() const
    {
        return *mUpdateVisitor;
    }

    osg::Stats& Stage::getStats() const
    {
        return *mStats;
    }

    osg::Group& Stage::getSceneRoot() const
    {
        return *mSceneRoot;
    }

    void Stage::setSceneRoot(osg::Group& root)
    {
        mSceneRoot = &root;
    }
}
