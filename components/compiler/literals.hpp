#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <components/interpreter/types.hpp>

namespace Compiler
{
    /// \brief Literal values.

    class Literals
    {
        std::vector<Interpreter::Type_Integer> mIntegers;
        std::vector<Interpreter::Type_Float> mFloats;
        std::vector<std::string> mStrings;

    public:
        const std::vector<Interpreter::Type_Integer>& getIntegers() const { return mIntegers; }

        const std::vector<Interpreter::Type_Float>& getFloats() const { return mFloats; }

        const std::vector<std::string>& getStrings() const { return mStrings; }

        std::int32_t addInteger(Interpreter::Type_Integer value);
        ///< add integer liternal and return index.

        std::int32_t addFloat(Interpreter::Type_Float value);
        ///< add float literal and return value.

        std::int32_t addString(const std::string& value);
        ///< add string literal and return value.

        void clear();
        ///< remove all literals.
    };
}
