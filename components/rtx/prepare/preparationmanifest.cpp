#include "preparationmanifest.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <optional>
#include <ranges>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <components/contentstore/key.hpp>
#include <components/platform/wholefile.hpp>
#include <components/rtx/preprocess/shape/shapepass.hpp>
#include <components/vfs/pathutil.hpp>

#include "filestamps.hpp"

namespace Rtx
{
    namespace
    {
        constexpr std::uint32_t sMagic = 0x4d585452; // "RTXM"
        constexpr std::uint32_t sFormat = 4;

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
            ContentStore::Digest digest("prepared", sFormat);
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

        // The layout `write` lays. **Every count is no larger than the bytes left could hold**, and every
        // index inside the names: the file is untrusted, and a count read whole would be a gigantic allocation.
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
        // **A name no path of the game's spells is no manifest**: the digest reads each as a
        // normalized path, whose view throws on one that is not, on the prepare worker, at every start
        // until the file was deleted by hand.
        for (std::uint32_t at = 0; at < names; ++at)
        {
            if (!reader.take(name) || !VFS::Path::isNormalized(name) || manifest.intern(name) != at)
                return {};
        }

        std::uint32_t keys = 0;
        constexpr std::size_t keyBytes = sizeof(ContentStore::Key) + sizeof(std::uint32_t);
        if (!reader.take(keys) || keys > reader.left() / keyBytes)
            return {};
        for (std::uint32_t at = 0; at < keys; ++at)
        {
            StoredKey key;
            std::uint32_t pass = 0;
            if (!reader.take(key.mKey.mHash) || !reader.take(pass) || pass >= sContentPassCount)
                return {};
            key.mPass = static_cast<ContentPassId>(pass);
            if (manifest.internKey(key) != at)
                return {};
        }

        std::uint32_t entries = 0;
        constexpr std::size_t entryBytesLeast = 4 + 8 + 4 + 4 + 4;
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
            if (!reader.take(filed.mKeysCount) || filed.mKeysCount > reader.left() / sizeof(std::uint32_t))
                return {};
            filed.mKeysFirst = static_cast<std::uint32_t>(manifest.mKeyRuns.size());
            for (std::uint32_t key = 0; key < filed.mKeysCount; ++key)
            {
                std::uint32_t index = 0;
                if (!reader.take(index) || index >= keys)
                    return {};
                manifest.mKeyRuns.push_back(index);
            }
            // Each model's entries of digests of their own and no more of them than a manifest keeps, as `keep`
            // files them.
            const auto [first, last] = manifest.mEntries.equal_range(model);
            if (std::distance(first, last) >= static_cast<std::ptrdiff_t>(sEntriesPerModelMost)
                || std::any_of(first, last, [&](const auto& kept) { return kept.second.mDigest == filed.mDigest; }))
                return {};
            manifest.mEntries.emplace(model, filed);
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

    std::uint32_t PreparationManifest::internKey(const StoredKey& key)
    {
        const auto [found, added] = mKeyIndex.try_emplace(key.mKey, static_cast<std::uint32_t>(mKeys.size()));
        if (added)
            mKeys.push_back(key);
        return found->second;
    }

    std::vector<PreparationManifest::Entry> PreparationManifest::entriesOf(const std::string_view model) const
    {
        std::vector<Entry> entries;
        const std::optional<std::uint32_t> index = indexOf(model);
        if (!index.has_value())
            return entries;
        const auto [first, last] = mEntries.equal_range(*index);
        for (const auto& [_, filed] : std::ranges::subrange(first, last))
        {
            Entry& entry = entries.emplace_back(Entry{ .mDigest = filed.mDigest, .mFiles = {} });
            entry.mFiles.reserve(filed.mCount);
            for (std::uint32_t at = 0; at < filed.mCount; ++at)
                entry.mFiles.push_back(mNames[mFiles[filed.mFirst + at]]);
            entry.mKeys.reserve(filed.mKeysCount);
            for (std::uint32_t at = 0; at < filed.mKeysCount; ++at)
                entry.mKeys.push_back(mKeys[mKeyRuns[filed.mKeysFirst + at]]);
        }
        std::ranges::sort(entries, {}, &Entry::mDigest);
        return entries;
    }

    std::optional<std::uint64_t> PreparationManifest::standingDigest(
        const std::string_view model, FileStamps& stamps, ContentStore::Store& store, Misc::ByteBuffer& scratch) const
    {
        const std::optional<std::uint32_t> index = indexOf(model);
        if (!index.has_value())
            return std::nullopt;
        const auto [first, last] = mEntries.equal_range(*index);
        for (const auto& [_, filed] : std::ranges::subrange(first, last))
        {
            const auto names = std::span(mFiles).subspan(filed.mFirst, filed.mCount)
                | std::views::transform([&](const std::uint32_t at) -> const std::string& { return mNames[at]; });
            if (digestOfNames(names, stamps) != filed.mDigest)
                continue;
            const std::span<const std::uint32_t> keys = std::span(mKeyRuns).subspan(filed.mKeysFirst, filed.mKeysCount);
            const bool held = std::ranges::all_of(keys, [&](const std::uint32_t at) {
                const StoredKey& key = mKeys[at];
                if (key.mPass == ContentPassId::Shape)
                    return store.read(key.mKey, storeKindOf(ContentPassId::Shape), ShapePass::sVersion,
                        ContentStore::Reach::Disk, scratch);
                return store.holds(key.mKey, ContentStore::Reach::Disk);
            });
            if (held)
                return filed.mDigest;
        }
        return std::nullopt;
    }

