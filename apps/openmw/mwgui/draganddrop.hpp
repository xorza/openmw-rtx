#pragma once

#include <cstddef>

#include "itemmodel.hpp"
#include "itemwidget.hpp"

namespace MyGUI
{
    class Widget;
}

namespace MWGui
{

    class ItemView;
    class SortFilterItemModel;

    class DragAndDrop
    {
    public:
        bool mIsOnDragAndDrop;
        ItemWidget* mDraggedWidget;
        ItemModel* mSourceModel;
        ItemView* mSourceView;
        SortFilterItemModel* mSourceSortModel;
        ItemStack mItem;
        std::size_t mDraggedCount;

        DragAndDrop();

        void startDrag(int index, SortFilterItemModel* sortModel, ItemModel* sourceModel, ItemView* sourceView,
            std::size_t count, bool playSound = true);
        void drop(ItemModel* targetModel, ItemView* targetView, bool playSound = true);
        void update();
        void onItemConsumed();
        void onFrame();

        void finish();

    private:
        void updateDraggedCount(std::size_t desiredCount);
    };

}
