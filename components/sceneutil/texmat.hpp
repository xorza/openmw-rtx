#pragma once

#include <cstdint>

#include <osg/ref_ptr>

namespace osg
{
    class Matrixf;
    class Uniform;
    class StateSet;
}

namespace SceneUtil
{

    osg::ref_ptr<osg::Uniform> createTexMatUniform(std::uint32_t texUnit, const osg::Matrixf& mat);

    void setupTexMatForStateSet(osg::StateSet& stateset, std::uint32_t texUnit, const osg::Matrixf& mat);

    void initTexMatForStateSet(osg::StateSet& stateset);

}
