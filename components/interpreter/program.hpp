#pragma once

#include <string>
#include <vector>

#include "types.hpp"

namespace Interpreter
{
    struct Program
    {
        std::vector<Type_Code> mInstructions;
        std::vector<Type_Integer> mIntegers;
        std::vector<Type_Float> mFloats;
        std::vector<std::string> mStrings;
    };
}
