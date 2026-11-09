#include "preparationmanifest.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <utility>

#include <components/platform/wholefile.hpp>
#include <components/rtx/preprocess/contentkey.hpp>
#include <components/vfs/pathutil.hpp>

#include "filestamps.hpp"

namespace Rtx
{
    namespace
    {
        constexpr std::uint32_t sMagic = 0x4d585452; // "RTXM"
        constexpr std::uint32_t sFormat = 2;

        /// The most bytes a manifest is read at: a file is untrusted, and a profile's manifest of half a
        /// million names stands near twenty megabytes.
        constexpr std::size_t sMostBytes = std::size_t{ 256 } << 20;

        /// The bytes a manifest is read from, a value at a time, which answer false once short.
        class Reader
        {
        public:
            explicit Reader(std::span<const char> bytes)
                : mBytes(bytes)
            {
            }

            template <class T>
            bool take(T& value)
            {
                if (mBytes.size() < sizeof(T))
                    return false;
                std::memcpy(&value, mBytes.data(), sizeof(T));
                mBytes = mBytes.subspan(sizeof(T));
                return true;
            }

            bool take(std::string& text)
            {
                std::uint16_t length = 0;
                if (!take(length) || mBytes.size() < length)
                    return false;
                text.assign(mBytes.data(), length);
                mBytes = mBytes.subspan(length);
                return true;
            }

            bool isEmpty() const { return mBytes.empty(); }

            std::size_t left() const { return mBytes.size(); }

        private:
            std::span<const char> mBytes;
        };

        template <class T>
        requires std::is_arithmetic_v<T>
        void put(std::string& into, const T value)
        {
            into.append(reinterpret_cast<const char*>(&value), sizeof(T));
        }

        /// The digest of the stamps of `names`, a range of the files' names, in their order.
        template <class Names>
        std::uint64_t digestOfNames(const Names& names, FileStamps& stamps)
        {
            ContentDigest digest("prepared", sFormat);
            for (const std::string& name : names)
            {
                digest.add(std::span<const char>(name));
                digest.add(stamps.stampOf(VFS::Path::NormalizedView(name.c_str())));
            }
            return digest.getKey().mHash[0];
        }

        /// A path longer than a length's sixteen bits is no path a content file names.
        bool putText(std::string& into, std::string_view text)
        {
            if (text.size() > 0xffff)
                return false;
            put(into, static_cast<std::uint16_t>(text.size()));
            into.append(text);
            return true;
        }
    }

    PreparationManifest PreparationManifest::read(const std::filesystem::path& path, const std::uint64_t signature)
    {
        // A manifest that does not read is an empty one, whatever kept it from reading.
        std::string bytes;
        if (!Platform::readWhole(path, bytes, sMostBytes).isOk())
            return {};

        // The layout `write` lays: the header, the names a 16-bit length and its characters each, then
        // the entries, each its model's name's index, its digest, its day and its files' indices.
        // **Every count no larger than the bytes left could hold**, and every index inside the names:
        // the file is untrusted, and a count read whole would be a gigantic allocation.
        Reader reader(bytes);
        std::uint32_t magic = 0;
        std::uint32_t format = 0;
        std::uint64_t signedWith = 0;
        std::uint32_t names = 0;
        if (!reader.take(magic) || magic != sMagic || !reader.take(format) || format != sFormat
            || !reader.take(signedWith) || signedWith != signature || !reader.take(names)
            || names > reader.left() / sizeof(std::uint16_t))
            return {};

        PreparationManifest manifest;
        std::string name;
        for (std::uint32_t at = 0; at < names; ++at)
        {
            if (!reader.take(name) || manifest.intern(name) != at)
                return {};
        }

        std::uint32_t entries = 0;
        constexpr std::size_t entryBytesLeast = 4 + 8 + 4 + 4;
        if (!reader.take(entries) || entries > reader.left() / entryBytesLeast)
            return {};
        for (std::uint32_t at = 0; at < entries; ++at)
        {
            std::uint32_t model = 0;
            Filed filed;
            if (!reader.take(model) || model >= names || !reader.take(filed.mDigest) || !reader.take(filed.mDay)
                || !reader.take(filed.mCount) || filed.mCount > reader.left() / sizeof(std::uint32_t))
                return {};
            filed.mFirst = static_cast<std::uint32_t>(manifest.mFiles.size());
            for (std::uint32_t file = 0; file < filed.mCount; ++file)
            {
                std::uint32_t index = 0;
                if (!reader.take(index) || index >= names)
                    return {};
                manifest.mFiles.push_back(index);
            }
            manifest.mEntries.insert_or_assign(model, filed);
        }
        if (!reader.isEmpty())
            return {};
        return manifest;
    }

