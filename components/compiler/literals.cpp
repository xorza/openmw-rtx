#include "literals.hpp"

#include <cstdint>

namespace Compiler
{
    std::int32_t Literals::addInteger(Interpreter::Type_Integer value)
    {
        auto index = static_cast<std::int32_t>(mIntegers.size());

        mIntegers.push_back(value);

        return index;
    }

    std::int32_t Literals::addFloat(Interpreter::Type_Float value)
    {
        auto index = static_cast<std::int32_t>(mFloats.size());

        mFloats.push_back(value);

        return index;
    }

    std::int32_t Literals::addString(const std::string& value)
    {
        auto index = static_cast<std::int32_t>(mStrings.size());

        mStrings.push_back(value);

        return index;
    }

    void Literals::clear()
    {
        mIntegers.clear();
        mFloats.clear();
        mStrings.clear();
    }
}
