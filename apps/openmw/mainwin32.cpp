#include <cstdint>
#include <cstdlib>

#include <components/misc/windows.hpp>

#include "entry.hpp"

// Asks a system with AMD's switchable graphics for its discrete card.
extern "C" __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 0x00000001;

// Where a Windows build without a console starts, its command line in the C runtime's `__argc` and `__argv`.
std::int32_t WINAPI WinMain(
    HINSTANCE /*instance*/, HINSTANCE /*previous*/, LPSTR /*commandLine*/, std::int32_t /*showCommand*/)
{
    return OMW::runGame(__argc, __argv);
}
