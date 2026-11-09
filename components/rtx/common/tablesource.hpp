#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>

namespace Rtx
{
    /// Where a table the build makes is declared: the header that names it, spelled from the source root, and
    /// its name in `Rtx`.
    struct TableName
    {
        std::string_view mHeader;
        std::string_view mName;
    };

    /// Writes the C++ source that defines the table `name` holding `values`, which the build compiles in.
    /// A float is spelled in hexadecimal, so the compiled table is the made one to the bit. Written beside
    /// `to` and renamed over it, so a tool stopped part way leaves no table the build would take as made.
    void writeTable(const std::filesystem::path& to, const TableName& name, std::span<const float> values);
    void writeTable(const std::filesystem::path& to, const TableName& name, std::span<const std::uint8_t> values);
}
