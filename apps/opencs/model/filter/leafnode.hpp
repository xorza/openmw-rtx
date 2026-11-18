#pragma once

#include <cstdint>
#include <vector>

#include "node.hpp"

namespace CSMFilter
{
    class LeafNode : public Node
    {
    public:
        std::vector<std::int32_t> getReferencedColumns() const override { return {}; }
        ///< Return a list of the IDs of the columns referenced by this node. The column mapping
        /// passed into test as columns must contain all columns listed here.
    };
}
