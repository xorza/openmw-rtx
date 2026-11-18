// This program generates the file tablesgen.hpp

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include <iconv.h>

void tab()
{
    std::cout << "   ";
}

// write one number with a space in front of it and a comma after it
void num(char i, bool last)
{
    // Convert i to its integer value, i.e. -128 to 127. Printing it directly
    // would result in non-printable characters in the source code, which is bad.
    std::cout << " " << static_cast<std::int32_t>(i);
    if (!last)
        std::cout << ",";
}

// Write one table entry (UTF8 value), 1-5 bytes
void writeChar(char* value, std::int32_t length, bool last, const std::string& comment = "")
{
    assert(length >= 1 && length <= 5);
    tab();
    num(length, false);
    for (std::int32_t i = 0; i < 5; i++)
        num(value[i], last && i == 4);

    if (comment != "")
        std::cout << " // " << comment;

    std::cout << std::endl;
}

// What to write on missing characters
void writeMissing(bool last)
{
    // Just write a space character
    char value[5];
    value[0] = ' ';
    for (std::int32_t i = 1; i < 5; i++)
        value[i] = 0;
    writeChar(value, 1, last, "not part of this charset");
}

std::int32_t write_table(const std::string& charset, const std::string& tableName)
{
    // Write table header
    std::cout << "const static signed char " << tableName << "[] =\n{\n";

    // Open conversion system
    iconv_t cd = iconv_open("UTF-8", charset.c_str());

    // Convert each character from 0 to 255
    for (std::int32_t i = 0; i < 256; i++)
    {
        bool last = (i == 255);

        char input = i;
        char* iptr = &input;
        std::size_t ileft = 1;

        char output[5];
        for (std::int32_t k = 0; k < 5; k++)
            output[k] = 0;
        char* optr = output;
        std::size_t oleft = 5;

        std::size_t res = iconv(cd, &iptr, &ileft, &optr, &oleft);

        if (res)
            writeMissing(last);
        else
            writeChar(output, 5 - oleft, last);
    }

    iconv_close(cd);

    // Finish table
    std::cout << "};\n";

    return 0;
}

int main()
{
    // Write header guard
    std::cout << "#pragma once\n\n";

    // Write namespace
    std::cout << "namespace ToUTF8\n{\n\n";

    // Central European and Eastern European languages that use Latin script, such as
    // Polish, Czech, Slovak, Hungarian, Slovene, Bosnian, Croatian, Serbian (Latin script), Romanian and Albanian.
    std::cout << "\n/// Central European and Eastern European languages that use Latin script,"
                 "\n/// such as Polish, Czech, Slovak, Hungarian, Slovene, Bosnian, Croatian,"
                 "\n/// Serbian (Latin script), Romanian and Albanian."
                 "\n";
    write_table("WINDOWS-1250", "windows_1250");

    // Cyrillic alphabet such as Russian, Bulgarian, Serbian Cyrillic and other languages
    std::cout << "\n/// Cyrillic alphabet such as Russian, Bulgarian, Serbian Cyrillic"
                 "\n/// and other languages"
                 "\n";
    write_table("WINDOWS-1251", "windows_1251");

    // English
    std::cout << "\n/// Latin alphabet used by English and some other Western languages"
                 "\n";
    write_table("WINDOWS-1252", "windows_1252");

    write_table("CP437", "cp437");

    // Close namespace
    std::cout << "\n}\n";

    return 0;
}
