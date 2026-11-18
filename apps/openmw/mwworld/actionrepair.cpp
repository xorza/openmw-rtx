#include "actionrepair.hpp"

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/windowmanager.hpp>
#include <apps/openmw/mwmechanics/actorutil.hpp>

namespace MWWorld
{
    ActionRepair::ActionRepair(const Ptr& item, bool force)
        : Action(false, item)
        , mForce(force)
    {
    }

    void ActionRepair::executeImp(const Ptr& actor)
    {
        if (actor != MWMechanics::getPlayer())
            return;

        if (!mForce && MWMechanics::isPlayerInCombat())
        {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sInventoryMessage2}");
            return;
        }

        MWBase::Environment::get().getWindowManager()->pushGuiMode(MWGui::GM_Repair, getTarget());
    }
}