    void PreparationManifest::keep(const std::string_view model, const Entry& entry, const std::uint32_t day)
    {
        const std::uint32_t index = intern(model);
        const auto first = static_cast<std::uint32_t>(mFiles.size());
        for (const std::string& file : entry.mFiles)
            mFiles.push_back(intern(file));
        const auto keysFirst = static_cast<std::uint32_t>(mKeyRuns.size());
        for (const StoredKey& key : entry.mKeys)
            mKeyRuns.push_back(internKey(key));
        const Filed filed{ .mDigest = entry.mDigest,
            .mDay = day,
            .mFirst = first,
            .mCount = static_cast<std::uint32_t>(entry.mFiles.size()),
            .mKeysFirst = keysFirst,
            .mKeysCount = static_cast<std::uint32_t>(entry.mKeys.size()) };
        mChanged = true;

        auto [kept, last] = mEntries.equal_range(index);
        const auto same
            = std::find_if(kept, last, [&](const auto& other) { return other.second.mDigest == entry.mDigest; });
        if (same != last)
        {
            same->second = filed;
            return;
        }
        if (std::distance(kept, last) >= static_cast<std::ptrdiff_t>(sEntriesPerModelMost))
        {
            // The least recently named, the one filed earliest among those named the same day.
            const auto oldest = std::min_element(kept, last, [](const auto& a, const auto& b) {
                return std::pair(a.second.mDay, a.second.mFirst) < std::pair(b.second.mDay, b.second.mFirst);
            });
            mEntries.erase(oldest);
        }
        mEntries.emplace(index, filed);
    }

    void PreparationManifest::name(const std::string_view model, const std::uint64_t digest, const std::uint32_t day)
    {
        const std::optional<std::uint32_t> index = indexOf(model);
        if (!index.has_value())
            return;
        const auto [first, last] = mEntries.equal_range(*index);
        const auto found = std::find_if(first, last, [&](const auto& kept) { return kept.second.mDigest == digest; });
        if (found == last || found->second.mDay == day)
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
        // And the keys likewise.
        constexpr std::uint32_t unused = 0xffffffffu;
        std::vector<std::uint32_t> written(mNames.size(), unused);
        std::vector<std::uint32_t> order;
        std::vector<std::uint32_t> keyWritten(mKeys.size(), unused);
        std::vector<std::uint32_t> keyOrder;
        const auto use
            = [](std::vector<std::uint32_t>& numbered, std::vector<std::uint32_t>& met, const std::uint32_t index) {
                  if (numbered[index] == unused)
                  {
                      numbered[index] = static_cast<std::uint32_t>(met.size());
                      met.push_back(index);
                  }
              };
        for (const auto& [model, filed] : mEntries)
        {
            use(written, order, model);
            for (std::uint32_t at = 0; at < filed.mCount; ++at)
                use(written, order, mFiles[filed.mFirst + at]);
            for (std::uint32_t at = 0; at < filed.mKeysCount; ++at)
                use(keyWritten, keyOrder, mKeyRuns[filed.mKeysFirst + at]);
        }

        std::string bytes;
        put(bytes, sMagic);
        put(bytes, sFormat);
        put(bytes, signature);
        put(bytes, static_cast<std::uint32_t>(order.size()));
        for (const std::uint32_t index : order)
            if (!putText(bytes, mNames[index]))
                return false;
        put(bytes, static_cast<std::uint32_t>(keyOrder.size()));
        for (const std::uint32_t index : keyOrder)
        {
            for (const std::uint64_t word : mKeys[index].mKey.mHash)
                put(bytes, word);
            put(bytes, static_cast<std::uint32_t>(mKeys[index].mPass));
        }
        put(bytes, static_cast<std::uint32_t>(mEntries.size()));
        for (const auto& [model, filed] : mEntries)
        {
            put(bytes, written[model]);
            put(bytes, filed.mDigest);
            put(bytes, filed.mDay);
            put(bytes, filed.mCount);
            for (std::uint32_t at = 0; at < filed.mCount; ++at)
                put(bytes, written[mFiles[filed.mFirst + at]]);
            put(bytes, filed.mKeysCount);
            for (std::uint32_t at = 0; at < filed.mKeysCount; ++at)
                put(bytes, keyWritten[mKeyRuns[filed.mKeysFirst + at]]);
        }

        return Platform::writeWhole(path, bytes, Platform::Durability::Whole).isOk();
    }
}
