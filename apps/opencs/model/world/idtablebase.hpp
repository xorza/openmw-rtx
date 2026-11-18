#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include <QAbstractItemModel>
#include <QModelIndex>

#include "columns.hpp"

namespace CSMWorld
{
    class UniversalId;

    class IdTableBase : public QAbstractItemModel
    {
        Q_OBJECT

    public:
        enum Features
        {
            Feature_ReorderWithinTopic = 1,

            /// Use ID column to generate view request (ID is transformed into
            /// worldspace and original ID is passed as hint with c: prefix).
            Feature_ViewId = 2,

            /// Use cell column to generate view request (cell ID is transformed
            /// into worldspace and record ID is passed as hint with r: prefix).
            Feature_ViewCell = 4,

            Feature_View = Feature_ViewId | Feature_ViewCell,

            Feature_Preview = 8,

            /// Table can not be modified through ordinary means.
            Feature_Constant = 16,

            Feature_AllowTouch = 32
        };

    private:
        std::uint32_t mFeatures;

    public:
        explicit IdTableBase(std::uint32_t features);

        virtual QModelIndex getModelIndex(const std::string& id, std::int32_t column) const = 0;

        /// Return index of column with the given \a id. If no such column exists, -1 is
        /// returned.
        virtual std::int32_t searchColumnIndex(Columns::ColumnId id) const = 0;

        /// Return index of column with the given \a id. If no such column exists, an
        /// exception is thrown.
        virtual std::int32_t findColumnIndex(Columns::ColumnId id) const = 0;

        /// Return the UniversalId and the hint for viewing \a row. If viewing is not
        /// supported by this table, return (UniversalId::Type_None, "").
        virtual std::pair<UniversalId, std::string> view(std::int32_t row) const = 0;

        /// Is \a id flagged as deleted?
        virtual bool isDeleted(const std::string& id) const = 0;

        virtual std::int32_t getColumnId(std::int32_t column) const = 0;

        std::uint32_t getFeatures() const;
    };
}
