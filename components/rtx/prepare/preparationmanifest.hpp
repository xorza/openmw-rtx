#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Rtx
{
    class FileStamps;

    /// What the last preparation read, model by model: the files each model's walk read and a digest of
    /// their stamps, so a start where a model's files stand as they were walks it no more, as ccache's
    /// direct mode trusts a file's stat for its manifest (`docs/rtx/architecture.md`, "The preparation
    /// is incremental by each file's stamp; the lookup is by content"). Kept in one file beside the
    /// store, read whole at the start and written whole at the end; a file of another format or
    /// signature, or one that does not read, is an empty manifest.
    class PreparationManifest
    {
    public:
        /// One model's files and the digest of their stamps.
        struct Entry
        {
            std::uint64_t mDigest = 0;
            std::vector<std::string> mFiles;
        };

        /// The manifest at `path`, made under `signature` — what the preparation computes and how, so
        /// a pass whose version moved walks every model again — or an empty one.
        static PreparationManifest read(const std::filesystem::path& path, std::uint64_t signature);

        /// The digest of `files`' stamps now, in their order.
        static std::uint64_t digestOf(std::span<const std::string> files, FileStamps& stamps);

        /// `model`'s entry, or null where it has none.
        const Entry* find(const std::string& model) const
        {
            const auto found = mEntries.find(model);
            return found != mEntries.end() ? &found->second : nullptr;
        }

        /// Whether `model`'s files stand as they did when it was walked.
        bool standsAsWalked(const std::string& model, FileStamps& stamps) const;

        /// Files `entry` under `model`, over what was there.
        void keep(std::string model, Entry entry);

        /// Writes every entry to `path` under `signature`, through a temporary file renamed into
        /// place; answers whether it could.
        bool write(const std::filesystem::path& path, std::uint64_t signature) const;

        std::size_t size() const { return mEntries.size(); }

    private:
        std::unordered_map<std::string, Entry> mEntries;
    };
}
