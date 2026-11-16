#include "actionharvest.hpp"

#include <sstream>

#include <MyGUI_LanguageManager.h>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/mechanicsmanager.hpp>
#include <apps/openmw/mwbase/windowmanager.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwrender/animation.hpp>
#include <components/misc/strings/format.hpp>

#include "class.hpp"
#include "containerstore.hpp"

namespace MWWorld
{
    ActionHarvest::ActionHarvest(const MWWorld::Ptr& container)
        : Action(true, container)
    {
        setSound(ESM::RefId::stringRefId("Item Ingredient Up"));
    }

    void ActionHarvest::executeImp(const MWWorld::Ptr& actor)
    {
        if (!MWBase::Environment::get().getWindowManager()->isAllowed(MWGui::GW_Inventory))
            return;

        MWWorld::Ptr target = getTarget();
        MWWorld::ContainerStore& store = target.getClass().getContainerStore(target);
        store.resolve();
        MWWorld::ContainerStore& actorStore = actor.getClass().getContainerStore(actor);
        std::map<std::string, int> takenMap;
        for (MWWorld::ContainerStoreIterator it = store.begin(); it != store.end(); ++it)
        {
            if (!it->getClass().showsInInventory(*it))
                continue;

            int itemCount = it->getCellRef().getCount();
            // Note: it is important to check for crime before move an item from container. Otherwise owner check will
            // not work for a last item in the container - empty harvested containers are considered as "allowed to
            // use".
            MWBase::Environment::get().getMechanicsManager()->itemTaken(actor, *it, target, itemCount);
            actorStore.add(*it, itemCount);
            store.remove(*it, itemCount);
            std::string name{ it->getClass().getName(*it) };
            takenMap[name] += itemCount;
        }

        // Spawn a messagebox (only for items added to player's inventory)
        if (actor == MWBase::Environment::get().getWorld()->getPlayerPtr())
        {
            std::ostringstream stream;
            int lineCount = 0;
            const static int maxLines = 10;
            for (const auto& pair : takenMap)
            {
                const std::string& itemName = pair.first;
                int itemCount = pair.second;
                lineCount++;
                if (lineCount == maxLines)
                    stream << "\n...";
                else if (lineCount > maxLines)
                    break;

                // The two GMST entries below expand to strings informing the player of what, and how many of it has
                // been added to their inventory
                std::string msgBox;
                if (itemCount == 1)
                {
                    msgBox = MyGUI::LanguageManager::getInstance().replaceTags("\n#{sNotifyMessage60}");
                    msgBox = Misc::StringUtils::format(msgBox, itemName);
                }
                else
                {
                    msgBox = MyGUI::LanguageManager::getInstance().replaceTags("\n#{sNotifyMessage61}");
                    msgBox = Misc::StringUtils::format(msgBox, itemCount, itemName);
                }

                stream << msgBox;
            }
            std::string tooltip = stream.str();
            // remove the first newline (easier this way)
            if (!tooltip.empty() && tooltip[0] == '\n')
                tooltip.erase(0, 1);

            if (!tooltip.empty())
                MWBase::Environment::get().getWindowManager()->messageBox(tooltip);
        }

        auto world = MWBase::Environment::get().getWorld();
        MWRender::Animation* anim = world->getAnimation(target);
        if (anim != nullptr)
            anim->harvest(target);
    }
}
