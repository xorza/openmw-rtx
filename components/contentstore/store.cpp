#include "store.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <cstddef>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <components/debug/debuglog.hpp>
#include <components/files/conversion.hpp>
#include <components/misc/bytebuffer.hpp>
#include <components/misc/digestwords.hpp>
#include <components/misc/hashstate.hpp>
#include <components/platform/folder.hpp>
#include <components/platform/wholefile.hpp>

namespace ContentStore
{
    namespace
    {
        /// What an entry's file ends with, after its payload. **The payload's own length and digest**,
        /// as `PipelineCache` writes its own, because a file a power loss zeroed after its rename, or
        /// one cut short, passes every check of the fields before them. Written `Durability::Whole`
        /// and not synced, as `PipelineCache` syncs its one file a session: the digest finds what a
        /// sync would have kept, and thousands of entries a load would each wait on the disk.
        ///
        /// **At the end and not the front**, as Parquet puts its footer: a read takes the whole file
        /// into the buffer the payload is handed in, and drops the trailer by a resize, where a header
        /// taken off the front moved every byte after it.
        struct EntryTrailer
        {
            std::uint32_t mMagic;
            std::uint32_t mFormat;
            std::uint32_t mKind;
            std::uint32_t mVersion;
            Misc::DigestWords mKey;
            Misc::DigestWords mDigest;
            std::uint64_t mPayloadBytes;
        };

        static_assert(sizeof(EntryTrailer) == 56, "the trailer carries no padding");

        /// "RTXC", read as a little-endian word: the ray tracer's store had the format first, and a magic of
        /// another would have refused every entry it holds.
        constexpr std::uint32_t sMagic = 0x43585452u;

        Misc::DigestWords digestOf(std::span<const std::byte> payload)
        {
            Misc::HashState state;
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
            Key mKey;
        };

        /// What a sweep lists beside the temporaries it removes: every entry, for a store that loads its
        /// small ones, or none.
        enum class Listing
        {
            Entries,
            StaleOnly,
        };

        /// What a sweep found, oldest first, and how many temporaries it took away.
        struct Swept
        {
            std::vector<StoredFile> mKept;
            std::uint64_t mStale = 0;
        };

        /// Lists every entry under `folder` where `listing` asks for them, and removes a temporary file older
        /// than `Platform::sPartialStaleAfter`.
        Swept sweepFolder(const std::filesystem::path& folder, const Listing listing)
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

                    if (const Platform::Partial partial = Platform::sweepStalePartial(file, now);
                        partial != Platform::Partial::None)
                    {
                        swept.mStale += partial == Platform::Partial::Removed ? 1 : 0;
                        continue;
                    }

                    if (listing == Listing::StaleOnly || !isEntryName(name))
                        continue;

                    Key key;
                    std::from_chars(name.data(), name.data() + 16, key.mHash[0], 16);
                    std::from_chars(name.data() + 16, name.data() + 32, key.mHash[1], 16);
                    const std::uintmax_t bytes = file.file_size(failed);
                    if (failed)
                        continue;

