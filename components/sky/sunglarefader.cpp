#include "sunglarefader.hpp"

#include <algorithm>

#include <osg/Math>
#include <osg/Vec4f>

#include <components/fallback/fallback.hpp>

namespace Sky
{
    SunGlareFader SunGlareFader::read()
    {
        const osg::Vec4f colour = Fallback::Map::getColour("Weather_Sun_Glare_Fader_Color") * 2.f;
        return SunGlareFader{
            .mColour = osg::Vec3f(std::min(1.f, colour.r()), std::min(1.f, colour.g()), std::min(1.f, colour.b())),
            .mMax = Fallback::Map::getFloat("Weather_Sun_Glare_Fader_Max"),
            .mAngleMax = osg::DegreesToRadians(Fallback::Map::getFloat("Weather_Sun_Glare_Fader_Angle_Max")),
        };
    }

    float SunGlareFader::atAngle(float angle) const
    {
        return 1.f - std::min(1.f, angle / mAngleMax);
    }
}
