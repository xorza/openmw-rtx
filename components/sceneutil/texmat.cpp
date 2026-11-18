#include "texmat.hpp"

#include <cstdint>
#include <format>

#include <osg/Matrixf>
#include <osg/StateSet>
#include <osg/Uniform>

namespace SceneUtil
{

    osg::ref_ptr<osg::Uniform> createTexMatUniform(std::uint32_t texUnit, const osg::Matrixf& mat)
    {
        return new osg::Uniform(std::format("texMat{}", texUnit).c_str(), mat);
    }

    void setupTexMatForStateSet(osg::StateSet& stateset, std::uint32_t texUnit, const osg::Matrixf& mat)
    {
        stateset.getOrCreateUniform(std::format("texMat{}", texUnit), osg::Uniform::FLOAT_MAT4)->set(mat);
    }

    void initTexMatForStateSet(osg::StateSet& stateset)
    {
        constexpr std::uint32_t maxTexMat = 10;
        for (std::uint32_t i = 0; i < maxTexMat; ++i)
            setupTexMatForStateSet(stateset, i, osg::Matrixf{});
    }

}
