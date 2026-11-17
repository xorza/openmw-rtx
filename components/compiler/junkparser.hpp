#pragma once

#include <cstdint>

#include "parser.hpp"

namespace Compiler
{
    /// \brief Parse an optional single junk token
    class JunkParser : public Parser
    {
        std::int32_t mIgnoreKeyword;

    public:
        JunkParser(ErrorHandler& errorHandler, const Context& context, std::int32_t ignoreKeyword = -1);

        bool parseInt(std::int32_t value, const TokenLoc& loc, Scanner& scanner) override;
        ///< Handle an int token.
        /// \return fetch another token?

        bool parseFloat(float value, const TokenLoc& loc, Scanner& scanner) override;
        ///< Handle a float token.
        /// \return fetch another token?

        bool parseName(const std::string& name, const TokenLoc& loc, Scanner& scanner) override;
        ///< Handle a name token.
        /// \return fetch another token?

        bool parseKeyword(std::int32_t keyword, const TokenLoc& loc, Scanner& scanner) override;
        ///< Handle a keyword token.
        /// \return fetch another token?

        bool parseSpecial(std::int32_t code, const TokenLoc& loc, Scanner& scanner) override;
        ///< Handle a special character token.
        /// \return fetch another token?
    };
}
