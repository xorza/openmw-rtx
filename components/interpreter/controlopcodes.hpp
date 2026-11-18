#pragma once

#include <cstdint>
#include <stdexcept>

#include "opcodes.hpp"
#include "runtime.hpp"

namespace Interpreter
{
    class OpReturn : public Opcode0
    {
    public:
        void execute(Runtime& runtime) override { runtime.setPC(-1); }
    };

    class OpSkipZero : public Opcode0
    {
    public:
        void execute(Runtime& runtime) override
        {
            Type_Integer data = runtime[0].mInteger;
            runtime.pop();

            if (data == 0)
                runtime.setPC(runtime.getPC() + 1);
        }
    };

    class OpSkipNonZero : public Opcode0
    {
    public:
        void execute(Runtime& runtime) override
        {
            Type_Integer data = runtime[0].mInteger;
            runtime.pop();

            if (data != 0)
                runtime.setPC(runtime.getPC() + 1);
        }
    };

    class OpJumpForward : public Opcode1
    {
    public:
        void execute(Runtime& runtime, std::uint32_t arg0) override
        {
            if (arg0 == 0)
                throw std::logic_error("infinite loop");

            runtime.setPC(static_cast<std::int32_t>(static_cast<std::uint32_t>(runtime.getPC()) + arg0 - 1));
        }
    };

    class OpJumpBackward : public Opcode1
    {
    public:
        void execute(Runtime& runtime, std::uint32_t arg0) override
        {
            if (arg0 == 0)
                throw std::logic_error("infinite loop");

            runtime.setPC(static_cast<std::int32_t>(static_cast<std::uint32_t>(runtime.getPC()) - arg0 - 1));
        }
    };
}
