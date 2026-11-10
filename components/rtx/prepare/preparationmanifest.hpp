#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <components/misc/bytebuffer.hpp>
#include <components/rtx/preprocess/contentkey.hpp>
#include <components/rtx/preprocess/contentpass.hpp>
#include <components/rtx/preprocess/contentstore.hpp>

namespace Rtx
{
    class FileStamps;

    /// What the preparations read, model by model: the files each model's walk read and a digest of
    /// their stamps, so a start where a model's files stand as they were walks it no more, as ccache's
    /// direct mode trusts a file's stat for its manifest (`docs/rtx/architecture.md`, "The preparation
    /// is incremental by each file's stamp; the lookup is by content"). Kept in one file beside the
    /// store, read whole at the start and written whole at the end, where anything moved; a file of
    /// another format or signature, or one that does not read, is an empty manifest.
    ///
    /// **Every content list's models, each until no run named it for `sUnnamedDaysMost` days**, so a
    /// switch between two profiles walks neither again. **Each name held once**: a model's files are
    /// indices into one table of the names, since most files are named by many models.
    ///
    /// **A model stands only where the store holds every entry its walk made or found**, as an action
    /// cache returns a result only where the blobs it names stand (`docs/rtx/architecture.md`, "The
    /// preparation is incremental by each file's stamp; the lookup is by content"): an entry the store
    /// refused, or one deleted by hand, is the model walked again. A walk files the keys it asked the
    /// store for; what a thread answered from its own facts, or a chain another model claimed that run,
    /// is filed under the model that asked first. Each key held once, as the names are.
    class PreparationManifest
    {
    public:
        /// One model's files and the digest of their stamps, as a walk hands them over.
        struct Entry
        {
            std::uint64_t mDigest = 0;
            std::vector<std::string> mFiles;

            /// The store's entries the walk made or found, each once, with the pass that filed it.
            std::vector<StoredKey> mKeys{};
        };

        /// How many days an entry stands that no run named: past them its content list is gone.
        static constexpr std::uint32_t sUnnamedDaysMost = 30;

        PreparationManifest() = default;

        /// Moved and never copied: the index of the names views the names a deque holds, which a move
        /// keeps where they stand and a copy would leave the copy's index viewing the original's.
        PreparationManifest(const PreparationManifest&) = delete;
        PreparationManifest& operator=(const PreparationManifest&) = delete;
        PreparationManifest(PreparationManifest&&) = default;
        PreparationManifest& operator=(PreparationManifest&&) = default;

        /// The manifest at `path`, made under `signature` — what the preparation computes and how, so
        /// a pass whose version moved walks every model again — or an empty one.
        static PreparationManifest read(const std::filesystem::path& path, std::uint64_t signature);

        /// The digest of `files`' stamps now, in their order.
        static std::uint64_t digestOf(std::span<const std::string> files, FileStamps& stamps);

        /// `model`'s entry as it was filed, or nothing where it has none.
        std::optional<Entry> find(std::string_view model) const;

        /// Whether `model`'s files stand as they did when it was walked, and `store` holds every entry
        /// its walk made: a shape read whole into the store's memory, where the frame, which reaches
        /// nothing else, finds it (`StoreReach::Memory`); every other entry by its file
        /// (`ContentStore::holds`, one `stat` an entry, which stamps it). Safe from any thread while
        /// nothing files an entry.
        /// @param scratch what a shape is read into, the caller's to keep.
        bool standsAsWalked(
            std::string_view model, FileStamps& stamps, ContentStore& store, Misc::ByteBuffer& scratch) const;

        /// Files `entry` under `model`, over what was there, named on `day`.
        void keep(std::string_view model, const Entry& entry, std::uint32_t day);

        /// Marks `model`'s entry, which stood as it was walked, named on `day`: a change once a day.
        void name(std::string_view model, std::uint32_t day);

        /// Drops every entry no run named in the `sUnnamedDaysMost` days before `day`.
        void dropUnnamed(std::uint32_t day);

        /// Whether an entry was filed, named on another day or dropped since it was read: what a write
        /// waits on.
        bool isChanged() const { return mChanged; }

        /// Writes every entry to `path` under `signature`, whole (`Platform::writeWhole`), each name it
        /// uses once; answers whether it could.
        bool write(const std::filesystem::path& path, std::uint64_t signature) const;

        std::size_t size() const { return mEntries.size(); }

    private:
        /// A model's entry: its digest, the day a run last named it, its files' run in `mFiles` and its
        /// keys' run in `mKeyRuns`.
        struct Filed
        {
            std::uint64_t mDigest = 0;
            std::uint32_t mDay = 0;
            std::uint32_t mFirst = 0;
            std::uint32_t mCount = 0;
            std::uint32_t mKeysFirst = 0;
            std::uint32_t mKeysCount = 0;
        };

        /// `name`'s index in the table, added where it is not there.
        std::uint32_t intern(std::string_view name);

        /// `key`'s index in the keys' table, added where it is not there.
        std::uint32_t internKey(const StoredKey& key);

        std::optional<std::uint32_t> indexOf(std::string_view name) const;

        /// The names, each once, in a deque so a name's characters stand where the index points.
        std::deque<std::string> mNames;
        std::unordered_map<std::string_view, std::uint32_t> mNameIndex;

        /// By the model's name's index.
        std::unordered_map<std::uint32_t, Filed> mEntries;

        /// Every entry's files, a run each, as indices into `mNames`: an entry filed over leaves its
        /// old run, which the next write leaves out, so the runs hold at most each run's twice.
        std::vector<std::uint32_t> mFiles;

        /// The keys, each once, and every entry's run of them as indices, as the names and the files.
        std::vector<StoredKey> mKeys;
        std::unordered_map<ContentKey, std::uint32_t, ContentKeyHash> mKeyIndex;
        std::vector<std::uint32_t> mKeyRuns;

        bool mChanged = false;
    };
}
