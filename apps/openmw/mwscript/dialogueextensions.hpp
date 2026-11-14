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
    /// \brief Dialogue/Journal-related script functionality
    namespace Dialogue
    {
        void installOpcodes(Interpreter::Interpreter& interpreter);
    }
}
