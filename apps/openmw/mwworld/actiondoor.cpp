#include "actiondoor.hpp"

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>

namespace MWWorld
{
    ActionDoor::ActionDoor(const MWWorld::Ptr& object)
        : Action(false, object)
    {
    }

    void ActionDoor::executeImp(const MWWorld::Ptr& actor)
    {
        MWBase::Environment::get().getWorld()->activateDoor(getTarget());
    }
}
