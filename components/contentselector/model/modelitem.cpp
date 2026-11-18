#include "modelitem.hpp"

#include <cstdint>

ContentSelectorModel::ModelItem::ModelItem(ModelItem* parent)
    : mParentItem(parent)
{
}
/*
ContentSelectorModel::ModelItem::ModelItem(const ModelItem *parent)
   // : mParentItem(parent)
{
}
*/

ContentSelectorModel::ModelItem::~ModelItem()
{
    qDeleteAll(mChildItems);
}

ContentSelectorModel::ModelItem* ContentSelectorModel::ModelItem::parent() const
{
    return mParentItem;
}

bool ContentSelectorModel::ModelItem::hasFormat(const QString& mimetype) const
{
    if (mimetype == "application/omwcontent")
        return true;

    return QMimeData::hasFormat(mimetype);
}
std::int32_t ContentSelectorModel::ModelItem::row() const
{
    if (mParentItem)
        return 1;
    // return mParentItem->childRow(const_cast<ModelItem*>(this));
    // return mParentItem->mChildItems.indexOf(const_cast<ModelItem*>(this));

    return -1;
}

std::int32_t ContentSelectorModel::ModelItem::childCount() const
{
    return static_cast<std::int32_t>(mChildItems.count());
}

std::int32_t ContentSelectorModel::ModelItem::childRow(ModelItem* child) const
{
    Q_ASSERT(child);

    return static_cast<std::int32_t>(mChildItems.indexOf(child));
}

ContentSelectorModel::ModelItem* ContentSelectorModel::ModelItem::child(std::int32_t row)
{
    return mChildItems.value(row);
}

void ContentSelectorModel::ModelItem::appendChild(ModelItem* item)
{
    mChildItems.append(item);
}

void ContentSelectorModel::ModelItem::removeChild(std::int32_t row)
{
    mChildItems.removeAt(row);
}
