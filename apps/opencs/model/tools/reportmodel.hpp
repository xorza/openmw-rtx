#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <QAbstractTableModel>
#include <QModelIndex>
#include <QVariant>

#include <apps/opencs/model/doc/messages.hpp>

namespace CSMWorld
{
    class UniversalId;
}

namespace CSMTools
{
    class ReportModel : public QAbstractTableModel
    {
        Q_OBJECT

        std::vector<CSMDoc::Message> mRows;

        // Fixed columns
        enum Columns
        {
            Column_Type = 0,
            Column_Id = 1,
            Column_Hint = 2
        };

        // Configurable columns
        std::int32_t mColumnDescription;
        std::int32_t mColumnField;
        std::int32_t mColumnSeverity;

    public:
        explicit ReportModel(bool fieldColumn = false, bool severityColumn = true);

        std::int32_t rowCount(const QModelIndex& parent = QModelIndex()) const override;

        std::int32_t columnCount(const QModelIndex& parent = QModelIndex()) const override;

        QVariant data(const QModelIndex& index, std::int32_t role = Qt::DisplayRole) const override;

        QVariant headerData(
            std::int32_t section, Qt::Orientation orientation, std::int32_t role = Qt::DisplayRole) const override;

        bool removeRows(std::int32_t row, std::int32_t count, const QModelIndex& parent = QModelIndex()) override;

        void add(const CSMDoc::Message& message);

        void flagAsReplaced(std::int32_t index);

        const CSMWorld::UniversalId& getUniversalId(std::int32_t row) const;

        std::string getHint(std::int32_t row) const;

        void clear();

        // Return number of messages with Error or SeriousError severity.
        std::int32_t countErrors() const;
    };
}
