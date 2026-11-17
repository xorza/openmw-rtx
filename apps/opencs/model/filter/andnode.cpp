#include "andnode.hpp"

#include <cstdint>

#include "narynode.hpp"
#include "node.hpp"

CSMFilter::AndNode::AndNode(const std::vector<std::shared_ptr<Node>>& nodes)
    : NAryNode(nodes, "and")
{
}

bool CSMFilter::AndNode::test(
    const CSMWorld::IdTableBase& table, std::int32_t row, const std::map<std::int32_t, std::int32_t>& columns) const
{
    std::int32_t size = getSize();

    for (std::int32_t i = 0; i < size; ++i)
        if (!(*this)[i].test(table, row, columns))
            return false;

    return true;
}
