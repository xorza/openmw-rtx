#pragma once

#include <cstdint>

#include "parser.hpp"

namespace Compiler
{
    // \brief Skip parser for skipping a line
    //
    // This parser is mainly intended for skipping the rest of a faulty line.

    class SkipParser : public Parser
    {
        bool mReportStrayArguments;

        void reportStrayArgument(const TokenLoc& loc);

    public:
        SkipParser(ErrorHandler& errorHandler, const Context& context, bool reportStrayArguments = false);

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
