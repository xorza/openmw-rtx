#include "junkparser.hpp"

#include <cstdint>

#include "scanner.hpp"

Compiler::JunkParser::JunkParser(ErrorHandler& errorHandler, const Context& context, std::int32_t ignoreKeyword)
    : Parser(errorHandler, context)
    , mIgnoreKeyword(ignoreKeyword)
{
}

bool Compiler::JunkParser::parseInt(std::int32_t value, const TokenLoc& loc, Scanner& scanner)
{
    scanner.putbackInt(value, loc);
    return false;
}

bool Compiler::JunkParser::parseFloat(float value, const TokenLoc& loc, Scanner& scanner)
{
    scanner.putbackFloat(value, loc);
    return false;
}

bool Compiler::JunkParser::parseName(const std::string& name, const TokenLoc& loc, Scanner& scanner)
{
    scanner.putbackName(name, loc);
    return false;
}

bool Compiler::JunkParser::parseKeyword(std::int32_t keyword, const TokenLoc& loc, Scanner& scanner)
{
    if (keyword == mIgnoreKeyword)
        reportWarning("Ignoring found junk", loc);
    else
        scanner.putbackKeyword(keyword, loc);

    return false;
}

bool Compiler::JunkParser::parseSpecial(std::int32_t code, const TokenLoc& loc, Scanner& scanner)
{
    if (code == Scanner::S_member)
        reportWarning("Ignoring found junk", loc);
    else
        scanner.putbackSpecial(code, loc);

    return false;
}
