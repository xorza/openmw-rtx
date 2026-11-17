#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "node.hpp"

namespace CSMFilter
{
    class NAryNode : public Node
    {
        std::vector<std::shared_ptr<Node>> mNodes;
        std::string mName;

    public:
        NAryNode(const std::vector<std::shared_ptr<Node>>& nodes, std::string name);

        std::int32_t getSize() const;

        const Node& operator[](std::int32_t index) const;

        std::vector<std::int32_t> getReferencedColumns() const override;
        ///< Return a list of the IDs of the columns referenced by this node. The column mapping
        /// passed into test as columns must contain all columns listed here.

        std::string toString(bool numericColumns) const override;
        ///< Return a string that represents this node.
        ///
        /// \param numericColumns Use numeric IDs instead of string to represent columns.
    };
}
