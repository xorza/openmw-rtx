#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include <components/misc/result.hpp>

namespace RtxTool
{
    /// Writes `bytes` as the whole of `path` (`Platform::writeWhole`): a report, a picture, a record a
    /// later run reads. Where it cannot, the answer names the file and the system's reason, so a run
    /// that wrote nothing fails and says where.
    Result<void, std::string> writeReport(const std::filesystem::path& path, std::string_view bytes);
}
