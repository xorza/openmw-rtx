#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <components/resource/imageheader.hpp>

#include "headerderived.hpp"

namespace Rtx
{
    /// What a source file came to where it was last read: its stamp then (`FileStamps`), the header's shape, and what
    /// was derived of its texels.
    struct SourceRecord
    {
        std::uint64_t mStamp = 0;
        Resource::HeaderShape mShape;
        FileFacts mFacts;
    };

    /// **Every source texture's record, by its path and its stamp**, so a session whose files stood as they were
    /// makes each header from its record with no read of its file (`Resource::HeaderDeriver::find`), as Git's index
    /// answers a file whose stat stands and ccache's direct mode trusts its manifest. Kept in one file beside the
    /// store, read whole at the start and written whole where anything moved; a file of another format or signature,
    /// or one that does not read whole, is an empty index.
    ///
    /// **A record of a file no older than the index's own write is not trusted** (`find`), as Git treats a racily
    /// clean entry ([racy-git](https://git-scm.com/docs/racy-git)): a file written in the same tick as the index could
    /// have changed under one stamp. **A path's records, one for each stamp it was read at, the least recently named
    /// dropped past `sRecordsPerPathMost`**, as the preparation's manifest keeps a model's, so two profiles whose
    /// archives hold one path apart read neither again; a record no run named for `sUnnamedDaysMost` days goes.
    ///
    /// Asked and filed from every loader's thread at once, under its lock.
    class SourceIndex
    {
    public:
        /// How many records a path holds, its least recently named dropped past them: one for each install a desk
        /// switches between.
        static constexpr std::size_t sRecordsPerPathMost = 4;

        /// How many days a record stands that no run named: past them its content is gone.
        static constexpr std::uint32_t sUnnamedDaysMost = 30;

        /// The index at `path`, read whole where it stands under this build's signature, written back there.
        explicit SourceIndex(std::filesystem::path path);

        SourceIndex(const SourceIndex&) = delete;
        SourceIndex& operator=(const SourceIndex&) = delete;
        ~SourceIndex() = default;

        /// `path`'s record at `stamp`, where one is filed and its file, last `modified` then, is older than the
        /// index's file was when it was read; marked named on `day`. Nothing where none stands or none is trusted.
        std::optional<SourceRecord> find(
            std::string_view path, std::uint64_t stamp, std::filesystem::file_time_type modified, std::uint32_t day);

        /// Files `record` under `path`, named on `day`: over the record of its stamp where `path` has one, and beside
        /// its others, the least recently named dropped past `sRecordsPerPathMost`.
        void file(std::string_view path, const SourceRecord& record, std::uint32_t day);

        /// Drops every record no run named in the `sUnnamedDaysMost` days before `day`.
        void dropUnnamed(std::uint32_t day);

        /// Whether a record was filed, named on another day or dropped since it was read: what a write waits on.
        bool isChanged() const;

        /// Writes every record to the index's file, whole (`Platform::writeWhole`), where anything changed; answers
        /// whether the file stands as the records do.
        bool write();

        std::size_t size() const;

    private:
        struct Filed
        {
            SourceRecord mRecord;
            std::uint32_t mDay = 0;
        };

        /// A map a `std::string_view` finds an entry of with no string made.
        struct NameHash
        {
            using is_transparent = void;
            std::size_t operator()(std::string_view name) const { return std::hash<std::string_view>{}(name); }
        };

        void read();

        const std::filesystem::path mPath;

        /// When the index's file was last written, as it was read: what a record's file must be older than.
        std::optional<std::filesystem::file_time_type> mWritten;

        mutable std::mutex mMutex;
        std::unordered_map<std::string, std::vector<Filed>, NameHash, std::equal_to<>> mByPath;
        bool mChanged = false;
    };
}
