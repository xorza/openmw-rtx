#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "types.hpp"

namespace Interpreter
{
    class Context;
    struct Program;

    /// Runtime data and engine interface

    class Runtime
    {
        Context* mContext = nullptr;
        const Program* mProgram = nullptr;
        std::int32_t mPC = 0;
        std::vector<Data> mStack;

    public:
        std::int32_t getPC() const { return mPC; }
        ///< return program counter.

        std::int32_t getIntegerLiteral(std::int32_t index) const;

        float getFloatLiteral(std::int32_t index) const;

        std::string_view getStringLiteral(std::int32_t index) const;

        void configure(const Program& program, Context& context);
        ///< \a context and \a code must exist as least until either configure, clear or
        /// the destructor is called.

        void clear();

        void setPC(std::int32_t value) { mPC = value; }
        ///< set program counter.

        void push(const Data& data);
        ///< push data on stack

        void push(Type_Integer value);
        ///< push integer data on stack.

        void push(Type_Float value);
        ///< push float data on stack.

        void pop();
        ///< pop stack

        Data& operator[](std::int32_t index);
        ///< Access stack member, counted from the top.

        Context& getContext();
    };
}
