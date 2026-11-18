#pragma once

#include <cstdint>
#include <string>

namespace Compiler
{
    /// \brief Location of a token in a source file

    struct TokenLoc
    {
        std::int32_t mColumn;
        std::int32_t mLine;
        std::string mLiteral;

        TokenLoc()
            : mColumn(0)
            , mLine(0)
            , mLiteral()
        {
        }
    };
}
