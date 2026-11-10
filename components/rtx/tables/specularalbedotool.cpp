#include <exception>
#include <filesystem>
#include <iostream>

#include "specularalbedomaker.hpp"
#include "tablesource.hpp"

// `openmw-rtx-specular-albedo <table.cpp>`: the source that compiles `Rtx::sSpecularAlbedo` in, as
// `Rtx::makeSpecularAlbedo` makes it.

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "usage: openmw-rtx-specular-albedo <table.cpp>\n";
        return 2;
    }

    try
    {
        Rtx::writeTable(std::filesystem::path(argv[1]),
            Rtx::TableName{ .mHeader = "components/rtx/frame/specularalbedo.hpp", .mName = "sSpecularAlbedo" },
            Rtx::makeSpecularAlbedo());
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "openmw-rtx-specular-albedo: " << error.what() << '\n';
        return 1;
    }
}
