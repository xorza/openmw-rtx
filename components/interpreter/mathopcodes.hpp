#pragma once

#include <cmath>
#include <cstdint>
#include <stdexcept>

#include "opcodes.hpp"
#include "runtime.hpp"

namespace Interpreter
{
    template <typename T>
    class OpAddInt : public Opcode0
    {
    public:
        void execute(Runtime& runtime) override
        {
            T result = getData<T>(runtime[1]) + getData<T>(runtime[0]);

            runtime.pop();

            getData<T>(runtime[0]) = result;
        }
    };

    template <typename T>
    class OpSubInt : public Opcode0
    {
    public:
        void execute(Runtime& runtime) override
        {
            T result = getData<T>(runtime[1]) - getData<T>(runtime[0]);

            runtime.pop();

            getData<T>(runtime[0]) = result;
        }
    };

    template <typename T>
    class OpMulInt : public Opcode0
    {
    public:
        void execute(Runtime& runtime) override
        {
            T result = getData<T>(runtime[1]) * getData<T>(runtime[0]);

            runtime.pop();

            getData<T>(runtime[0]) = result;
        }
    };

    template <typename T>
    class OpDivInt : public Opcode0
    {
    public:
        void execute(Runtime& runtime) override
        {
            T left = getData<T>(runtime[0]);

            if (left == 0)
                throw std::runtime_error("division by zero");

            T result = getData<T>(runtime[1]) / left;

            runtime.pop();

            getData<T>(runtime[0]) = result;
        }
    };

    template <typename T, typename C>
    class OpCompare : public Opcode0
    {
    public:
        void execute(Runtime& runtime) override
        {
            std::int32_t result = C()(getData<T>(runtime[1]), getData<T>(runtime[0]));

            runtime.pop();

            runtime[0].mInteger = result;
        }
    };
}
