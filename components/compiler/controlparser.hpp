#pragma once

#include <cstdint>
#include <vector>

#include <components/interpreter/types.hpp>

#include "exprparser.hpp"
#include "lineparser.hpp"
#include "parser.hpp"

namespace Compiler
{
    class Locals;
    class Literals;

    // Control structure parser

    class ControlParser : public Parser
    {
        enum State
        {
            StartState,
            IfEndState,
            IfBodyState,
            IfElseifEndState,
            IfElseifBodyState,
            IfElseEndState,
            IfElseBodyState,
            IfEndifState,
            WhileEndState,
            WhileBodyState,
            WhileEndwhileState,
            IfElseJunkState
        };

        typedef std::vector<Interpreter::Type_Code> Codes;
        typedef std::vector<std::pair<Codes, Codes>> IfCodes;

        Locals& mLocals;
        Literals& mLiterals;
        Codes mCode;
        Codes mCodeBlock;
        IfCodes mIfCode; // condition, body
        LineParser mLineParser;
        ExprParser mExprParser;
        State mState;

        bool parseIfBody(std::int32_t keyword, const TokenLoc& loc, Scanner& scanner);

        bool parseWhileBody(std::int32_t keyword, const TokenLoc& loc, Scanner& scanner);

    public:
        ControlParser(ErrorHandler& errorHandler, const Context& context, Locals& locals, Literals& literals);

        void appendCode(std::vector<Interpreter::Type_Code>& code) const;
        ///< store generated code in \a code.

        bool parseName(const std::string& name, const TokenLoc& loc, Scanner& scanner) override;
        ///< Handle a name token.
        /// \return fetch another token?

        bool parseKeyword(std::int32_t keyword, const TokenLoc& loc, Scanner& scanner) override;
        ///< Handle a keyword token.
        /// \return fetch another token?

        bool parseSpecial(std::int32_t code, const TokenLoc& loc, Scanner& scanner) override;
        ///< Handle a special character token.
        /// \return fetch another token?

        void reset() override;
        ///< Reset parser to clean state.
    };
}
