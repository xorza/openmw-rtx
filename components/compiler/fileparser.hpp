#pragma once

#include <cstdint>

#include "locals.hpp"
#include "parser.hpp"
#include "scriptparser.hpp"

namespace Compiler
{
    // Top-level parser, to be used for global scripts, local scripts and targeted scripts

    class FileParser : public Parser
    {
        enum State
        {
            BeginState,
            NameState,
            BeginCompleteState,
            EndNameState,
            EndCompleteState
        };

        ScriptParser mScriptParser;
        State mState;
        std::string mName;
        Locals mLocals;

    public:
        FileParser(ErrorHandler& errorHandler, Context& context);

        Interpreter::Program getProgram() const;

        const Locals& getLocals() const;
        ///< get local variable declarations.

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

        void reset() override;
        ///< Reset parser to clean state.
    };
}
