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
    /// \brief Temporary script functionality limited to the console
    namespace User
    {
        void installOpcodes(Interpreter::Interpreter& interpreter);
    }
}
