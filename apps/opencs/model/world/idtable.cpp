#include "idtable.hpp"

#include <cstdint>
#include <map>
#include <stdexcept>

#include <QModelIndex>
#include <QVariant>

#include <components/esm3/loadcell.hpp>

#include "collectionbase.hpp"
#include "columnbase.hpp"
#include "columns.hpp"
#include "idcollection.hpp"
#include "idtablebase.hpp"
#include "record.hpp"
#include "universalid.hpp"
#include "variantconversion.hpp"

CSMWorld::IdTable::IdTable(CollectionBase* idCollection, std::uint32_t features)
    : IdTableBase(features)
    , mIdCollection(idCollection)
{
}

std::int32_t CSMWorld::IdTable::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;

    return mIdCollection->getSize();
}

std::int32_t CSMWorld::IdTable::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;

    return mIdCollection->getColumns();
}

QVariant CSMWorld::IdTable::data(const QModelIndex& index, std::int32_t role) const
{
    if (index.row() < 0 || index.column() < 0)
        return QVariant();

    if (role == ColumnBase::Role_Display)
        return QVariant(mIdCollection->getColumn(index.column()).mDisplayType);

    if (role == ColumnBase::Role_ColumnId)
        return QVariant(getColumnId(index.column()));

    if ((role != Qt::DisplayRole && role != Qt::EditRole))
        return QVariant();

    if (role == Qt::EditRole && !mIdCollection->getColumn(index.column()).isEditable())
        return QVariant();

    return mIdCollection->getData(index.row(), index.column());
}

QVariant CSMWorld::IdTable::headerData(std::int32_t section, Qt::Orientation orientation, std::int32_t role) const
{
    if (orientation == Qt::Vertical)
        return QVariant();

    if (orientation != Qt::Horizontal)
        throw std::logic_error("Unknown header orientation specified");

    if (role == Qt::DisplayRole)
        return tr(mIdCollection->getColumn(section).getTitle().c_str());

    if (role == ColumnBase::Role_Flags)
        return mIdCollection->getColumn(section).mFlags;

    if (role == ColumnBase::Role_Display)
        return mIdCollection->getColumn(section).mDisplayType;

    if (role == ColumnBase::Role_ColumnId)
        return getColumnId(section);

    return QVariant();
}

bool CSMWorld::IdTable::setData(const QModelIndex& index, const QVariant& value, std::int32_t role)
{
    if (mIdCollection->getColumn(index.column()).isEditable() && role == Qt::EditRole)
    {
        mIdCollection->setData(index.row(), index.column(), value);

        std::int32_t stateColumn = searchColumnIndex(Columns::ColumnId_Modification);
        if (stateColumn != -1)
        {
            if (index.column() == stateColumn)
            {
                // modifying the state column can modify other values. we need to tell
                // views that the whole row has changed.

                emit dataChanged(
                    this->index(index.row(), 0), this->index(index.row(), columnCount(index.parent()) - 1));
            }
            else
            {
                emit dataChanged(index, index);

                // Modifying a value can also change the Modified status of a record.
                QModelIndex stateIndex = this->index(index.row(), stateColumn);
                emit dataChanged(stateIndex, stateIndex);
            }
        }
        else
            emit dataChanged(index, index);

        return true;
    }

    return false;
}

Qt::ItemFlags CSMWorld::IdTable::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::ItemFlags();

    Qt::ItemFlags flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled;

    if (mIdCollection->getColumn(index.column()).isUserEditable())
        flags |= Qt::ItemIsEditable;

    std::int32_t blockedColumn = searchColumnIndex(Columns::ColumnId_Blocked);
    if (blockedColumn != -1 && blockedColumn != index.column())
    {
        bool isBlocked = mIdCollection->getData(index.row(), blockedColumn).toInt();
        if (isBlocked)
            flags = Qt::ItemIsSelectable; // not enabled (to grey out)
    }

    return flags;
}

bool CSMWorld::IdTable::removeRows(std::int32_t row, std::int32_t count, const QModelIndex& parent)
{
    if (parent.isValid())
        return false;

    beginRemoveRows(parent, row, row + count - 1);

    mIdCollection->removeRows(row, count);

    endRemoveRows();

    return true;
}

QModelIndex CSMWorld::IdTable::index(std::int32_t row, std::int32_t column, const QModelIndex& parent) const
{
    if (parent.isValid())
        return QModelIndex();

    if (row < 0 || row >= mIdCollection->getSize())
        return QModelIndex();

    if (column < 0 || column >= mIdCollection->getColumns())
        return QModelIndex();

    return createIndex(row, column);
}

QModelIndex CSMWorld::IdTable::parent(const QModelIndex& index) const
{
    return QModelIndex();
}

void CSMWorld::IdTable::addRecord(const std::string& id, UniversalId::Type type)
{
    ESM::RefId refId = ESM::RefId::stringRefId(id);
    std::int32_t index = mIdCollection->getAppendIndex(refId, type);

    beginInsertRows(QModelIndex(), index, index);

    mIdCollection->appendBlankRecord(refId, type);

    endInsertRows();
}

