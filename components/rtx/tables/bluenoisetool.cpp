#include <exception>
#include <filesystem>
#include <iostream>

#include "bluenoisemaker.hpp"
#include "tablesource.hpp"

// `openmw-rtx-blue-noise <table.cpp>`: the source that compiles `Rtx::sBlueNoise` in, as
// `Rtx::makeBlueNoise` makes it.

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "usage: openmw-rtx-blue-noise <table.cpp>\n";
        return 2;
    }

    try
    {
        Rtx::writeTable(std::filesystem::path(argv[1]),
            Rtx::TableName{ .mHeader = "components/rtx/frame/bluenoise.hpp", .mName = "sBlueNoise" },
            Rtx::makeBlueNoise());
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "openmw-rtx-blue-noise: " << error.what() << '\n';
        return 1;
    }
}
