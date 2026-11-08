#include "contentstore.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <cstddef>
#include <cstring>
#include <format>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <components/debug/debuglog.hpp>
#include <components/files/conversion.hpp>
#include <components/platform/folder.hpp>
#include <components/platform/process.hpp>
#include <components/rtx/common/digestwords.hpp>
#include <components/rtx/common/hashstate.hpp>

namespace Rtx
{
    namespace
    {
        /// What an entry's file opens with. **The payload's own length and digest**, as
        /// `PipelineCache` writes its own, because a file a power loss zeroed after its rename, or
        /// one cut short, passes every check of the fields before them. No sync before the rename,
        /// which `PipelineCache` makes for one file a session: the digest finds what a sync would
        /// have kept, and thousands of entries a load would each wait on the disk.
        struct EntryHeader
        {
            std::uint32_t mMagic;
            std::uint32_t mFormat;
            std::uint32_t mPass;
            std::uint32_t mVersion;
            DigestWords mKey;
            DigestWords mDigest;
            std::uint64_t mPayloadBytes;
        };

        static_assert(sizeof(EntryHeader) == 56, "the header carries no padding");

        /// "RTXC", read as a little-endian word.
        constexpr std::uint32_t sMagic = 0x43585452u;

        constexpr std::string_view sPartial = ".partial";

        DigestWords digestOf(std::span<const std::byte> payload)
        {
            HashState state;
            state.add(payload);
            return state.getWords();
        }

        /// Whether `name` is an entry's: the key's 32 hexadecimal digits.
        bool isEntryName(std::string_view name)
        {
            return name.size() == 32 && std::all_of(name.begin(), name.end(), [](char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            });
        }

        /// An entry's file as a sweep lists it.
        struct StoredFile
        {
            std::filesystem::file_time_type mTime;
            std::uintmax_t mBytes;
            std::filesystem::path mPath;
            ContentKey mKey;
        };

        /// What a sweep kept, oldest first, and what it took away.
        struct Swept
        {
            std::vector<StoredFile> mKept;
            std::uint64_t mBytes = 0;
            std::uint64_t mEvicted = 0;
            std::uint64_t mStale = 0;
        };

        /// Lists every entry under `folder`, removes the least recently used until the rest is at
        /// most `target` bytes, and removes a temporary file older than `ContentStore::sStaleAfter`.
        Swept sweepFolder(const std::filesystem::path& folder, std::uint64_t target)
        {
            Swept swept;
            const std::optional<std::vector<std::filesystem::directory_entry>> folders = Platform::listFolder(folder);
            if (!folders.has_value())
                return swept;

            const std::filesystem::file_time_type now = std::filesystem::file_time_type::clock::now();
            for (const std::filesystem::directory_entry& shard : *folders)
            {
                std::error_code failed;
                if (!shard.is_directory(failed) || shard.path().filename().native().size() != 2)
                    continue;

                const std::optional<std::vector<std::filesystem::directory_entry>> files
                    = Platform::listFolder(shard.path());
                if (!files.has_value())
                    continue;

                for (const std::filesystem::directory_entry& file : *files)
                {
                    if (!file.is_regular_file(failed))
                        continue;

                    const std::string name = Files::pathToUnicodeString(file.path().filename());
                    const std::filesystem::file_time_type time = file.last_write_time(failed);
                    if (failed)
                        continue;

                    if (name.ends_with(sPartial))
                    {
                        if (now - time > ContentStore::sStaleAfter && std::filesystem::remove(file.path(), failed))
                            ++swept.mStale;
                        continue;
                    }

                    if (!isEntryName(name))
                        continue;

                    ContentKey key;
                    std::from_chars(name.data(), name.data() + 16, key.mHash[0], 16);
                    std::from_chars(name.data() + 16, name.data() + 32, key.mHash[1], 16);
                    const std::uintmax_t bytes = file.file_size(failed);
                    if (failed)
                        continue;

                    swept.mKept.push_back(
                        StoredFile{ .mTime = time, .mBytes = bytes, .mPath = file.path(), .mKey = key });
                    swept.mBytes += bytes;
                }
            }

            // **The oldest first, until the rest is at the target**: an entry's time is its last use.
            std::sort(swept.mKept.begin(), swept.mKept.end(),
                [](const StoredFile& a, const StoredFile& b) { return a.mTime < b.mTime; });
            std::size_t first = 0;
            for (; first < swept.mKept.size() && swept.mBytes > target; ++first)
            {
                std::error_code failed;
                std::filesystem::remove(swept.mKept[first].mPath, failed);
                swept.mBytes -= swept.mKept[first].mBytes;
                ++swept.mEvicted;
            }
            swept.mKept.erase(swept.mKept.begin(), swept.mKept.begin() + static_cast<std::ptrdiff_t>(first));
            return swept;
        }
    }

