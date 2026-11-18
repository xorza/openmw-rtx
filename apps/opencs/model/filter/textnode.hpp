#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <QRegularExpression>

#include <apps/opencs/model/world/idtablebase.hpp>

#include "leafnode.hpp"

namespace CSMFilter
{
    class TextNode : public LeafNode
    {
        std::int32_t mColumnId;
        std::string mText;
        QRegularExpression mRegExp;

    public:
        TextNode(std::int32_t columnId, std::string text);

        bool test(const CSMWorld::IdTableBase& table, std::int32_t row,
            const std::map<std::int32_t, std::int32_t>& columns) const override;
        ///< \return Can the specified table row pass through to filter?
        /// \param columns column ID to column index mapping

        std::vector<std::int32_t> getReferencedColumns() const override;
        ///< Return a list of the IDs of the columns referenced by this node. The column mapping
        /// passed into test as columns must contain all columns listed here.

        std::string toString(bool numericColumns) const override;
        ///< Return a string that represents this node.
        ///
        /// \param numericColumns Use numeric IDs instead of string to represent columns.

        bool isValid() { return mRegExp.isValid(); }
    };
}
