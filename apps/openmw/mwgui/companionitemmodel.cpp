#include "companionitemmodel.hpp"

#include <cstddef>
#include <cstdint>

#include <apps/openmw/mwworld/class.hpp>

namespace
{

    void modifyProfit(const MWWorld::Ptr& actor, std::int32_t diff)
    {
        const ESM::RefId& script = actor.getClass().getScript(actor);
        if (!script.empty())
        {
            std::int32_t profit = actor.getRefData().getLocals().getIntVar(script, "minimumprofit");
            profit += diff;
            actor.getRefData().getLocals().setVarByInt(script, "minimumprofit", profit);
        }
    }

}

namespace MWGui
{
    CompanionItemModel::CompanionItemModel(const MWWorld::Ptr& actor)
        : InventoryItemModel(actor)
    {
    }

    MWWorld::Ptr CompanionItemModel::addItem(const ItemStack& item, std::size_t count, bool allowAutoEquip)
    {
        if (hasProfit(mActor))
            modifyProfit(mActor,
                static_cast<std::int32_t>(
                    static_cast<std::size_t>(item.mBase.getClass().getValue(item.mBase)) * count));

        return InventoryItemModel::addItem(item, count, allowAutoEquip);
    }

    MWWorld::Ptr CompanionItemModel::copyItem(const ItemStack& item, std::size_t count, bool allowAutoEquip)
    {
        if (hasProfit(mActor))
            modifyProfit(mActor,
                static_cast<std::int32_t>(
                    static_cast<std::size_t>(item.mBase.getClass().getValue(item.mBase)) * count));

        return InventoryItemModel::copyItem(item, count, allowAutoEquip);
    }

    void CompanionItemModel::removeItem(const ItemStack& item, std::size_t count)
    {
        if (hasProfit(mActor))
            modifyProfit(mActor,
                -static_cast<std::int32_t>(
                    static_cast<std::size_t>(item.mBase.getClass().getValue(item.mBase)) * count));

        InventoryItemModel::removeItem(item, count);
    }

    bool CompanionItemModel::hasProfit(const MWWorld::Ptr& actor)
    {
        const ESM::RefId& script = actor.getClass().getScript(actor);
        if (script.empty())
            return false;
        return actor.getRefData().getLocals().hasVar(script, "minimumprofit");
    }
}
