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
    /// \brief AI-related script functionality
    namespace Ai
    {
        void installOpcodes(Interpreter::Interpreter& interpreter);
    }
}
