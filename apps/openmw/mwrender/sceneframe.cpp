#include "sceneframe.hpp"

#include <components/sky/sundisc.hpp>

#include "skystate.hpp"

namespace MWRender
{
    osg::Vec4f sunDiscOf(const SkyState& sky, const WorldState& world)
    {
        return sky.mWeatherRan ? osg::Vec4f(Sky::sunDiscPosition(sky.mSunDirection), 0.f) : world.mSunLightPosition;
    }
}
