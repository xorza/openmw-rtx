#include "actionopen.hpp"

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/mechanicsmanager.hpp>
#include <apps/openmw/mwbase/windowmanager.hpp>
#include <apps/openmw/mwmechanics/actorutil.hpp>
#include <apps/openmw/mwmechanics/disease.hpp>

namespace MWWorld
{
    ActionOpen::ActionOpen(const MWWorld::Ptr& container)
        : Action(false, container)
    {
    }

    void ActionOpen::executeImp(const MWWorld::Ptr& actor)
    {
        if (!MWBase::Environment::get().getWindowManager()->isAllowed(MWGui::GW_Inventory))
            return;

        if (actor != MWMechanics::getPlayer())
            return;

        if (!MWBase::Environment::get().getMechanicsManager()->onOpen(getTarget()))
            return;

        MWMechanics::diseaseContact(actor, getTarget());

        MWBase::Environment::get().getWindowManager()->pushGuiMode(MWGui::GM_Container, getTarget());
    }
}
