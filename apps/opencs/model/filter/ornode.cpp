#include "ornode.hpp"

#include <cstdint>

#include "narynode.hpp"
#include "node.hpp"

namespace CSMWorld
{
    class IdTableBase;
}

CSMFilter::OrNode::OrNode(const std::vector<std::shared_ptr<Node>>& nodes)
    : NAryNode(nodes, "or")
{
}

bool CSMFilter::OrNode::test(
    const CSMWorld::IdTableBase& table, std::int32_t row, const std::map<std::int32_t, std::int32_t>& columns) const
{
    std::int32_t size = getSize();

    for (std::int32_t i = 0; i < size; ++i)
        if ((*this)[i].test(table, row, columns))
            return true;

    return false;
}
