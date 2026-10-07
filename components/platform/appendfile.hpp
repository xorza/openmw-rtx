#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <utility>

namespace Platform
{
    /// **A file every write lands at the end of, whoever else writes it**, over
    /// `appendfileposix.cpp` and `appendfilewin32.cpp`: `O_APPEND` on POSIX, and on Windows a handle
    /// opened for `FILE_APPEND_DATA` alone, since the C runtime's append moves to the end and then
    /// writes, and a write another process makes between the two is written over. What the game's
    /// log and its crash monitor both write to. Move-only, and closed as it goes.
    class AppendFile
    {
    public:
        /// `path` opened to append, and emptied first where `emptied`. Closed where the system
        /// refused.
        static AppendFile open(const std::filesystem::path& path, bool emptied);

        AppendFile() = default;

        AppendFile(AppendFile&& other) noexcept { std::swap(mHold, other.mHold); }

        AppendFile& operator=(AppendFile&& other) noexcept
        {
            std::swap(mHold, other.mHold);
            return *this;
        }

        AppendFile(const AppendFile&) = delete;
        AppendFile& operator=(const AppendFile&) = delete;
        ~AppendFile();

        bool isOpen() const { return mHold != -1; }

        /// Writes `bytes` at the end, in one write where the system takes it whole.
        void write(std::string_view bytes) const;

    private:
        /// A descriptor or a handle, and -1 for none.
        std::intptr_t mHold = -1;
    };
}
