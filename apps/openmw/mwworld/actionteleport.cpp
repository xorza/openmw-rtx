#include "actionteleport.hpp"

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/luamanager.hpp>
#include <apps/openmw/mwbase/mechanicsmanager.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwmechanics/actorutil.hpp>
#include <apps/openmw/mwmechanics/creaturestats.hpp>
#include <components/esm3/loadmgef.hpp>

#include "cellstore.hpp"
#include "class.hpp"
#include "player.hpp"
#include "worldmodel.hpp"

namespace MWWorld
{
    ActionTeleport::ActionTeleport(ESM::RefId cellId, const ESM::Position& position, bool teleportFollowers)
        : Action(true)
        , mCellId(cellId)
        , mPosition(position)
        , mTeleportFollowers(teleportFollowers)
    {
    }

    void ActionTeleport::executeImp(const Ptr& actor)
    {
        if (mTeleportFollowers)
        {
            // Find any NPCs that are following the actor and teleport them with him
            std::set<MWWorld::Ptr> followers;

            bool toExterior = MWBase::Environment::get().getWorldModel()->getCell(mCellId).isExterior();
            getFollowers(actor, followers, toExterior, true);

            for (const auto& follower : followers)
                teleport(follower);
        }

        teleport(actor);
    }

    void ActionTeleport::teleport(const Ptr& actor)
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::WorldModel* worldModel = MWBase::Environment::get().getWorldModel();
        auto& stats = actor.getClass().getCreatureStats(actor);
        stats.land(actor == world->getPlayerPtr());
        stats.setTeleported(true);

        Ptr teleported;
        if (actor == world->getPlayerPtr())
        {
            world->getPlayer().setTeleported(true);
            world->changeToCell(mCellId, mPosition, true);
            teleported = world->getPlayerPtr();
        }
        else
        {
            if (actor.getClass().getCreatureStats(actor).getAiSequence().isInCombat(world->getPlayerPtr()))
            {
                actor.getClass().getCreatureStats(actor).getAiSequence().stopCombat();
                return;
            }
            else
                teleported = world->moveObject(actor, &worldModel->getCell(mCellId), mPosition.asVec3(), true, true);
        }

        if (!world->isWaterWalkingCastableOnTarget(teleported) && MWMechanics::hasWaterWalking(teleported))
            teleported.getClass()
                .getCreatureStats(teleported)
                .getActiveSpells()
                .purgeEffect(actor, ESM::MagicEffect::WaterWalking);

        MWBase::Environment::get().getLuaManager()->objectTeleported(teleported);
    }

    void ActionTeleport::getFollowers(
        const MWWorld::Ptr& actor, std::set<MWWorld::Ptr>& out, bool toExterior, bool includeHostiles)
    {
        std::set<MWWorld::Ptr> followers;
        MWBase::Environment::get().getMechanicsManager()->getActorsFollowing(actor, followers);

        for (auto follower : followers)
        {
            const ESM::RefId& script = follower.getClass().getScript(follower);

            if (!includeHostiles && follower.getClass().getCreatureStats(follower).getAiSequence().isInCombat(actor))
                continue;

            if (!toExterior && !script.empty()
                && follower.getRefData().getLocals().getIntVar(script, "stayoutside") == 1
                && follower.getCell()->getCell()->isExterior())
                continue;

            if ((follower.getRefData().getPosition().asVec3() - actor.getRefData().getPosition().asVec3()).length2()
                > 800 * 800)
                continue;

            out.emplace(follower);
        }
    }
}
