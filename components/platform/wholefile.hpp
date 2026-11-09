#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <components/misc/result.hpp>

/// **A file read whole and a file written whole**, the fork's one way to do either, over
/// `wholefileposix.cpp` and `wholefilewin32.cpp`, as Rust's `std::fs::read` and `write` and Go's
/// `os.ReadFile` and `WriteFile` are. A write goes to a temporary beside its target and is renamed
/// over it, as Go's `renameio`, LLVM's `writeToOutput` and Chromium's `ImportantFileWriter` write, so a
/// reader, a crash or a full disk finds the old file or the new one and never a part. What a file is
/// appended to is `AppendFile`'s.
namespace Platform
{
    /// What a caller can tell apart of a read or a write that failed.
    enum class IoFailure : std::uint8_t
    {
        /// The file, or a folder on its path, is not there.
        NotFound,

        /// The system refused: no permission, a read-only volume, or a file another process holds.
        Denied,

        /// The volume or the user's quota is full.
        NoSpace,

        Other,
    };

    /// A failed read or write: its case, and the system's own error for a log.
    struct IoError
    {
        IoFailure mFailure = IoFailure::Other;
        std::error_code mCode;
    };

    /// How far a written file is taken before `writeWhole` answers.
    enum class Durability : std::uint8_t
    {
        /// Whole or not at all to every reader; a power loss may still leave the old file, or a
        /// new name on a body the disk never received, which a format with its own digest finds.
        /// For a cache: thousands of entries a load would each wait on the disk.
        Whole,

        /// Whole, and on the disk with its name: the file synced before the rename and its folder
        /// after it on POSIX, the rename written through on Windows. For what a person keeps: the
        /// settings and a saved game.
        Synced,
    };

    /// What a temporary's name ends with: `<target's name>.<process>-<count>.partial`, in the
    /// target's folder. A process killed between a write and its rename leaves one, which the
    /// folder's owner removes once it is old (`ContentStore`, `PipelineCache`).
    inline constexpr std::string_view sPartialSuffix = ".partial";

    /// How old a temporary is before its folder's owner takes it for one a killed process left: a
    /// live write renames its own within the second.
    inline constexpr std::chrono::hours sPartialStaleAfter{ 24 };

    /// Reads all of `path` into `into`, which is emptied first and keeps its capacity: read to the
    /// end, whatever size the system states, so a file under `/proc` or `/sys` reads as well.
    Result<void, IoError> readWhole(const std::filesystem::path& path, std::vector<std::byte>& into);

    /// The same into a string, for text.
    Result<void, IoError> readWhole(const std::filesystem::path& path, std::string& into);

    /// Writes `parts`, one after another, as the whole of `path`, through a temporary renamed over
    /// it; where `path` is a link, over the file it names. On a failure the temporary is removed and
    /// `path` stands as it was.
    Result<void, IoError> writeWhole(
        const std::filesystem::path& path, std::span<const std::span<const std::byte>> parts, Durability durability);

    /// The same for one buffer.
    inline Result<void, IoError> writeWhole(
        const std::filesystem::path& path, std::span<const std::byte> bytes, Durability durability)
    {
        return writeWhole(path, std::span<const std::span<const std::byte>>(&bytes, 1), durability);
    }

    /// The same for text.
    inline Result<void, IoError> writeWhole(
        const std::filesystem::path& path, std::string_view text, Durability durability)
    {
        return writeWhole(path, std::as_bytes(std::span(text)), durability);
    }
}