    ContentStore::ContentStore(std::filesystem::path directory, const std::uint64_t cap)
        : mDirectory(std::move(directory))
        , mCap(cap)
    {
        std::error_code failed;
        std::filesystem::create_directories(mDirectory, failed);
        if (failed)
            Log(Debug::Warning) << "Rtx: the content store at " << Files::pathToUnicodeString(mDirectory)
                                << " could not be made, and will miss: " << failed.message();

        mQueue.push_back(Task{ .mKind = TaskKind::Start });
        mWorker.start("content store", [this](const Platform::StopToken& stop) { serve(stop); });
        mMonitor.give([] {});
    }

    ContentStore::~ContentStore()
    {
        flush();
        mWorker.stop();
    }

    std::filesystem::path ContentStore::pathOf(const ContentKey& key) const
    {
        const std::string name = std::format("{:016x}{:016x}", key.mHash[0], key.mHash[1]);
        return mDirectory / name.substr(0, 2) / name;
    }

    bool ContentStore::read(const ContentKey& key, const ContentPassId pass, const std::uint32_t version,
        const StoreReach reach, std::vector<std::byte>& payload)
    {
        const auto stampOnce = [&] {
            if (mStamped.insert(key).second)
            {
                mQueue.push_back(Task{ .mKind = TaskKind::Stamp, .mKey = key });
                return true;
            }
            return false;
        };

        bool stamp = false;
        const bool resident = mMonitor.under([&] {
            const auto held = mResident.find(key);
            if (held == mResident.end() || held->second.mPass != pass || held->second.mVersion != version)
                return false;
            payload.assign(held->second.mPayload.begin(), held->second.mPayload.end());
            ++mStats.mFound;
            stamp = stampOnce();
            return true;
        });
        if (stamp)
            mMonitor.give([] {});
        if (resident)
            return true;

        ContentPassId readPass = ContentPassId::Count;
        std::uint32_t readVersion = 0;
        const std::filesystem::path path = pathOf(key);
        bool found = reach == StoreReach::Disk && readFile(path, key, readPass, readVersion, payload);

        // The key is made of the pass's name and version, so a file under it that names another is
        // a fault in the file, and not an entry of another pass.
        if (found && (readPass != pass || readVersion != version))
        {
            refuse(path);
            found = false;
        }

        stamp = false;
        mMonitor.under([&] {
            if (!found)
            {
                ++mStats.mMissed;
                return;
            }
            ++mStats.mFound;
            keepResident(key, pass, version, payload);
            stamp = stampOnce();
        });
        if (stamp)
            mMonitor.give([] {});

        return found;
    }

    void ContentStore::write(const ContentKey& key, const ContentPassId pass, const std::uint32_t version,
        const std::span<const std::byte> payload)
    {
        assert(payload.size() <= sPayloadMost && "a pass's output past what a stored file may hold");

        bool queued = false;
        mMonitor.under([&] {
            keepResident(key, pass, version, payload);
            if (payload.size() > sQueuedMost - std::min(mQueuedBytes, sQueuedMost))
            {
                ++mStats.mDropped;
                return;
            }

            // Written now, so its time is this session's: no stamp follows a later hit on it.
            mStamped.insert(key);
            mQueue.push_back(Task{ .mKind = TaskKind::Write,
                .mKey = key,
                .mPass = pass,
                .mVersion = version,
                .mPayload = std::vector<std::byte>(payload.begin(), payload.end()) });
            mQueuedBytes += payload.size();
            queued = true;
        });
        if (queued)
            mMonitor.give([] {});
    }

