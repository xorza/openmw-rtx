#pragma once

#include "../mwworld/ptr.hpp"

namespace Interpreter
{
    class Runtime;
}

namespace MWScript
{
    struct ExplicitRef
    {
        static constexpr bool implicit = false;

        MWWorld::Ptr operator()(Interpreter::Runtime& runtime, bool required = true, bool activeOnly = false) const;
    };

    struct ImplicitRef
    {
        static constexpr bool implicit = true;

        MWWorld::Ptr operator()(Interpreter::Runtime& runtime, bool required = true, bool activeOnly = false) const;
    };
}
