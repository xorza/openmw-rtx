#pragma once

#include <cstdint>

namespace OMW
{
    /// The game from its command line, to its exit status: what `main` runs, and on Windows `WinMain` too
    /// (`mainwin32.cpp`), as a program built for the window starts there.
    std::int32_t runGame(std::int32_t argc, char** argv);
}
