#include "actionalchemy.hpp"

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/windowmanager.hpp>
#include <apps/openmw/mwmechanics/actorutil.hpp>

namespace MWWorld
{
    ActionAlchemy::ActionAlchemy(bool force)
        : Action(false)
        , mForce(force)
    {
    }

    void ActionAlchemy::executeImp(const Ptr& actor)
    {
        if (actor != MWMechanics::getPlayer())
            return;

        if (!mForce && MWMechanics::isPlayerInCombat())
        { // Ensure we're not in combat
            MWBase::Environment::get().getWindowManager()->messageBox("#{sInventoryMessage3}");
            return;
        }

        MWBase::Environment::get().getWindowManager()->pushGuiMode(MWGui::GM_Alchemy);
    }
}
