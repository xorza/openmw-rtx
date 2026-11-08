#include "preparationmanifest.hpp"

#include <cstring>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

#include <components/rtx/preprocess/contentkey.hpp>
#include <components/vfs/pathutil.hpp>

#include "filestamps.hpp"

namespace Rtx
{
    namespace
    {
        constexpr std::uint32_t sMagic = 0x4d585452; // "RTXM"
        constexpr std::uint32_t sFormat = 1;

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

        private:
            std::span<const char> mBytes;
        };

        template <class T>
        requires std::is_arithmetic_v<T>
        void put(std::string& into, const T value)
        {
            into.append(reinterpret_cast<const char*>(&value), sizeof(T));
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
        std::ifstream file(path, std::ios::binary);
        const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        Reader reader(bytes);
        std::uint32_t magic = 0;
        std::uint32_t format = 0;
        std::uint64_t signedWith = 0;
        std::uint32_t count = 0;
        if (!reader.take(magic) || magic != sMagic || !reader.take(format) || format != sFormat
            || !reader.take(signedWith) || signedWith != signature || !reader.take(count))
            return {};

        PreparationManifest manifest;
        for (std::uint32_t at = 0; at < count; ++at)
        {
            std::string model;
            Entry entry;
            std::uint32_t files = 0;
            if (!reader.take(model) || !reader.take(entry.mDigest) || !reader.take(files))
                return {};
            entry.mFiles.resize(files);
            for (std::string& name : entry.mFiles)
                if (!reader.take(name))
                    return {};
            manifest.mEntries.insert_or_assign(std::move(model), std::move(entry));
        }
        return reader.isEmpty() ? manifest : PreparationManifest{};
    }

    std::uint64_t PreparationManifest::digestOf(const std::span<const std::string> files, FileStamps& stamps)
    {
        ContentDigest digest("prepared", sFormat);
        for (const std::string& name : files)
        {
            digest.add(std::span<const char>(name));
            digest.add(stamps.stampOf(VFS::Path::NormalizedView(name.c_str())));
        }
        return digest.getKey().mHash[0];
    }

    bool PreparationManifest::standsAsWalked(const std::string& model, FileStamps& stamps) const
    {
        const auto found = mEntries.find(model);
        return found != mEntries.end() && digestOf(found->second.mFiles, stamps) == found->second.mDigest;
    }

    void PreparationManifest::keep(std::string model, Entry entry)
    {
        mEntries.insert_or_assign(std::move(model), std::move(entry));
    }

    bool PreparationManifest::write(const std::filesystem::path& path, const std::uint64_t signature) const
    {
        std::string bytes;
        put(bytes, sMagic);
        put(bytes, sFormat);
        put(bytes, signature);
        put(bytes, static_cast<std::uint32_t>(mEntries.size()));
        for (const auto& [model, entry] : mEntries)
        {
            if (!putText(bytes, model))
                return false;
            put(bytes, entry.mDigest);
            put(bytes, static_cast<std::uint32_t>(entry.mFiles.size()));
            for (const std::string& name : entry.mFiles)
                if (!putText(bytes, name))
                    return false;
        }

        std::filesystem::path partial = path;
        partial += ".partial";
        {
            std::ofstream file(partial, std::ios::binary | std::ios::trunc);
            file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            if (!file)
                return false;
        }
        std::error_code failed;
        std::filesystem::rename(partial, path, failed);
        return !failed;
    }
}
