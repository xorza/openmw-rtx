#include "actiontalk.hpp"

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/windowmanager.hpp>
#include <apps/openmw/mwmechanics/actorutil.hpp>

namespace MWWorld
{
    ActionTalk::ActionTalk(const Ptr& actor)
        : Action(false, actor)
    {
    }

    void ActionTalk::executeImp(const Ptr& actor)
    {
        if (actor == MWMechanics::getPlayer())
            MWBase::Environment::get().getWindowManager()->pushGuiMode(MWGui::GM_Dialogue, getTarget());
    }
}
