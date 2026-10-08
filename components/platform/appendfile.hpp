#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

#include "uniquehold.hpp"

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

        bool isOpen() const { return mHold.isOpen(); }

        /// Writes `bytes` at the end, in one write where the system takes it whole.
        void write(std::string_view bytes) const;

    private:
        /// A descriptor or a handle, and -1 for none.
        struct Closing
        {
            using Handle = std::intptr_t;
            static constexpr Handle sNone = -1;
            static void close(Handle hold) noexcept;
        };

        UniqueHold<Closing> mHold;
    };
}
