#pragma once

namespace Compiler
{
    class Extensions;
}

namespace Interpreter
{
    class Interpreter;
}

namespace MWScript
{
    /// \brief sky-related script functionality
    namespace Sky
    {
        void installOpcodes(Interpreter::Interpreter& interpreter);
    }
}
