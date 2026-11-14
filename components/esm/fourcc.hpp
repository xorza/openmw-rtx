#pragma once

namespace ESM
{
    inline constexpr unsigned int fourCC(const char (&name)[5])
    {
        return static_cast<unsigned char>(name[0]) | (static_cast<unsigned char>(name[1]) << 8)
            | (static_cast<unsigned char>(name[2]) << 16) | (static_cast<unsigned char>(name[3]) << 24);
    }
}
