#include "aibreathe.hpp"

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>

#include "movement.hpp"
#include "npcstats.hpp"
#include "steering.hpp"

bool MWMechanics::AiBreathe::execute(
    const MWWorld::Ptr& actor, CharacterController& characterController, AiState& state, float duration)
{
    static const float fHoldBreathTime
        = MWBase::Environment::get().getESMStore()->get<ESM::GameSetting>().find("fHoldBreathTime")->mValue.getFloat();

    const MWWorld::Class& actorClass = actor.getClass();
    if (actorClass.isNpc())
    {
        if (actorClass.getNpcStats(actor).getTimeToStartDrowning() < fHoldBreathTime / 2)
        {
            actorClass.getCreatureStats(actor).setMovementFlag(CreatureStats::Flag_Run, true);

            actorClass.getMovementSettings(actor).mPosition[1] = 1;
            smoothTurn(actor, static_cast<float>(-osg::PI_2), 0);

            return false;
        }
    }

    return true;
}
