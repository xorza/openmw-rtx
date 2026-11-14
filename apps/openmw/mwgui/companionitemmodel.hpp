#pragma once

#include "inventoryitemmodel.hpp"

namespace MWGui
{

    /// @brief The companion item model keeps track of the companion's profit by
    /// monitoring which items are being added to and removed from the model.
    class CompanionItemModel : public InventoryItemModel
    {
    public:
        explicit CompanionItemModel(const MWWorld::Ptr& actor);

        bool hasProfit(const MWWorld::Ptr& actor);

    protected:
        MWWorld::Ptr addItem(const ItemStack& item, size_t count, bool allowAutoEquip = true) override;
        MWWorld::Ptr copyItem(const ItemStack& item, size_t count, bool allowAutoEquip = true) override;
        void removeItem(const ItemStack& item, size_t count) override;
    };

}
