#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include "categories.hpp"

namespace Settings
{
    class SettingsFileParser
    {
    public:
        void loadSettingsFile(const std::filesystem::path& file, CategorySettingValueMap& settings,
            bool base64Encoded = false, bool overrideExisting = false);

        void saveSettingsFile(const std::filesystem::path& file, const CategorySettingValueMap& settings);

    private:
        /// Increment i until it longer points to a whitespace character
        /// in the string or has reached the end of the string.
        /// @return false if we have reached the end of the string
        bool skipWhiteSpace(std::size_t& i, std::string& str);

        [[noreturn]] void fail(const std::string& message);

        std::filesystem::path mFile;
        std::int32_t mLine = 0;
    };
}
