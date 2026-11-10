#include <exception>
#include <filesystem>
#include <iostream>

#include "fognoisemaker.hpp"
#include "tablesource.hpp"

// `openmw-rtx-fog-noise <table.cpp>`: the source that compiles `Rtx::sFogNoise` in, as
// `Rtx::makeFogNoise` makes it.

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "usage: openmw-rtx-fog-noise <table.cpp>\n";
        return 2;
    }

    try
    {
        Rtx::writeTable(std::filesystem::path(argv[1]),
            Rtx::TableName{ .mHeader = "components/rtx/world/fognoise.hpp", .mName = "sFogNoise" },
            Rtx::makeFogNoise());
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "openmw-rtx-fog-noise: " << error.what() << '\n';
        return 1;
    }
}
