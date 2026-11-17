#pragma once

#include <cstddef>
#include <cstdint>

#include "itemmodel.hpp"

namespace MWGui
{

    class InventoryItemModel : public ItemModel
    {
    public:
        explicit InventoryItemModel(const MWWorld::Ptr& actor);

        ItemStack getItem(ModelIndex index) override;
        ModelIndex getIndex(const ItemStack& item) override;
        std::size_t getItemCount() override;

        bool onTakeItem(const MWWorld::Ptr& item, std::int32_t count) override;

        /// Move items from this model to \a otherModel.
        MWWorld::Ptr moveItem(
            const ItemStack& item, std::size_t count, ItemModel* otherModel, bool allowAutoEquip = true) override;

        void update() override;

        bool usesContainer(const MWWorld::Ptr& container) override;

    protected:
        MWWorld::Ptr addItem(const ItemStack& item, std::size_t count, bool allowAutoEquip = true) override;
        MWWorld::Ptr copyItem(const ItemStack& item, std::size_t count, bool allowAutoEquip = true) override;
        void removeItem(const ItemStack& item, std::size_t count) override;

        MWWorld::Ptr mActor;

    private:
        std::vector<ItemStack> mItems;
    };

}
