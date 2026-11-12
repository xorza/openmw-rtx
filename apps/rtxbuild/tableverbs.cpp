#include <filesystem>

#include <components/rtx/tables/bluenoisemaker.hpp>
#include <components/rtx/tables/fognoisemaker.hpp>
#include <components/rtx/tables/specularalbedomaker.hpp>
#include <components/rtx/tables/tablesource.hpp>

#include "verbs.hpp"

namespace RtxBuild
{
    void blueNoise(const Arguments arguments)
    {
        Rtx::writeTable(std::filesystem::path(arguments[0]),
            Rtx::TableName{ .mHeader = "components/rtx/frame/bluenoise.hpp", .mName = "sBlueNoise" },
            Rtx::makeBlueNoise());
    }

    void fogNoise(const Arguments arguments)
    {
        Rtx::writeTable(std::filesystem::path(arguments[0]),
            Rtx::TableName{ .mHeader = "components/rtx/world/fognoise.hpp", .mName = "sFogNoise" },
            Rtx::makeFogNoise());
    }

    void specularAlbedo(const Arguments arguments)
    {
        Rtx::writeTable(std::filesystem::path(arguments[0]),
            Rtx::TableName{ .mHeader = "components/rtx/frame/specularalbedo.hpp", .mName = "sSpecularAlbedo" },
            Rtx::makeSpecularAlbedo());
    }
}
