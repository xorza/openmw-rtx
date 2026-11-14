#pragma once

#include <filesystem>
#include <iosfwd>
#include <memory>
#include <string>

namespace Files
{
    std::unique_ptr<std::ifstream> openBinaryInputFileStream(const std::filesystem::path& path);
}
