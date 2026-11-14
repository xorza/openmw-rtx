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
    /// \brief Container-related script functionality (chests, NPCs, creatures)
    namespace Container
    {
        void installOpcodes(Interpreter::Interpreter& interpreter);
    }
}