void CSMWorld::IdTable::addRecordWithData(
    const std::string& id, const std::map<std::int32_t, QVariant>& data, UniversalId::Type type)
{
    ESM::RefId refId = ESM::RefId::stringRefId(id);
    std::int32_t index = mIdCollection->getAppendIndex(refId, type);

    beginInsertRows(QModelIndex(), index, index);

    mIdCollection->appendBlankRecord(refId, type);

<<<<<<< apps/opencs/model/world/idtable.cpp (ours)
    for (const auto& entry : data)
    {
        mIdCollection->setData(index, entry.first, entry.second);
=======
    for (const auto& [column, value] : data)
    {
        mIdCollection->setData(index, column, value);
>>>>>>> apps/opencs/model/world/idtable.cpp (theirs)
    }

    endInsertRows();
}

void CSMWorld::IdTable::cloneRecord(
    const ESM::RefId& origin, const ESM::RefId& destination, CSMWorld::UniversalId::Type type)
{
    std::int32_t index = mIdCollection->getAppendIndex(destination, type);

    beginInsertRows(QModelIndex(), index, index);
    mIdCollection->cloneRecord(origin, destination, type);
    endInsertRows();
}

bool CSMWorld::IdTable::touchRecord(const std::string& id)
{
    ESM::RefId refId = ESM::RefId::stringRefId(id);
    bool changed = mIdCollection->touchRecord(refId);

    std::int32_t row = mIdCollection->getIndex(refId);
    std::int32_t column = mIdCollection->searchColumnIndex(Columns::ColumnId_RecordType);
    if (changed && column != -1)
    {
        QModelIndex modelIndex = index(row, column);
        emit dataChanged(modelIndex, modelIndex);
    }

    return changed;
}

std::string CSMWorld::IdTable::getId(std::int32_t row) const
{
    return mIdCollection->getId(row).getRefIdString();
}

/// This method can return only indexes to the top level table cells
QModelIndex CSMWorld::IdTable::getModelIndex(const std::string& id, std::int32_t column) const
{
    const std::int32_t row = mIdCollection->searchId(ESM::RefId::stringRefId(id));

    if (row != -1)
        return index(row, column);

    return QModelIndex();
}

void CSMWorld::IdTable::setRecord(
    const std::string& id, std::unique_ptr<RecordBase> record, CSMWorld::UniversalId::Type type)
{
    const ESM::RefId refId = ESM::RefId::stringRefId(id);
    const std::int32_t index = mIdCollection->searchId(refId);

    if (index == -1)
    {
        // For info records, appendRecord may use a different index than the one returned by
        // getAppendIndex (because of prev/next links).  This can result in the display not
        // updating correctly after an undo
        //
        // Use an alternative method to get the correct index.  For non-Info records the
        // record pointer is ignored and internally calls getAppendIndex.
        const std::int32_t index2 = mIdCollection->getInsertIndex(refId, type, record.get());

        beginInsertRows(QModelIndex(), index2, index2);

        mIdCollection->appendRecord(std::move(record), type);

        endInsertRows();
    }
    else
    {
        mIdCollection->replace(index, std::move(record));
        emit dataChanged(
            CSMWorld::IdTable::index(index, 0), CSMWorld::IdTable::index(index, mIdCollection->getColumns() - 1));
    }
}

const CSMWorld::RecordBase& CSMWorld::IdTable::getRecord(const std::string& id) const
{
    return mIdCollection->getRecord(ESM::RefId::stringRefId(id));
}

std::int32_t CSMWorld::IdTable::searchColumnIndex(Columns::ColumnId id) const
{
    return mIdCollection->searchColumnIndex(id);
}

std::int32_t CSMWorld::IdTable::findColumnIndex(Columns::ColumnId id) const
{
    return mIdCollection->findColumnIndex(id);
}

void CSMWorld::IdTable::reorderRows(std::int32_t baseIndex, const std::vector<std::int32_t>& newOrder)
{
    if (newOrder.empty())
        return;
    if (!mIdCollection->reorderRows(baseIndex, newOrder))
        return;
    emit dataChanged(index(baseIndex, 0),
        index(baseIndex + static_cast<std::int32_t>(newOrder.size()) - 1, mIdCollection->getColumns() - 1));
}

std::pair<CSMWorld::UniversalId, std::string> CSMWorld::IdTable::view(std::int32_t row) const
{
    std::string id;
    std::string hint;

    if (getFeatures() & Feature_ViewCell)
    {
        std::int32_t cellColumn = mIdCollection->searchColumnIndex(Columns::ColumnId_Cell);
        std::int32_t idColumn = mIdCollection->searchColumnIndex(Columns::ColumnId_Id);

        if (cellColumn != -1 && idColumn != -1)
        {
            id = getString(mIdCollection->getData(row, cellColumn));
            hint = "r:" + getString(mIdCollection->getData(row, idColumn));
        }
    }
    else if (getFeatures() & Feature_ViewId)
    {
        std::int32_t column = mIdCollection->searchColumnIndex(Columns::ColumnId_Id);

        if (column != -1)
        {
            id = getString(mIdCollection->getData(row, column));
            hint = "c:" + id;
        }
    }

    if (id.empty())
        return std::make_pair(UniversalId::Type_None, "");

    if (id[0] == '#')
        id = ESM::Cell::sDefaultWorldspaceId.getValue();

    return std::make_pair(UniversalId(UniversalId::Type_Scene, id), hint);
}

/// For top level data/columns
bool CSMWorld::IdTable::isDeleted(const std::string& id) const
{
    return getRecord(id).isDeleted();
}

std::int32_t CSMWorld::IdTable::getColumnId(std::int32_t column) const
{
    return mIdCollection->getColumn(column).getId();
}

CSMWorld::CollectionBase* CSMWorld::IdTable::idCollection() const
{
    return mIdCollection;
}

CSMWorld::LandTextureIdTable::LandTextureIdTable(CollectionBase* idCollection, std::uint32_t features)
    : IdTable(idCollection, features)
{
}

const CSMWorld::Record<ESM::LandTexture>* CSMWorld::LandTextureIdTable::searchRecord(
    std::uint16_t index, std::int32_t plugin) const
{
    return static_cast<CSMWorld::IdCollection<ESM::LandTexture>*>(idCollection())->searchRecord(index, plugin);
}
