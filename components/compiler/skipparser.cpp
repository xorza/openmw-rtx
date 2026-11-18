#include "skipparser.hpp"

#include <cstdint>

#include "errorhandler.hpp"
#include "scanner.hpp"

namespace Compiler
{
    SkipParser::SkipParser(ErrorHandler& errorHandler, const Context& context, bool reportStrayArguments)
        : Parser(errorHandler, context)
        , mReportStrayArguments(reportStrayArguments)
    {
    }

    void SkipParser::reportStrayArgument(const TokenLoc& loc)
    {
        if (mReportStrayArguments)
            getErrorHandler().warning("Extra argument", loc);
    }

    bool SkipParser::parseInt(std::int32_t value, const TokenLoc& loc, Scanner& scanner)
    {
        reportStrayArgument(loc);
        return true;
    }

    bool SkipParser::parseFloat(float value, const TokenLoc& loc, Scanner& scanner)
    {
        reportStrayArgument(loc);
        return true;
    }

    bool SkipParser::parseName(const std::string& name, const TokenLoc& loc, Scanner& scanner)
    {
        reportStrayArgument(loc);
        return true;
    }

    bool SkipParser::parseKeyword(std::int32_t keyword, const TokenLoc& loc, Scanner& scanner)
    {
        reportStrayArgument(loc);
        return true;
    }

    bool SkipParser::parseSpecial(std::int32_t code, const TokenLoc& loc, Scanner& scanner)
    {
        if (code == Scanner::S_newline)
        {
            if (mReportStrayArguments)
                scanner.putbackSpecial(code, loc);
            return false;
        }
        reportStrayArgument(loc);
        return true;
    }
}
