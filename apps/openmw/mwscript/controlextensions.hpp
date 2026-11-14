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
    /// \brief player controls-related script functionality
    namespace Control
    {
        void installOpcodes(Interpreter::Interpreter& interpreter);
    }
}
