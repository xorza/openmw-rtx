#pragma once

#include <cstdint>

#include <osg/ref_ptr>

namespace osg
{
    class Geometry;
    class StateSet;
}

namespace SceneUtil
{
    osg::ref_ptr<osg::Geometry> createWaterGeometry(float size, std::int32_t segments, float textureRepeats);

    osg::ref_ptr<osg::StateSet> createSimpleWaterStateSet(float alpha, std::int32_t renderBin);
}
