#pragma once

#include <cstdint>
#include <map>
#include <memory>

#include "unarynode.hpp"

namespace CSMWorld
{
    class IdTableBase;
}

namespace CSMFilter
{
    class Node;
    class NotNode : public UnaryNode
    {
    public:
        explicit NotNode(std::shared_ptr<Node> child);

        bool test(const CSMWorld::IdTableBase& table, std::int32_t row,
            const std::map<std::int32_t, std::int32_t>& columns) const override;
        ///< \return Can the specified table row pass through to filter?
        /// \param columns column ID to column index mapping
    };
}
