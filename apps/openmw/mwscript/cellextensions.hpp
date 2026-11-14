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
    /// \brief cell-related script functionality
    namespace Cell
    {
        void installOpcodes(Interpreter::Interpreter& interpreter);
    }
}
