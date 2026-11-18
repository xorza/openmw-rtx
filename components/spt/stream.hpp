#pragma once

#include <cstdint>
#include <istream>
#include <string>

#include <components/files/binarystream.hpp>

namespace Spt
{
    class SPTStream : public Files::BinaryStream
    {
    public:
        using Files::BinaryStream::BinaryStream;

        bool eof() { return mStream->peek() == std::char_traits<char>::eof(); }
        std::int32_t readSection();
        void expectSection(std::int32_t expected);
        std::string readIntString() { return readSizedString(4096); }
    };
}
