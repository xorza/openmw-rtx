#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <apps/openmw/mwworld/containerstore.hpp>

#include "itemmodel.hpp"

namespace MWGui
{

    /// @brief The container item model supports multiple item sources, which are needed for
    /// making NPCs sell items from containers owned by them
    class ContainerItemModel : public ItemModel
    {
    public:
        ContainerItemModel(const std::vector<MWWorld::Ptr>& itemSources, const std::vector<MWWorld::Ptr>& worldItems);
        ///< @note The order of elements \a itemSources matters here. The first element has the highest priority for
        ///< removal,
        ///  while the last element will be used to add new items to.

        explicit ContainerItemModel(const MWWorld::Ptr& source);

        bool allowedToUseItems() const override;

        bool onDropItem(const MWWorld::Ptr& item, std::int32_t count) override;
        bool onTakeItem(const MWWorld::Ptr& item, std::int32_t count) override;

        ItemStack getItem(ModelIndex index) override;
        ModelIndex getIndex(const ItemStack& item) override;
        std::size_t getItemCount() override;

        void update() override;

        bool usesContainer(const MWWorld::Ptr& container) override;

    protected:
        MWWorld::Ptr addItem(const ItemStack& item, std::size_t count, bool allowAutoEquip = true) override;
        MWWorld::Ptr copyItem(const ItemStack& item, std::size_t count, bool allowAutoEquip = true) override;
        void removeItem(const ItemStack& item, std::size_t count) override;

    private:
        std::vector<std::pair<MWWorld::Ptr, MWWorld::ResolutionHandle>> mItemSources;
        std::vector<MWWorld::Ptr> mWorldItems;
        const bool mTrading;
        std::vector<ItemStack> mItems;
    };

}
