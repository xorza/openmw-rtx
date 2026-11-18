#include "narynode.hpp"

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <utility>

#include "node.hpp"

CSMFilter::NAryNode::NAryNode(const std::vector<std::shared_ptr<Node>>& nodes, std::string name)
    : mNodes(nodes)
    , mName(std::move(name))
{
}

std::int32_t CSMFilter::NAryNode::getSize() const
{
    return static_cast<std::int32_t>(mNodes.size());
}

const CSMFilter::Node& CSMFilter::NAryNode::operator[](std::int32_t index) const
{
    return *mNodes.at(static_cast<std::size_t>(index));
}

std::vector<std::int32_t> CSMFilter::NAryNode::getReferencedColumns() const
{
    std::vector<std::int32_t> columns;

    for (const auto& node : mNodes)
    {
        std::vector<std::int32_t> columns2 = node->getReferencedColumns();

        columns.insert(columns.end(), columns2.begin(), columns2.end());
    }

    return columns;
}

std::string CSMFilter::NAryNode::toString(bool numericColumns) const
{
    std::ostringstream stream;

    stream << mName << " (";

    bool first = true;
    std::int32_t size = getSize();

    for (std::int32_t i = 0; i < size; ++i)
    {
        if (first)
            first = false;
        else
            stream << ", ";

        stream << (*this)[i].toString(numericColumns);
    }

    stream << ")";

    return stream.str();
}
