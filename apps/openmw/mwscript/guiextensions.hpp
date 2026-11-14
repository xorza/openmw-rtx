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
    /// \brief GUI-related script functionality
    namespace Gui
    {
        void installOpcodes(Interpreter::Interpreter& interpreter);
    }
}
