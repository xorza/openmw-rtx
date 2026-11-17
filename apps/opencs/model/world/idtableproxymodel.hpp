#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <QModelIndex>
#include <QSortFilterProxyModel>
#include <QString>
#include <QTimer>

#include <apps/opencs/model/prefs/state.hpp>

#include "columns.hpp"

class QObject;

namespace CSMFilter
{
    class Node;
}

namespace CSMWorld
{
    class IdTableBase;

    class IdTableProxyModel : public QSortFilterProxyModel
    {
        Q_OBJECT

        std::shared_ptr<CSMFilter::Node> mFilter;
        QTimer mFilterTimer;
        std::shared_ptr<CSMFilter::Node> mAwaitingFilter;
        std::map<std::int32_t, std::int32_t> mColumnMap; // column ID, column index in this model (or -1)

        // Cache of enum values for enum columns (e.g. Modified, Record Type).
        // Used to speed up comparisons during the sort by such columns.
        typedef std::map<Columns::ColumnId, std::vector<std::pair<std::int32_t, std::string>>> EnumColumnCache;
        mutable EnumColumnCache mEnumColumnCache;

    protected:
        IdTableBase* mSourceModel;

    private:
        void updateColumnMap();

    public:
        explicit IdTableProxyModel(QObject* parent = nullptr);

        virtual QModelIndex getModelIndex(const std::string& id, std::int32_t column) const;

        void setSourceModel(QAbstractItemModel* model) override;

        void setFilter(const std::shared_ptr<CSMFilter::Node>& filter);

    protected:
        bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

        bool filterAcceptsRow(std::int32_t sourceRow, const QModelIndex& sourceParent) const override;

        QString getRecordId(std::int32_t sourceRow) const;

    protected slots:

        virtual void sourceRowsInserted(const QModelIndex& parent, std::int32_t start, std::int32_t end);

        virtual void sourceRowsRemoved(const QModelIndex& parent, std::int32_t start, std::int32_t end);

        virtual void sourceDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight);

        void timerTimeout();

        void settingChanged(const CSMPrefs::Setting* setting);

    signals:

        void rowAdded(const std::string& id);
    };
}
