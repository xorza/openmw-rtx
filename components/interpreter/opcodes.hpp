#pragma once

#include <cstdint>

namespace Interpreter
{
    class Runtime;

    /// opcode for 0 arguments
    class Opcode0
    {
    public:
        virtual void execute(Runtime& runtime) = 0;

        virtual ~Opcode0() = default;
    };

    /// opcode for 1 argument
    class Opcode1
    {
    public:
        virtual void execute(Runtime& runtime, std::uint32_t arg0) = 0;

        virtual ~Opcode1() = default;
    };

}
