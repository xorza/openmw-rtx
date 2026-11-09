#include "dlss.hpp"

#include <optional>

#include <components/platform/process.hpp>

#include "dlsspaths.hpp"

namespace Platform::Dlss
{
    std::filesystem::path applicationDataDirectory()
    {
        std::filesystem::path directory = runtimeDirectory();
        if (directory.empty())
            directory = std::filesystem::temp_directory_path();
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        return directory;
    }

    std::filesystem::path executableDirectory()
    {
        if (const std::optional<std::filesystem::path> executable = Process::executable(); executable.has_value())
            return executable->parent_path();
        return {};
    }

    bool installed()
    {
        return !featureLibrary().empty();
    }
}
