#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace Version
{
    std::string_view getVersion();
    std::string_view getCommitHash();
    std::string_view getTagHash();
    std::int32_t getLuaApiRevision();
    std::int32_t getPostprocessingApiRevision();

    // Prepares string that contains version and commit hash.
    std::string getOpenmwVersionDescription();

    bool checkResourcesVersion(const std::filesystem::path& resourcePath);

    std::string_view getDocumentationUrl();
}
