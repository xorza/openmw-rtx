#pragma once

#include <cstdint>
#include <string>

#include <QAbstractProxyModel>
#include <QModelIndex>
#include <QString>
#include <QVariant>

#include "columnbase.hpp"

/*! \brief
 * Proxy model used to connect view in the dialogue into the nested columns of the main model.
 */

namespace CSMWorld
{
    class IdTree;

    class NestedTableProxyModel : public QAbstractProxyModel
    {
        Q_OBJECT

        const std::int32_t mParentColumn;
        IdTree* mMainModel;
        std::string mId;

    public:
        NestedTableProxyModel(const QModelIndex& parent, ColumnBase::Display columnId, IdTree* parentModel);
        // parent is the parent of columns to work with. Columnid provides information about the column

        std::string getParentId() const;

        std::int32_t getParentColumn() const;

        CSMWorld::IdTree* model() const;

        QModelIndex mapFromSource(const QModelIndex& sourceIndex) const override;

        QModelIndex mapToSource(const QModelIndex& proxyIndex) const override;

        std::int32_t rowCount(const QModelIndex& index) const override;

        std::int32_t columnCount(const QModelIndex& parent) const override;

        QModelIndex index(
            std::int32_t row, std::int32_t column, const QModelIndex& parent = QModelIndex()) const override;

        QModelIndex parent(const QModelIndex& index) const override;

        QVariant headerData(std::int32_t section, Qt::Orientation orientation, std::int32_t role) const override;

        QVariant data(const QModelIndex& index, std::int32_t role = Qt::DisplayRole) const override;

        bool setData(const QModelIndex& index, const QVariant& value, std::int32_t role = Qt::EditRole) override;

        Qt::ItemFlags flags(const QModelIndex& index) const override;

    private:
        void setupHeaderVectors(ColumnBase::Display columnId);

        bool indexIsParent(const QModelIndex& index);

    private slots:
        void forwardRowsAboutToInserted(const QModelIndex& parent, std::int32_t first, std::int32_t last);

        void forwardRowsInserted(const QModelIndex& parent, std::int32_t first, std::int32_t last);

        void forwardRowsAboutToRemoved(const QModelIndex& parent, std::int32_t first, std::int32_t last);

        void forwardRowsRemoved(const QModelIndex& parent, std::int32_t first, std::int32_t last);

        void forwardResetStart(const QString& id);

        void forwardResetEnd(const QString& id);

        void forwardDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight);
    };
}
