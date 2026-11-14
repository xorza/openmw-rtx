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
    namespace Misc
    {
        void installOpcodes(Interpreter::Interpreter& interpreter);
    }
}
