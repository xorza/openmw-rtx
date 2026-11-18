#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <components/interpreter/types.hpp>

namespace Compiler
{
    class Literals;

    /// Typedef for script arguments string
    /** Every character reperesents an argument to the command. All arguments are required until a /, after which
        every argument is optional. <BR>
        Eg: fff/f represents 3 required floats followed by one optional float <BR>
        f - Float <BR>
        c - String, case smashed <BR>
        l - Integer <BR>
        s - Short <BR>
        S - String, case preserved <BR>
        x - Optional, ignored string argument. Emits a parser warning when this argument is supplied. <BR>
        X - Optional, ignored numeric expression. Emits a parser warning when this argument is supplied. <BR>
        z - Optional, ignored string or numeric argument. Emits a parser warning when this argument is supplied. <BR>
        j - A piece of junk (either . or a specific keyword)
    **/
    typedef std::string ScriptArgs;

    /// Typedef for script return char
    /** The character represents the type of data being returned. <BR>
        f - float <BR>
        S - String (Cell names) <BR>
        l - Integer
    **/
    typedef char ScriptReturn;

    /// \brief Collection of compiler extensions
    class Extensions
    {

        struct Function
        {
            char mReturn{};
            ScriptArgs mArguments;
            std::int32_t mCode{};
            std::int32_t mCodeExplicit{};
            std::int32_t mSegment{};
        };

        struct Instruction
        {
            ScriptArgs mArguments;
            std::int32_t mCode{};
            std::int32_t mCodeExplicit{};
            std::int32_t mSegment{};
        };

        std::int32_t mNextKeywordIndex;
        std::map<std::string, std::int32_t> mKeywords;
        std::map<std::int32_t, Function> mFunctions;
        std::map<std::int32_t, Instruction> mInstructions;

    public:
        Extensions();

        std::int32_t searchKeyword(const std::string& keyword) const;
        ///< Return extension keyword code, that is assigned to the string \a keyword.
        /// - if no match is found 0 is returned.
        /// - keyword must be all lower case.

        bool isFunction(
            std::int32_t keyword, ScriptReturn& returnType, ScriptArgs& argumentType, bool& explicitReference) const;
        ///< Is this keyword registered with a function? If yes, return return and argument
        /// types.
        /// \param explicitReference In: has explicit reference; Out: set to false, if
        /// explicit reference is not available for this instruction.

        bool isInstruction(std::int32_t keyword, ScriptArgs& argumentType, bool& explicitReference) const;
        ///< Is this keyword registered with a function? If yes, return argument types.
        /// \param explicitReference In: has explicit reference; Out: set to false, if
        /// explicit reference is not available for this instruction.

        void registerFunction(std::string_view keyword, ScriptReturn returnType, std::string_view argumentType,
            std::int32_t code, std::int32_t codeExplicit = -1);
        ///< Register a custom function
        /// - keyword must be all lower case.
        /// - keyword must be unique
        /// - if explicit references are not supported, segment5codeExplicit must be set to -1
        /// \note Currently only segment 3 and segment 5 opcodes are supported.

        void registerInstruction(
            std::string_view keyword, std::string_view argumentType, std::int32_t code, std::int32_t codeExplicit = -1);
        ///< Register a custom instruction
        /// - keyword must be all lower case.
        /// - keyword must be unique
        /// - if explicit references are not supported, segment5codeExplicit must be set to -1
        /// \note Currently only segment 3 and segment 5 opcodes are supported.

        void generateFunctionCode(std::int32_t keyword, std::vector<Interpreter::Type_Code>& code, Literals& literals,
            const std::string& id, std::int32_t optionalArguments) const;
        ///< Append code for function to \a code.

        void generateInstructionCode(std::int32_t keyword, std::vector<Interpreter::Type_Code>& code,
            Literals& literals, const std::string& id, std::int32_t optionalArguments) const;
        ///< Append code for function to \a code.

        void listKeywords(std::vector<std::string>& keywords) const;
        ///< Append all known keywords to \a kaywords.
    };
}
