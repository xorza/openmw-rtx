#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "leafnode.hpp"

namespace CSMWorld
{
    class IdTableBase;
}

namespace CSMFilter
{
    class BooleanNode : public LeafNode
    {
        bool mTrue;

    public:
        explicit BooleanNode(bool value)
            : mTrue(value)
        {
        }

        bool test(const CSMWorld::IdTableBase& table, std::int32_t row,
            const std::map<std::int32_t, std::int32_t>& columns) const override
        {
            return mTrue;
        }
        ///< \return Can the specified table row pass through to filter?
        /// \param columns column ID to column index mapping

        std::string toString(bool numericColumns) const override { return mTrue ? "true" : "false"; }
        ///< Return a string that represents this node.
        ///
        /// \param numericColumns Use numeric IDs instead of string to represent columns.
    };
}
