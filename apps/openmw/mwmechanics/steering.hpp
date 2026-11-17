#pragma once

#include <algorithm>
#include <cstdint>

#include <osg/Math>

namespace MWWorld
{
    class Ptr;
}

namespace MWMechanics
{

    // Max rotating speed, radian/sec
    inline float getAngularVelocity(const float actorSpeed)
    {
        constexpr float degreesPerFrame = 15.f;
        constexpr std::int32_t framesPerSecond = 60;
        const float baseAngularVelocity = osg::DegreesToRadians(degreesPerFrame * framesPerSecond);
        const float baseSpeed = 200;
        return baseAngularVelocity * std::max(actorSpeed / baseSpeed, 1.0f);
    }

    /// configure rotation settings for an actor to reach this target angle (eventually)
    /// @return have we reached the target angle?
    bool zTurn(const MWWorld::Ptr& actor, float targetAngleRadians,
        float epsilonRadians = static_cast<float>(osg::DegreesToRadians(0.5)));

    bool smoothTurn(const MWWorld::Ptr& actor, float targetAngleRadians, std::int32_t axis,
        float epsilonRadians = static_cast<float>(osg::DegreesToRadians(0.5)));

}
