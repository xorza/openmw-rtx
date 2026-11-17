#include "stream.hpp"

#include <cstdint>
#include <format>
#include <stdexcept>

namespace Spt
{
    std::int32_t SPTStream::readSection()
    {
        std::int32_t value = 0;
        read(value);
        return value;
    }

    void SPTStream::expectSection(std::int32_t expected)
    {
        const std::int32_t section = readSection();
        if (section != expected)
            throw std::runtime_error(std::format("Expected section {}, got {}", expected, section));
    }
}
