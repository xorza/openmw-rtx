#pragma once

#include <cstdint>

#include "parser.hpp"

namespace Compiler
{
    class Locals;

    class DeclarationParser : public Parser
    {
        enum State
        {
            State_Begin,
            State_Name,
            State_End
        };

        Locals& mLocals;
        State mState;
        char mType;

    public:
        DeclarationParser(ErrorHandler& errorHandler, const Context& context, Locals& locals);

        bool parseName(const std::string& name, const TokenLoc& loc, Scanner& scanner) override;
        ///< Handle a name token.
        /// \return fetch another token?

        bool parseKeyword(std::int32_t keyword, const TokenLoc& loc, Scanner& scanner) override;
        ///< Handle a keyword token.
        /// \return fetch another token?

        bool parseSpecial(std::int32_t code, const TokenLoc& loc, Scanner& scanner) override;
        ///< Handle a special character token.
        /// \return fetch another token?

        bool parseInt(std::int32_t value, const TokenLoc& loc, Scanner& scanner) override;
        ///< Handle an int token.
        /// \return fetch another token?

        void reset() override;
    };
}
