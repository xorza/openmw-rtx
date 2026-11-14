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
    namespace Sound
    {
        // Script-extensions related to sound
        void installOpcodes(Interpreter::Interpreter& interpreter);
    }
}
