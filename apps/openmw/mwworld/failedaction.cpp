#include "failedaction.hpp"

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/windowmanager.hpp>
#include <apps/openmw/mwmechanics/actorutil.hpp>

namespace MWWorld
{
    FailedAction::FailedAction(std::string_view msg, const Ptr& target)
        : Action(false, target)
        , mMessage(msg)
    {
    }

    void FailedAction::executeImp(const Ptr& actor)
    {
        if (actor == MWMechanics::getPlayer() && !mMessage.empty())
            MWBase::Environment::get().getWindowManager()->messageBox(mMessage);
    }
}
