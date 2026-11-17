#pragma once

#include <cstdint>

#include <QHash>

#include "columns.hpp"
#include "idtableproxymodel.hpp"
#include "universalid.hpp"

class QAbstractItemModel;
class QModelIndex;
class QObject;

namespace CSMWorld
{
    class InfoTableProxyModel : public IdTableProxyModel
    {
        Q_OBJECT

        UniversalId::Type mType;
        Columns::ColumnId mInfoColumnId;
        ///< Contains ID for Topic or Journal ID
        std::int32_t mInfoColumnIndex;
        std::int32_t mLastAddedSourceRow;

        mutable QHash<QString, std::int32_t> mFirstRowCache;

        std::int32_t getFirstInfoRow(std::int32_t currentRow) const;
        ///< Finds the first row with the same topic (journal entry) as in \a currentRow
        ///< \a currentRow is a row of the source model.

    public:
        explicit InfoTableProxyModel(UniversalId::Type type, QObject* parent = nullptr);

        void setSourceModel(QAbstractItemModel* sourceModel) override;

    protected:
        bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

    protected slots:
        void sourceRowsInserted(const QModelIndex& parent, std::int32_t start, std::int32_t end) override;
        void sourceRowsRemoved(const QModelIndex& parent, std::int32_t start, std::int32_t end) override;
        void sourceDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight) override;
    };
}
