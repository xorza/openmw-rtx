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
    /// \brief Script functionality limited to the console
    namespace Console
    {
        inline void installOpcodes(Interpreter::Interpreter& interpreter) {}
    }
}
