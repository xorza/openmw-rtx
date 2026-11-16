#include "narynode.hpp"

#include <sstream>
#include <utility>

#include "node.hpp"

CSMFilter::NAryNode::NAryNode(const std::vector<std::shared_ptr<Node>>& nodes, std::string name)
    : mNodes(nodes)
    , mName(std::move(name))
{
}

int CSMFilter::NAryNode::getSize() const
{
    return static_cast<int>(mNodes.size());
}

const CSMFilter::Node& CSMFilter::NAryNode::operator[](int index) const
{
    return *mNodes.at(static_cast<std::size_t>(index));
}

std::vector<int> CSMFilter::NAryNode::getReferencedColumns() const
{
    std::vector<int> columns;

    for (const auto& node : mNodes)
    {
        std::vector<int> columns2 = node->getReferencedColumns();

        columns.insert(columns.end(), columns2.begin(), columns2.end());
    }

    return columns;
}

std::string CSMFilter::NAryNode::toString(bool numericColumns) const
{
    std::ostringstream stream;

    stream << mName << " (";

    bool first = true;
    int size = getSize();

    for (int i = 0; i < size; ++i)
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
