#include "actorutil.hpp"

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/player.hpp>
#include <components/esm3/loadmgef.hpp>

#include "creaturestats.hpp"
#include "magiceffects.hpp"

namespace MWMechanics
{
    MWWorld::Ptr getPlayer()
    {
        return MWBase::Environment::get().getWorld()->getPlayerPtr();
    }

    bool isPlayerInCombat()
    {
        return MWBase::Environment::get().getWorld()->getPlayer().isInCombat();
    }

    bool canActorMoveByZAxis(const MWWorld::Ptr& actor)
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        return (actor.getClass().canSwim(actor) && world->isSwimming(actor)) || world->isFlying(actor);
    }

    bool hasWaterWalking(const MWWorld::Ptr& actor)
    {
        const MWMechanics::MagicEffects& effects = actor.getClass().getCreatureStats(actor).getMagicEffects();
        return effects.getOrDefault(ESM::MagicEffect::WaterWalking).getMagnitude() > 0;
    }

    bool isTargetMagicallyHidden(const MWWorld::Ptr& actor)
    {
        const MagicEffects& magicEffects = actor.getClass().getCreatureStats(actor).getMagicEffects();
        return (magicEffects.getOrDefault(ESM::MagicEffect::Invisibility).getMagnitude() > 0)
            || (magicEffects.getOrDefault(ESM::MagicEffect::Chameleon).getMagnitude() >= 75);
    }
}
