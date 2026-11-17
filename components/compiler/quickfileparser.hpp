#pragma once

#include <cstdint>

#include "declarationparser.hpp"
#include "parser.hpp"

namespace Compiler
{
    class Locals;

    /// \brief File parser variant that ignores everything but variable declarations
    class QuickFileParser : public Parser
    {
        DeclarationParser mDeclarationParser;

    public:
        QuickFileParser(ErrorHandler& errorHandler, const Context& context, Locals& locals);

        bool parseName(const std::string& name, const TokenLoc& loc, Scanner& scanner) override;
        ///< Handle a name token.
        /// \return fetch another token?

        bool parseKeyword(std::int32_t keyword, const TokenLoc& loc, Scanner& scanner) override;
        ///< Handle a keyword token.
        /// \return fetch another token?

        bool parseSpecial(std::int32_t code, const TokenLoc& loc, Scanner& scanner) override;
        ///< Handle a special character token.
        /// \return fetch another token?

        void parseEOF(Scanner& scanner) override;
        ///< Handle EOF token.
    };
}
