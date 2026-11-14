#pragma once

#include <tuple>

namespace Misc
{
    template <typename TupleType, typename Callable>
    void tupleForEach(TupleType& tuple, Callable&& f)
    {
        std::apply([&f](auto&... x) { (f(x), ...); }, tuple);
    }
}