                    swept.mKept.push_back(
                        StoredFile{ .mTime = time, .mBytes = bytes, .mPath = file.path(), .mKey = key });
                }
            }

            // **The oldest first**: an entry's time is its last use, and the start loads the newest.
            std::sort(swept.mKept.begin(), swept.mKept.end(),
                [](const StoredFile& a, const StoredFile& b) { return a.mTime < b.mTime; });
            return swept;
        }
    }

    Store::Store(std::filesystem::path directory, const Policy& policy)
        : mDirectory(std::move(directory))
        , mPolicy(policy)
    {
        if (mPolicy.mHolding.has_value() && mPolicy.mKindCount < 64)
            assert(mPolicy.mHolding->mAtAnySize >> mPolicy.mKindCount == 0
                && "a kind held at any size that the policy does not have");

        std::error_code failed;
        std::filesystem::create_directories(mDirectory, failed);
        if (failed)
            Log(Debug::Warning) << "The content store at " << Files::pathToUnicodeString(mDirectory)
                                << " could not be made, and will miss: " << failed.message();

        mQueue.push_back(Task{ .mKind = TaskKind::Start });
        mWorker = Platform::startThread("content store", [this](const std::stop_token& stop) { serve(stop); });
        mMonitor.give([] {});
    }

    Store::~Store()
    {
        flush();
        mWorker = std::jthread();
    }

    std::filesystem::path Store::pathOf(const Key& key) const
    {
        const std::string name = std::format("{:016x}{:016x}", key.mHash[0], key.mHash[1]);
        return mDirectory / name.substr(0, 2) / name;
    }

    bool Store::read(const Key& key, const std::uint32_t kind, const std::uint32_t version, const Reach reach,
        Misc::ByteBuffer& payload)
    {
        assert(kind < mPolicy.mKindCount && "a kind the store's owner does not have");

        const auto stampOnce = [&] {
            if (!stampsOnce(key))
                return false;
            mQueue.push_back(Task{ .mKind = TaskKind::Stamp, .mKey = key });
            return true;
        };

        bool stamp = false;
        const bool resident = mMonitor.under([&] {
            const auto held = mResident.find(key);
            if (held == mResident.end() || held->second.mKind != kind || held->second.mVersion != version)
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

        // **The path made only where the ask reaches the disk**: three allocations, which a miss on the
        // frame's thread, whose asks reach memory alone, would pay for nothing.
        bool found = false;
        if (reach == Reach::Disk)
        {
            std::uint32_t readKind = 0;
            std::uint32_t readVersion = 0;
            const std::filesystem::path path = pathOf(key);
            found = readFile(path, key, readKind, readVersion, payload);

            // The key is made of its maker's name and version, so a file under it that names another kind
            // is a fault in the file, and not another kind's entry.
            if (found && (readKind != kind || readVersion != version))
            {
                refuse(path);
                found = false;
            }
        }

        stamp = false;
        mMonitor.under([&] {
            if (!found)
            {
                ++mStats.mMissed;
                return;
            }
            ++mStats.mFound;
            keepResident(key, kind, version, payload);
            stamp = stampOnce();
        });
        if (stamp)
            mMonitor.give([] {});

        return found;
    }

    bool Store::holds(const Key& key, const Reach reach)
    {
        const bool resident = mMonitor.under([&] { return mResident.contains(key); });
        std::error_code failed;
        const bool found = resident || (reach == Reach::Disk && std::filesystem::is_regular_file(pathOf(key), failed));

        bool stamp = false;
        mMonitor.under([&] {
            if (!found)
            {
                ++mStats.mMissed;
                return;
            }
            ++mStats.mFound;
            stamp = stampsOnce(key);
            if (stamp)
                mQueue.push_back(Task{ .mKind = TaskKind::Stamp, .mKey = key });
        });
        if (stamp)
            mMonitor.give([] {});
        return found;
    }

    void Store::write(const Key& key, const std::uint32_t kind, const std::uint32_t version, Misc::ByteBuffer payload,
        const Reach reach)
    {
        assert(kind < mPolicy.mKindCount && "a kind the store's owner does not have");
        assert(payload.size() <= sPayloadMost && "an entry past what a stored file may hold");

        // **Room in the queue, or an empty queue**: one payload past the bound still goes alone, and a
        // writer that reaches the disk waits for room, where the frame's thread, which may wait on no
        // disk, drops what has none.
        const auto fits
            = [&] { return mQueuedBytes == 0 || payload.size() <= sQueuedMost - std::min(mQueuedBytes, sQueuedMost); };

        // **Moved into the queue under the lock, and copied nowhere**: the frame's thread asks the
        // store's memory under the same lock, and a copy of a chain there held it for milliseconds.
        bool resident = false;
        while (true)
        {
            bool queued = false;
            bool dropped = false;
            mMonitor.under([&] {
                if (!resident)
                {
                    keepResident(key, kind, version, payload);
                    resident = true;
                }
                if (!fits())
                {
                    if (reach == Reach::Memory)
                    {
                        ++mStats.mDropped;
                        dropped = true;
                    }
                    return;
                }

                // Written now, so its time is this session's: no stamp follows a later hit on it.
                stampsOnce(key);
                mQueuedBytes += payload.size();
                mQueue.push_back(Task{ .mKind = TaskKind::Write,
                    .mKey = key,
                    .mEntryKind = kind,
                    .mVersion = version,
                    .mPayload = std::move(payload) });
                queued = true;
            });
            if (queued)
            {
                mMonitor.give([] {});
                return;
            }
            if (dropped)
                return;

            // No worker left to make room, as once the store is stopping: dropped, as the frame's thread
            // drops, where a wait would spin for ever.
            if (!mMonitor.await(fits))
            {
                mMonitor.under([&] { ++mStats.mDropped; });
                return;
            }
        }
    }

    void Store::discard(const Key& key)
    {
        mMonitor.under([&] {
            const auto held = mResident.find(key);
            if (held != mResident.end())
            {
                mResidentBytes -= residentBytesOf(held->second.mPayload.size());
                mResident.erase(held);
            }
            mQueue.push_back(Task{ .mKind = TaskKind::Discard, .mKey = key });
        });
        mMonitor.give([] {});
    }

    void Store::flush()
    {
        mMonitor.await([&] { return mQueue.empty() && mInFlight == 0; });
    }

    Stats Store::getStats()
    {
        return mMonitor.under([&] { return mStats; });
    }

    void Store::keepResident(
        const Key& key, const std::uint32_t kind, const std::uint32_t version, std::span<const std::byte> payload)
    {
        if (!mPolicy.mHolding.has_value())
            return;
        const bool anySize = kind < 64 && ((mPolicy.mHolding->mAtAnySize >> kind) & 1) != 0;
        if (payload.size() > sResidentMost && !anySize)
            return;

        // **Over what was held under the key**, as two writers of one key replace it.
        const auto held = mResident.find(key);
        const std::size_t replaced = held != mResident.end() ? residentBytesOf(held->second.mPayload.size()) : 0;
        const std::size_t bytes = residentBytesOf(payload.size());
        if (mResidentBytes - replaced + bytes > sResidentBytesMost)
            return;

        mResident.insert_or_assign(key, Resident{ kind, version, Misc::ByteBuffer(payload.begin(), payload.end()) });
        mResidentBytes = mResidentBytes - replaced + bytes;
    }

    std::size_t Store::residentBytesOf(const std::size_t payloadBytes)
    {
        // A node of the table, its key, entry and next link, and a bucket's link beside it.
        constexpr std::size_t node = sizeof(std::pair<const Key, Resident>) + 2 * sizeof(void*);
        return payloadBytes + node;
    }

    bool Store::stampsOnce(const Key& key)
    {
        // A stamp ranks what the start loads into memory, which a store that holds nothing does not load.
        if (!mPolicy.mHolding.has_value())
            return false;

        // Past the bound a hit stamps nothing: each stamp is a task on the queue, and a key no longer
        // remembered would queue one at every hit.
        if (mStamped.size() >= sStampedMost)
            return false;
        return mStamped.insert(key).second;
    }

    void Store::serve(const std::stop_token& stop)
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
            [&](const std::stop_token&) {
                run(task);
                // Let go of the payload, so an idle store holds no write's bytes.
                task.mPayload = Misc::ByteBuffer();
                mMonitor.hand([&] { --mInFlight; });
            });
    }

    void Store::run(Task& task)
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

    void Store::start()
    {
        const Swept swept
            = sweepFolder(mDirectory, mPolicy.mHolding.has_value() ? Listing::Entries : Listing::StaleOnly);
        if (!mPolicy.mHolding.has_value())
        {
            mMonitor.under([&] { mStats.mStale += swept.mStale; });
            return;
        }

        // **The tables sized once, here, at their bounds**, off the frame: a table that grows rehashes
        // under the lock, on the frame's thread or while it waits on the lock, and a session adds keys
        // that no file held at its start. Each bound is exact: `stampsOnce` notes no more keys, and every
        // resident entry costs at least its node. The cost is the buckets, about a million in each
        // table, 16 MiB together.
        mMonitor.under([&] {
            mStats.mStale += swept.mStale;
            mStamped.reserve(sStampedMost);
            mResident.reserve(sResidentBytesMost / residentBytesOf(0));
        });

        // **The small entries into memory, newest first**, so an ask on the frame's thread finds a
        // fact without waiting on the disk (`Reach::Memory`), and the bound keeps the newest.
        Misc::ByteBuffer payload;
        for (auto entry = swept.mKept.rbegin(); entry != swept.mKept.rend(); ++entry)
        {
            if (entry->mBytes > sizeof(EntryTrailer) + sResidentMost)
                continue;

            // Not read where it could not be kept: a store that only grows would read every small
            // file at every start, past the bound, for nothing.
            const std::size_t costs = residentBytesOf(entry->mBytes - sizeof(EntryTrailer));
            if (mMonitor.under([&] { return mResidentBytes + costs > sResidentBytesMost; }))
                continue;

            std::uint32_t kind = 0;
            std::uint32_t version = 0;
            if (readFile(entry->mPath, entry->mKey, kind, version, payload))
                mMonitor.under([&] { keepResident(entry->mKey, kind, version, payload); });
        }
    }

    bool Store::readFile(const std::filesystem::path& path, const Key& key, std::uint32_t& kind, std::uint32_t& version,
        Misc::ByteBuffer& payload)
    {
        // **Bounded before it is read**: a file is untrusted, and one past the most a payload holds is
        // refused on its size, unread rather than allocated for.
        if (const Result<void, Platform::IoError> read
            = Platform::readWhole(path, payload, sizeof(EntryTrailer) + sPayloadMost);
            !read.isOk())
        {
            if (read.error().mFailure == Platform::IoFailure::TooLarge)
                refuse(path);
            return false;
        }

        EntryTrailer trailer{};
        if (payload.size() < sizeof(trailer))
        {
            refuse(path);
            return false;
        }
        const std::size_t payloadBytes = payload.size() - sizeof(trailer);
        std::memcpy(&trailer, payload.data() + payloadBytes, sizeof(trailer));
        payload.resize(payloadBytes);
        if (trailer.mMagic != sMagic || trailer.mFormat != sFormat || trailer.mKey != key.mHash
            || trailer.mKind >= mPolicy.mKindCount || trailer.mPayloadBytes != payloadBytes
            || digestOf(payload) != trailer.mDigest)
        {
            refuse(path);
            return false;
        }

        kind = trailer.mKind;
        version = trailer.mVersion;
        return true;
    }

    void Store::refuse(const std::filesystem::path& path)
    {
        std::error_code failed;
        std::filesystem::remove(path, failed);
        mMonitor.under([&] { ++mStats.mRefused; });
    }

    void Store::writeFile(const Task& task)
    {
        const std::filesystem::path path = pathOf(task.mKey);
        std::error_code failed;

        // **A key whose file stands is stamped and not written again**: a key is its entry's whole
        // input, so the file holds these bytes, and the frame's thread, whose asks reach memory
        // alone, misses an entry too large to hold there in every session and offers it again. A
        // file a fault left is refused by the next read that reaches it, and the next write makes it.
        if (std::filesystem::exists(path, failed))
        {
            if (mPolicy.mHolding.has_value())
                stampFile(task.mKey);
            return;
        }

        std::filesystem::create_directories(path.parent_path(), failed);

        const EntryTrailer trailer{ .mMagic = sMagic,
            .mFormat = sFormat,
            .mKind = task.mEntryKind,
            .mVersion = task.mVersion,
            .mKey = task.mKey.mHash,
            .mDigest = digestOf(task.mPayload),
            .mPayloadBytes = task.mPayload.size() };

        // **Written whole** (`Platform::writeWhole`), so a reader sees a whole entry or none, and two
        // writers of one key, which write the same bytes, leave one.
        const std::array<std::span<const std::byte>, 2> parts{ std::span<const std::byte>(task.mPayload),
            std::as_bytes(std::span(&trailer, 1)) };
        if (const Result<void, Platform::IoError> written
            = Platform::writeWhole(path, parts, Platform::Durability::Whole);
            !written.isOk())
            failed = written.error().mCode;

        mMonitor.under([&] { ++(failed ? mStats.mDropped : mStats.mWritten); });
        if (failed)
        {
            if (!mWriteFailureLogged)
                Log(Debug::Warning) << "The content store could not write " << Files::pathToUnicodeString(path)
                                    << ", and counts every write that fails after it: " << failed.message();
            mWriteFailureLogged = true;
        }
    }

    void Store::stampFile(const Key& key)
    {
        std::error_code failed;
        std::filesystem::last_write_time(pathOf(key), std::filesystem::file_time_type::clock::now(), failed);
        if (!failed)
            mMonitor.under([&] { ++mStats.mStamped; });
    }
}
