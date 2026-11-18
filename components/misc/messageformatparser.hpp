#pragma once

#include <cstdint>
#include <string_view>

namespace Misc
{
    class MessageFormatParser
    {
    protected:
        enum Placeholder
        {
            StringPlaceholder,
            IntegerPlaceholder,
            FloatPlaceholder
        };

        enum Flags
        {
            None = 0,
            PositiveSpace = 1,
            PositiveSign = 2,
            AlignLeft = 4,
            PrependZero = 8,
            AlternateForm = 16
        };

        enum class Notation : char
        {
            Fixed = 'f',
            ScientificUpper = 'E',
            ScientificLower = 'e',
            ShortestUpper = 'G',
            ShortestLower = 'g',
            HexUpper = 'A',
            HexLower = 'a'
        };

        virtual void visitedPlaceholder(Placeholder placeholder, std::int32_t flags, std::int32_t width,
            std::int32_t precision, Notation notation) = 0;
        virtual void visitedCharacter(char c) = 0;

    public:
        virtual ~MessageFormatParser();

        virtual void process(std::string_view message);
    };
}
