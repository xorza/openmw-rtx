#include "unarynode.hpp"

#include <cstdint>
#include <utility>

#include "node.hpp"

CSMFilter::UnaryNode::UnaryNode(std::shared_ptr<Node> child, std::string name)
    : mChild(std::move(child))
    , mName(std::move(name))
{
}

const CSMFilter::Node& CSMFilter::UnaryNode::getChild() const
{
    return *mChild;
}

CSMFilter::Node& CSMFilter::UnaryNode::getChild()
{
    return *mChild;
}

std::vector<std::int32_t> CSMFilter::UnaryNode::getReferencedColumns() const
{
    return mChild->getReferencedColumns();
}

std::string CSMFilter::UnaryNode::toString(bool numericColumns) const
{
    return mName + " " + mChild->toString(numericColumns);
}