    void ContentStore::discard(const ContentKey& key)
    {
        mMonitor.under([&] {
            const auto held = mResident.find(key);
            if (held != mResident.end())
            {
                mResidentBytes -= held->second.mPayload.size();
                mResident.erase(held);
            }
            mQueue.push_back(Task{ .mKind = TaskKind::Discard, .mKey = key });
        });
        mMonitor.give([] {});
    }

    void ContentStore::flush()
    {
        mMonitor.await([&] { return mQueue.empty() && mInFlight == 0; });
    }

    ContentStoreStats ContentStore::getStats()
    {
        return mMonitor.under([&] { return mStats; });
    }

    void ContentStore::keepResident(const ContentKey& key, const ContentPassId pass, const std::uint32_t version,
        std::span<const std::byte> payload)
    {
        if (payload.size() > sResidentMost)
            return;

        // **Over what was held under the key**, as two writers of one key replace it.
        const auto held = mResident.find(key);
        const std::size_t replaced = held != mResident.end() ? held->second.mPayload.size() : 0;
        if (mResidentBytes - replaced + payload.size() > sResidentBytesMost)
            return;

        mResident.insert_or_assign(
            key, Resident{ pass, version, std::vector<std::byte>(payload.begin(), payload.end()) });
        mResidentBytes = mResidentBytes - replaced + payload.size();
    }

    void ContentStore::serve(const Platform::StopToken& stop)
    {
        Task task{ .mKind = TaskKind::Start };
        mMonitor.serve(
            stop, [&] { return !mQueue.empty(); },
            [&] {
                task = std::move(mQueue.front());
                mQueue.pop_front();
                mQueuedBytes -= task.mPayload.size();
                ++mInFlight;
            },
            [&](const Platform::StopToken&) {
                run(task);
                // Let go of the payload, so an idle store holds no write's bytes.
                task.mPayload = std::vector<std::byte>();
                mMonitor.hand([&] { --mInFlight; });
            });
    }

    void ContentStore::run(Task& task)
    {
        switch (task.mKind)
        {
            case TaskKind::Start:
                start();
                return;
            case TaskKind::Write:
                writeFile(task);
                return;
            case TaskKind::Stamp:
                stampFile(task.mKey);
                return;
            case TaskKind::Discard:
                refuse(pathOf(task.mKey));
                return;
        }
    }

    void ContentStore::start()
    {
        const Swept swept = sweepFolder(mDirectory, mCap);
        mStoredBytes = swept.mBytes;
        mMonitor.under([&] {
            mStats.mEvicted += swept.mEvicted;
            mStats.mStale += swept.mStale;
        });

        // **The small entries into memory, newest first**, so an ask on the frame's thread finds a
        // fact without waiting on the disk (`StoreReach::Memory`), and the bound keeps the newest.
        std::vector<std::byte> payload;
        for (auto entry = swept.mKept.rbegin(); entry != swept.mKept.rend(); ++entry)
        {
            if (entry->mBytes > sizeof(EntryHeader) + sResidentMost)
                continue;

            ContentPassId pass = ContentPassId::Count;
            std::uint32_t version = 0;
            if (readFile(entry->mPath, entry->mKey, pass, version, payload))
                mMonitor.under([&] { keepResident(entry->mKey, pass, version, payload); });
        }
    }

    bool ContentStore::readFile(const std::filesystem::path& path, const ContentKey& key, ContentPassId& pass,
        std::uint32_t& version, std::vector<std::byte>& payload)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            return false;

