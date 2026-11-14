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
    namespace Animation
    {
        void registerExtensions(Compiler::Extensions& extensions);

        void installOpcodes(Interpreter::Interpreter& interpreter);
    }
}
