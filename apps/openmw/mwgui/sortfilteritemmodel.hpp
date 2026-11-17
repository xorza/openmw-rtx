#pragma once

#include <cstddef>
#include <cstdint>

#include "itemmodel.hpp"

namespace MWGui
{

    class SortFilterItemModel : public ProxyItemModel
    {
    public:
        explicit SortFilterItemModel(std::unique_ptr<ItemModel> sourceModel);

        void update() override;

        bool filterAccepts(const ItemStack& item);

        bool allowedToUseItems() const override;
        ItemStack getItem(ModelIndex index) override;
        std::size_t getItemCount() override;

        /// Dragged items are not displayed.
        void addDragItem(const MWWorld::Ptr& dragItem, std::size_t count);
        void clearDragItems();

        void setCategory(std::int32_t category);
        void setFilter(std::int32_t filter);
        void setNameFilter(const std::string& filter);
        void setEffectFilter(const std::string& filter);
        void setApparatusTypeFilter(const std::int32_t type);

        /// Use ItemStack::Type for sorting?
        void setSortByType(bool sort) { mSortByType = sort; }

        void onClose() override;
        bool onDropItem(const MWWorld::Ptr& item, std::int32_t count) override;
        bool onTakeItem(const MWWorld::Ptr& item, std::int32_t count) override;

        static constexpr std::int32_t Category_Weapon = (1 << 1);
        static constexpr std::int32_t Category_Apparel = (1 << 2);
        static constexpr std::int32_t Category_Misc = (1 << 3);
        static constexpr std::int32_t Category_Magic = (1 << 4);
        static constexpr std::int32_t Category_All = 255;

        static constexpr std::int32_t Filter_OnlyIngredients = (1 << 0);
        static constexpr std::int32_t Filter_OnlyEnchanted = (1 << 1);
        static constexpr std::int32_t Filter_OnlyEnchantable = (1 << 2);
        static constexpr std::int32_t Filter_OnlyChargedSoulstones = (1 << 3);
        static constexpr std::int32_t Filter_OnlyUsableItems = (1 << 4); // Only items with a Use action
        static constexpr std::int32_t Filter_OnlyRepairable = (1 << 5);
        static constexpr std::int32_t Filter_OnlyRechargable = (1 << 6);
        static constexpr std::int32_t Filter_OnlyRepairTools = (1 << 7);
        static constexpr std::int32_t Filter_OnlyAlchemyTools = (1 << 8);

    private:
        std::vector<ItemStack> mItems;

        std::vector<std::pair<MWWorld::Ptr, std::size_t>> mDragItems;

        std::int32_t mCategory;
        std::int32_t mFilter;
        bool mSortByType;

        std::int32_t mApparatusTypeFilter; // filter by apparatus type
        std::string mNameFilter; // filter by item name
        std::string mEffectFilter; // filter by magic effect
    };

}