        const std::streamoff bytes = file.tellg();
        EntryHeader header{};
        file.seekg(0);
        if (bytes < static_cast<std::streamoff>(sizeof(header))
            || !file.read(reinterpret_cast<char*>(&header), sizeof(header)) || header.mMagic != sMagic
            || header.mFormat != sFormat || header.mKey != key.mHash
            || header.mPass >= static_cast<std::uint32_t>(ContentPassId::Count) || header.mPayloadBytes > sPayloadMost
            || header.mPayloadBytes != static_cast<std::uint64_t>(bytes) - sizeof(header))
        {
            file.close();
            refuse(path);
            return false;
        }

        payload.resize(static_cast<std::size_t>(header.mPayloadBytes));
        if (!file.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size()))
            || digestOf(payload) != header.mDigest)
        {
            file.close();
            refuse(path);
            return false;
        }

        pass = static_cast<ContentPassId>(header.mPass);
        version = header.mVersion;
        return true;
    }

    void ContentStore::refuse(const std::filesystem::path& path)
    {
        std::error_code failed;
        std::filesystem::remove(path, failed);
        mMonitor.under([&] { ++mStats.mRefused; });
    }

    void ContentStore::writeFile(const Task& task)
    {
        const std::filesystem::path path = pathOf(task.mKey);
        std::error_code failed;

        // **A key whose file stands is stamped and not written again**: a key is its pass's whole
        // input, so the file holds these bytes, and the frame's thread, whose asks reach memory
        // alone, misses an entry too large to hold there in every session and offers it again. A
        // file a fault left is refused by the next read that reaches it, and the next write makes it.
        if (std::filesystem::exists(path, failed))
        {
            stampFile(task.mKey);
            return;
        }

        std::filesystem::create_directories(path.parent_path(), failed);

        const EntryHeader header{ .mMagic = sMagic,
            .mFormat = sFormat,
            .mPass = static_cast<std::uint32_t>(task.mPass),
            .mVersion = task.mVersion,
            .mKey = task.mKey.mHash,
            .mDigest = digestOf(task.mPayload),
            .mPayloadBytes = task.mPayload.size() };

        // **Through a temporary in the entry's own folder, renamed into place**, so a reader sees a
        // whole entry or none, and two writers of one key, which write the same bytes, leave one.
        // Named for the process and its count of writes, so no other writer of this store, in this
        // process or another, uses the name at once.
        std::filesystem::path partial = path;
        partial += std::format(".{}-{}{}", Platform::Process::currentId(), mPartials++, sPartial);
        {
            std::ofstream file(partial, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(&header), sizeof(header));
            file.write(reinterpret_cast<const char*>(task.mPayload.data()),
                static_cast<std::streamsize>(task.mPayload.size()));
            file.close();
            failed = file.fail() ? std::make_error_code(std::errc::io_error) : std::error_code();
        }
        if (!failed)
            std::filesystem::rename(partial, path, failed);
        if (failed)
        {
            std::error_code ignored;
            std::filesystem::remove(partial, ignored);
        }

        mMonitor.under([&] { ++(failed ? mStats.mDropped : mStats.mWritten); });
        if (failed)
            return;

        // **Kept under the cap within a session as well as at its start**, as ccache keeps its
        // limit at every compile: past the cap, the least recently used go until a tenth of it is
        // free, so a session at the cap does not list the folders after every write. Two stores
        // writing one key count it twice here, which only sweeps sooner, and the sweep counts the
        // folders anew.
        mStoredBytes += sizeof(EntryHeader) + task.mPayload.size();
        if (mStoredBytes > mCap)
        {
            const Swept swept = sweepFolder(mDirectory, mCap - mCap / 10);
            mStoredBytes = swept.mBytes;
            mMonitor.under([&] {
                mStats.mEvicted += swept.mEvicted;
                mStats.mStale += swept.mStale;
            });
        }
    }

    void ContentStore::stampFile(const ContentKey& key)
    {
        std::error_code failed;
        std::filesystem::last_write_time(pathOf(key), std::filesystem::file_time_type::clock::now(), failed);
        if (!failed)
            mMonitor.under([&] { ++mStats.mStamped; });
    }
}