    std::uint64_t PreparationManifest::digestOf(const std::span<const std::string> files, FileStamps& stamps)
    {
        return digestOfNames(files, stamps);
    }

    std::optional<std::uint32_t> PreparationManifest::indexOf(const std::string_view name) const
    {
        const auto found = mNameIndex.find(name);
        if (found == mNameIndex.end())
            return std::nullopt;
        return found->second;
    }

    std::uint32_t PreparationManifest::intern(const std::string_view name)
    {
        if (const std::optional<std::uint32_t> found = indexOf(name))
            return *found;
        const auto index = static_cast<std::uint32_t>(mNames.size());
        const std::string& kept = mNames.emplace_back(name);
        mNameIndex.emplace(std::string_view(kept), index);
        return index;
    }

    std::optional<PreparationManifest::Entry> PreparationManifest::find(const std::string_view model) const
    {
        const std::optional<std::uint32_t> index = indexOf(model);
        if (!index.has_value())
            return std::nullopt;
        const auto found = mEntries.find(*index);
        if (found == mEntries.end())
            return std::nullopt;

        Entry entry{ .mDigest = found->second.mDigest, .mFiles = {} };
        entry.mFiles.reserve(found->second.mCount);
        for (std::uint32_t at = 0; at < found->second.mCount; ++at)
            entry.mFiles.push_back(mNames[mFiles[found->second.mFirst + at]]);
        return entry;
    }

    bool PreparationManifest::standsAsWalked(const std::string_view model, FileStamps& stamps) const
    {
        const std::optional<std::uint32_t> index = indexOf(model);
        if (!index.has_value())
            return false;
        const auto found = mEntries.find(*index);
        if (found == mEntries.end())
            return false;

        const auto names = std::span(mFiles).subspan(found->second.mFirst, found->second.mCount)
            | std::views::transform([&](const std::uint32_t at) -> const std::string& { return mNames[at]; });
        return digestOfNames(names, stamps) == found->second.mDigest;
    }

    void PreparationManifest::keep(const std::string_view model, const Entry& entry, const std::uint32_t day)
    {
        const std::uint32_t index = intern(model);
        const auto first = static_cast<std::uint32_t>(mFiles.size());
        for (const std::string& file : entry.mFiles)
            mFiles.push_back(intern(file));
        mEntries.insert_or_assign(index,
            Filed{ .mDigest = entry.mDigest,
                .mDay = day,
                .mFirst = first,
                .mCount = static_cast<std::uint32_t>(entry.mFiles.size()) });
        mChanged = true;
    }

    void PreparationManifest::name(const std::string_view model, const std::uint32_t day)
    {
        const std::optional<std::uint32_t> index = indexOf(model);
        if (!index.has_value())
            return;
        const auto found = mEntries.find(*index);
        if (found == mEntries.end() || found->second.mDay == day)
            return;
        found->second.mDay = day;
        mChanged = true;
    }

    void PreparationManifest::dropUnnamed(const std::uint32_t day)
    {
        mChanged |= std::erase_if(mEntries, [&](const auto& entry) {
            return day > entry.second.mDay && day - entry.second.mDay > sUnnamedDaysMost;
        }) > 0;
    }

    bool PreparationManifest::write(const std::filesystem::path& path, const std::uint64_t signature) const
    {
        // The names the entries use, each once, in the order they are first met: a name an entry
        // filed over or dropped named alone stays behind.
        constexpr std::uint32_t unused = 0xffffffffu;
        std::vector<std::uint32_t> written(mNames.size(), unused);
        std::vector<std::uint32_t> order;
        const auto use = [&](const std::uint32_t index) {
            if (written[index] == unused)
            {
                written[index] = static_cast<std::uint32_t>(order.size());
                order.push_back(index);
            }
            return written[index];
        };
        for (const auto& [model, filed] : mEntries)
        {
            use(model);
            for (std::uint32_t at = 0; at < filed.mCount; ++at)
                use(mFiles[filed.mFirst + at]);
        }

        std::string bytes;
        put(bytes, sMagic);
        put(bytes, sFormat);
        put(bytes, signature);
        put(bytes, static_cast<std::uint32_t>(order.size()));
        for (const std::uint32_t index : order)
            if (!putText(bytes, mNames[index]))
                return false;
        put(bytes, static_cast<std::uint32_t>(mEntries.size()));
        for (const auto& [model, filed] : mEntries)
        {
            put(bytes, written[model]);
            put(bytes, filed.mDigest);
            put(bytes, filed.mDay);
            put(bytes, filed.mCount);
            for (std::uint32_t at = 0; at < filed.mCount; ++at)
                put(bytes, written[mFiles[filed.mFirst + at]]);
        }

        return Platform::writeWhole(path, bytes, Platform::Durability::Whole).isOk();
    }
}
