#pragma once

#include <map>
#include <memory>
#include <vector>

#include "narynode.hpp"

namespace CSMWorld
{
    class IdTableBase;
}

namespace CSMFilter
{
    class Node;
    class OrNode : public NAryNode
    {
    public:
        explicit OrNode(const std::vector<std::shared_ptr<Node>>& nodes);

        bool test(const CSMWorld::IdTableBase& table, int row, const std::map<int, int>& columns) const override;
        ///< \return Can the specified table row pass through to filter?
        /// \param columns column ID to column index mapping
    };
}
