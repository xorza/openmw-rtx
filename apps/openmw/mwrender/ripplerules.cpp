#include "ripplerules.hpp"

#include <osg/Vec3f>

#include <apps/openmw/mwbase/world.hpp>

namespace MWRender
{
    bool isWading(const MWBase::World& world, const MWWorld::ConstPtr& actor)
    {
        const osg::Vec3f at = actor.getRefData().getPosition().asVec3();
        return (world.isUnderwater(actor.getCell(), at) && !world.isSubmerged(actor)) || world.isWalkingOnWater(actor);
    }
}
