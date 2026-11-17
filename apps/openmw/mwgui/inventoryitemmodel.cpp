#include "inventoryitemmodel.hpp"

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <utility>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/mechanicsmanager.hpp>
#include <apps/openmw/mwmechanics/actorutil.hpp>
#include <apps/openmw/mwmechanics/creaturestats.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/containerstore.hpp>
#include <apps/openmw/mwworld/inventorystore.hpp>
#include <apps/openmw/mwworld/manualref.hpp>

namespace MWGui
{

    InventoryItemModel::InventoryItemModel(const MWWorld::Ptr& actor)
        : mActor(actor)
    {
    }

    ItemStack InventoryItemModel::getItem(ModelIndex index)
    {
        if (index < 0)
            throw std::runtime_error("Invalid index supplied");
        if (mItems.size() <= static_cast<std::size_t>(index))
            throw std::runtime_error("Item index out of range");
        return mItems[static_cast<std::size_t>(index)];
    }

    std::size_t InventoryItemModel::getItemCount()
    {
        return mItems.size();
    }

    ItemModel::ModelIndex InventoryItemModel::getIndex(const ItemStack& item)
    {
        ModelIndex i = 0;
        for (ItemStack& itemStack : mItems)
        {
            if (itemStack == item)
                return i;
            ++i;
        }
        return -1;
    }

    MWWorld::Ptr InventoryItemModel::addItem(const ItemStack& item, std::size_t count, bool allowAutoEquip)
    {
        if (item.mBase.getContainerStore() == &mActor.getClass().getContainerStore(mActor))
            throw std::runtime_error("Item to add needs to be from a different container!");
        return *mActor.getClass().getContainerStore(mActor).add(
            item.mBase, static_cast<std::int32_t>(count), allowAutoEquip);
    }

    MWWorld::Ptr InventoryItemModel::copyItem(const ItemStack& item, std::size_t count, bool allowAutoEquip)
    {
        if (item.mBase.getContainerStore() == &mActor.getClass().getContainerStore(mActor))
            throw std::runtime_error("Item to copy needs to be from a different container!");

        MWWorld::ManualRef newRef(
            *MWBase::Environment::get().getESMStore(), item.mBase, static_cast<std::int32_t>(count));
        return *mActor.getClass().getContainerStore(mActor).add(
            newRef.getPtr(), static_cast<std::int32_t>(count), allowAutoEquip);
    }

    void InventoryItemModel::removeItem(const ItemStack& item, std::size_t count)
    {
        std::int32_t removed = 0;
        // Re-equipping makes sense only if a target has inventory
        if (mActor.getClass().hasInventoryStore(mActor))
        {
            MWWorld::InventoryStore& store = mActor.getClass().getInventoryStore(mActor);
            removed = store.remove(item.mBase, static_cast<std::int32_t>(count), true);
        }
        else
        {
            MWWorld::ContainerStore& store = mActor.getClass().getContainerStore(mActor);
            removed = store.remove(item.mBase, static_cast<std::int32_t>(count));
        }

        std::stringstream error;

        if (removed == 0)
        {
            error << "Item '" << item.mBase.getCellRef().getRefId() << "' was not found in container store to remove";
            throw std::runtime_error(error.str());
        }
        else if (std::cmp_less(removed, count))
        {
            error << "Not enough items '" << item.mBase.getCellRef().getRefId() << "' in the stack to remove ("
                  << static_cast<std::int32_t>(count) << " requested, " << removed << " found)";
            throw std::runtime_error(error.str());
        }
    }

    MWWorld::Ptr InventoryItemModel::moveItem(
        const ItemStack& item, std::size_t count, ItemModel* otherModel, bool allowAutoEquip)
    {
        // Can't move conjured items: This is a general fix that also takes care of issues with taking conjured items
        // via the 'Take All' button.
        if (item.mFlags & ItemStack::Flag_Bound)
            return MWWorld::Ptr();

        return ItemModel::moveItem(item, count, otherModel, allowAutoEquip);
    }

    void InventoryItemModel::update()
    {
        MWWorld::ContainerStore& store = mActor.getClass().getContainerStore(mActor);

        mItems.clear();

        for (MWWorld::ContainerStoreIterator it = store.begin(); it != store.end(); ++it)
        {
            MWWorld::Ptr item = *it;

            if (!item.getClass().showsInInventory(item))
                continue;

            ItemStack newItem(item, this, static_cast<std::size_t>(item.getCellRef().getCount()));

            if (mActor.getClass().hasInventoryStore(mActor))
            {
                MWWorld::InventoryStore& invStore = mActor.getClass().getInventoryStore(mActor);
                if (invStore.isEquipped(newItem.mBase))
                    newItem.mType = ItemStack::Type_Equipped;
            }

            mItems.push_back(newItem);
        }
    }

    bool InventoryItemModel::onTakeItem(const MWWorld::Ptr& item, std::int32_t count)
    {
        // Looting a dead corpse is considered OK
        if (mActor.getClass().isActor() && mActor.getClass().getCreatureStats(mActor).isDead())
            return true;

        MWWorld::Ptr player = MWMechanics::getPlayer();
        MWBase::Environment::get().getMechanicsManager()->itemTaken(player, item, mActor, count);

        return true;
    }

    bool InventoryItemModel::usesContainer(const MWWorld::Ptr& container)
    {
        return mActor == container;
    }

}
