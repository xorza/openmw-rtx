#pragma once

#include <cstddef>
#include <cstdint>

#include "itemmodel.hpp"

namespace MWGui
{

    /// @brief The pickpocket item model randomly hides item stacks based on a specified chance. Equipped items are
    /// always hidden.
    class PickpocketItemModel : public ProxyItemModel
    {
    public:
        PickpocketItemModel(const MWWorld::Ptr& actor, std::unique_ptr<ItemModel> sourceModel, bool hideItems = true);

        bool allowedToUseItems() const override;
        ItemStack getItem(ModelIndex index) override;
        std::size_t getItemCount() override;
        void update() override;
        void onClose() override;
        bool onDropItem(const MWWorld::Ptr& item, std::int32_t count) override;
        bool onTakeItem(const MWWorld::Ptr& item, std::int32_t count) override;

    protected:
        MWWorld::Ptr mActor;
        bool mPickpocketDetected;
        bool stealItem(const MWWorld::Ptr& item, std::int32_t count);

    private:
        std::vector<ItemStack> mHiddenItems;
        std::vector<ItemStack> mItems;
    };

}
