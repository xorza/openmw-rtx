#pragma once

#include <cstdint>

#include <osg/Vec3f>

#include <apps/openmw/mwworld/ptr.hpp>

#include "collisiontype.hpp"

namespace MWPhysics
{
    class RayCastingResult
    {
    public:
        bool mHit = false;
        osg::Vec3f mHitPos;
        osg::Vec3f mHitNormal;
        MWWorld::Ptr mHitObject;
    };

    class RayCastingInterface
    {
    public:
        virtual ~RayCastingInterface() = default;

        /// @param ignore Optional, a list of Ptr to ignore in the list of results. targets are actors to filter for,
        /// ignoring all other actors.
        virtual RayCastingResult castRay(const osg::Vec3f& from, const osg::Vec3f& to,
            const std::vector<MWWorld::ConstPtr>& ignore = {}, const std::vector<MWWorld::Ptr>& targets = {},
            std::int32_t mask = CollisionType_Default, std::int32_t group = 0xff) const = 0;

        RayCastingResult castRay(const osg::Vec3f& from, const osg::Vec3f& to, std::int32_t mask) const
        {
            return castRay(from, to, {}, {}, mask);
        }

        virtual RayCastingResult castSphere(const osg::Vec3f& from, const osg::Vec3f& to, float radius,
            std::int32_t mask = CollisionType_Default, std::int32_t group = 0xff) const = 0;

        /// Return true if actor1 can see actor2.
        virtual bool getLineOfSight(const MWWorld::ConstPtr& actor1, const MWWorld::ConstPtr& actor2) const = 0;
    };
}
