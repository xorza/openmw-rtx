#include "notnode.hpp"

#include <cstdint>

#include "node.hpp"
#include "unarynode.hpp"

namespace CSMWorld
{
    class IdTableBase;
}

CSMFilter::NotNode::NotNode(std::shared_ptr<Node> child)
    : UnaryNode(std::move(child), "not")
{
}

bool CSMFilter::NotNode::test(
    const CSMWorld::IdTableBase& table, std::int32_t row, const std::map<std::int32_t, std::int32_t>& columns) const
{
    return !getChild().test(table, row, columns);
}
