#include "store.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <cstddef>
#include <cstring>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>

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
        /// What an entry's file ends with, after its payload and its segments' table: the payload's own length, the
        /// table's length and digest, as `PipelineCache` writes its own, because a file a power loss zeroed after its
        /// rename, or one cut short, passes every check of the fields before them. Written `Durability::Whole` and not
        /// synced: the digests find what a sync would have kept, and thousands of entries a load would each wait on
        /// the disk. At the end and not the front, as Parquet puts its footer: a read takes the whole file into the
        /// buffer the payload is handed in and drops the table and the trailer by a resize, where a header taken off
        /// the front moved every byte after it.
        struct EntryTrailer
        {
            std::uint32_t mMagic;
            std::uint32_t mFormat;
            std::uint32_t mKind;
            std::uint32_t mVersion;
            Misc::DigestWords mKey;
            Misc::DigestWords mTableDigest;
            std::uint64_t mPayloadBytes;
            std::uint32_t mSegments;
            std::uint32_t mReserved;
        };

        static_assert(sizeof(EntryTrailer) == 64, "the trailer carries no padding");

        /// One segment of the payload in the table before the trailer: where it ends, and its bytes' digest.
        struct SegmentRecord
        {
            std::uint64_t mEnd;
            Misc::DigestWords mDigest;
        };

        static_assert(sizeof(SegmentRecord) == 24, "a segment's record carries no padding");

        /// The bytes a file holds past its payload for `segments` segments: their table and the trailer.
        std::uint64_t tailBytesOf(const std::uint32_t segments)
        {
            return std::uint64_t{ segments } * sizeof(SegmentRecord) + sizeof(EntryTrailer);
        }

        /// "RTXC", read as a little-endian word: the ray tracer's store had the format first, and a magic of
        /// another would have refused every entry it holds.
        constexpr std::uint32_t sMagic = 0x43585452u;

        /// **The one check of a trailer**, `read`'s and `holds`'s: the check that failed, or nothing
        /// where it holds for `key`, a file of `fileBytes`, and `kinds` kinds. The table's and the segments'
        /// digests are the read's alone, which has them.
        std::string_view checkTrailer(
            const EntryTrailer& trailer, const Key& key, const std::uint64_t fileBytes, const std::uint32_t kinds)
        {
            if (trailer.mMagic != sMagic)
                return "its magic";
            if (trailer.mFormat != Store::sFormat)
                return "its format";
            if (trailer.mKey != key.mHash)
                return "its key";
            if (trailer.mKind >= kinds)
                return "its kind";
            if (trailer.mSegments == 0 || trailer.mSegments > Store::sSegmentsMost || trailer.mReserved != 0)
                return "its segments";
            const std::uint64_t tail = tailBytesOf(trailer.mSegments);
            if (fileBytes < tail || trailer.mPayloadBytes != fileBytes - tail)
                return "its length";
            return {};
        }

        /// The last `sizeof(EntryTrailer)` bytes of the file at `path`, and the file's size, or nothing
        /// where it is shorter or does not read.
        struct TailRead
        {
            EntryTrailer mTrailer;
            std::uint64_t mBytes;
        };

        std::optional<TailRead> readTrailer(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file)
                return std::nullopt;
            const std::streamoff bytes = file.tellg();
            if (std::cmp_less(bytes, sizeof(EntryTrailer)))
                return TailRead{ EntryTrailer{}, static_cast<std::uint64_t>(std::max<std::streamoff>(bytes, 0)) };
            TailRead read{ EntryTrailer{}, static_cast<std::uint64_t>(bytes) };
            file.seekg(bytes - static_cast<std::streamoff>(sizeof(EntryTrailer)));
            if (!file.read(reinterpret_cast<char*>(&read.mTrailer), sizeof(EntryTrailer)))
                return std::nullopt;
            return read;
        }

        Misc::DigestWords digestOf(std::span<const std::byte> payload)
        {
            Misc::HashState state;
            state.add(payload);
            return state.getWords();
        }

        /// **The check of a file's segments**, the read's, over `file` whole, whose trailer `checkTrailer` passed: the
        /// check that failed, or nothing where the table's digest holds, its ends never fall and reach the payload's
        /// length, and
        /// each segment's bytes hold their digest.
        std::string_view checkSegments(const EntryTrailer& trailer, std::span<const std::byte> file)
        {
            const auto payloadBytes = static_cast<std::size_t>(trailer.mPayloadBytes);
            const std::span<const std::byte> tableBytes
                = file.subspan(payloadBytes, std::size_t{ trailer.mSegments } * sizeof(SegmentRecord));
            if (digestOf(tableBytes) != trailer.mTableDigest)
                return "its table's digest";

            std::uint64_t start = 0;
            for (std::uint32_t at = 0; at < trailer.mSegments; ++at)
            {
                SegmentRecord record{};
                std::memcpy(&record, tableBytes.data() + std::size_t{ at } * sizeof(SegmentRecord), sizeof(record));
                const bool last = at + 1 == trailer.mSegments;
                if (record.mEnd < start || record.mEnd > trailer.mPayloadBytes
                    || (last && record.mEnd != trailer.mPayloadBytes))
                    return "its segments";
                if (digestOf(
                        file.subspan(static_cast<std::size_t>(start), static_cast<std::size_t>(record.mEnd - start)))
                    != record.mDigest)
                    return "its digest";
                start = record.mEnd;
            }
            return {};
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

        /// Lists every entry under `folder`, one kind's, where `listing` asks for them, into `swept`, and removes a
        /// temporary file older than `Platform::sPartialStaleAfter`.
        void sweepFolder(const std::filesystem::path& folder, const Listing listing, Swept& swept)
        {
            const std::optional<std::vector<std::filesystem::directory_entry>> folders = Platform::listFolder(folder);
            if (!folders.has_value())
                return;

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
        }
    }

    Store::Store(const std::filesystem::path& directory, const Policy& policy)
        : mDirectory(directory / ("v" + std::to_string(sFormat)))
        , mPolicy(policy)
    {
        if (mPolicy.mHolding.has_value())
        {
            if (mPolicy.mKindCount < 64)
                assert(mPolicy.mHolding->mAtAnySize >> mPolicy.mKindCount == 0
                    && "a kind held at any size that the policy does not have");
            assert(mPolicy.mHolding->mResidentBytesMost > 0 && "a store that holds entries in no room");
            assert(mPolicy.mHolding->mResidentCountMost > 0 && "a store that holds no entry");
            assert(mPolicy.mHolding->mStampedMost > 0 && "a store that holds entries and stamps none");
        }

        std::error_code failed;
        std::filesystem::create_directories(mDirectory, failed);
        if (failed)
            Log(Debug::Warning) << "The content store at " << Files::pathToUnicodeString(mDirectory)
                                << " could not be made, and will miss: " << failed.message();

        mMonitor.give([&] {
            mQueue.emplace_back(StartTask{});
            return true;
        });
        mWorker = Platform::startThread("content store", [this](const std::stop_token& stop) { serve(stop); });
    }

    Store::~Store()
    {
        flush();
        mWorker = std::jthread();
    }

    std::filesystem::path Store::pathOf(const Key& key, const std::uint32_t kind) const
    {
        const std::string name = std::format("{:016x}{:016x}", key.mHash[0], key.mHash[1]);
        return mDirectory / std::to_string(kind) / name.substr(0, 2) / name;
    }

    bool Store::read(const Key& key, const std::uint32_t kind, const std::uint32_t version, const Reach reach,
        Misc::ByteBuffer& payload)
    {
        assert(kind < mPolicy.mKindCount && "a kind the store's owner does not have");

        const auto stampOnce = [&](const std::size_t payloadBytes) {
            if (!ranks(kind, payloadBytes) || !stampsOnce(key))
                return false;
            mQueue.emplace_back(StampTask{ .mKey = key, .mKind = kind });
            return true;
        };

        // **Copied after the lock**, which the frame's thread asks for every shape under: the entry is
        // shared and never changed, so the reference taken under it is all the lock is held for.
        std::shared_ptr<const Misc::ByteBuffer> resident;
        mMonitor.give([&] {
            const auto held = mResident.find(key);
            if (held == mResident.end() || held->second.mKind != kind || held->second.mVersion != version)
                return false;
            ++mStats.mFound;
            resident = held->second.mPayload;
            return stampOnce(resident->size());
        });
        if (resident != nullptr)
        {
            payload.assign(resident->begin(), resident->end());
            return true;
        }

        // **The path made only where the ask reaches the disk**: three allocations, which a miss on the
        // frame's thread, whose asks reach memory alone, would pay for nothing.
        bool found = false;
        if (reach == Reach::Disk)
        {
            std::uint32_t readKind = 0;
            std::uint32_t readVersion = 0;
            const std::filesystem::path path = pathOf(key, kind);
            found = readFile(path, key, readKind, readVersion, payload);

            // The key is made of its maker's name and version, so a file under it that names another kind
            // is a fault in the file, and not another kind's entry.
            if (found && (readKind != kind || readVersion != version))
            {
                refuse(path, readKind != kind ? "the kind asked for" : "the version asked for");
                found = false;
            }
        }

        std::shared_ptr<const Misc::ByteBuffer> kept = found ? residentCopy(kind, payload) : nullptr;
        mMonitor.give([&] {
            if (!found)
            {
                ++mStats.mMissed;
                return false;
            }
            ++mStats.mFound;
            if (kept != nullptr)
                keepResident(key, kind, version, std::move(kept));
            return stampOnce(payload.size());
        });

        return found;
    }

    bool Store::holds(const Key& key, const std::uint32_t kind, const Reach reach)
    {
        assert(kind < mPolicy.mKindCount && "a kind the store's owner does not have");

        // What ranks the entry for the start's load (`ranks`), off the entry held or the file's trailer.
        std::size_t payloadBytes = 0;
        bool found = mMonitor.under([&] {
            const auto held = mResident.find(key);
            if (held == mResident.end() || held->second.mKind != kind)
                return false;
            payloadBytes = held->second.mPayload->size();
            return true;
        });
        if (!found && reach == Reach::Disk)
        {
            const std::filesystem::path path = pathOf(key, kind);
            if (const std::optional<TailRead> tail = readTrailer(path))
            {
                std::string_view failed = checkTrailer(tail->mTrailer, key, tail->mBytes, mPolicy.mKindCount);
                if (failed.empty() && tail->mTrailer.mKind != kind)
                    failed = "the kind asked for";
                found = failed.empty();
                if (!found)
                    refuse(path, failed);
                payloadBytes = static_cast<std::size_t>(tail->mTrailer.mPayloadBytes);
            }
        }

        mMonitor.give([&] {
            if (!found)
            {
                ++mStats.mUnheld;
                return false;
            }
            ++mStats.mHeld;
            if (!ranks(kind, payloadBytes) || !stampsOnce(key))
                return false;
            mQueue.emplace_back(StampTask{ .mKey = key, .mKind = kind });
            return true;
        });
        return found;
    }

    void Store::write(Filed entry, const Reach reach)
    {
        assert(entry.mKind < mPolicy.mKindCount && "a kind the store's owner does not have");
        assert(entry.mPayload.size() <= sPayloadMost && "an entry past what a stored file may hold");
        assert(entry.mSegmentEnds.size() < sSegmentsMost && "an entry of more segments than a file may hold");
        assert(std::ranges::is_sorted(entry.mSegmentEnds) && "segments whose ends fall");
        assert((entry.mSegmentEnds.empty() || entry.mSegmentEnds.back() == entry.mPayload.size())
            && "segments that do not end where the payload does");

        const Key& key = entry.mKey;
        const std::uint32_t kind = entry.mKind;
        const std::uint32_t version = entry.mVersion;
        const Misc::ByteBuffer& payload = entry.mPayload;

        // **Room in the queue, or an empty queue**: one payload past the bound still goes alone, and a
        // writer that reaches the disk waits for room, where the frame's thread, which may wait on no
        // disk, drops what has none.
        const std::size_t most = mPolicy.mQueuedBytesMost;
        const auto fits = [&] { return mQueuedBytes == 0 || payload.size() <= most - std::min(mQueuedBytes, most); };

        // Copied before the lock and moved into the queue under it: the frame's thread asks the store's memory under
        // the same lock, and a copy of a chain made there held it for milliseconds.
        std::shared_ptr<const Misc::ByteBuffer> kept = residentCopy(kind, payload);
        while (true)
        {
            bool dropped = false;
            const bool queued = mMonitor.give([&] {
                if (kept != nullptr)
                    keepResident(key, kind, version, std::move(kept));
                if (!fits())
                {
                    if (reach == Reach::Memory)
                    {
                        ++mStats.mDropped;
                        dropped = true;
                    }
                    return false;
                }

                // Written now, so its time is this session's: no stamp follows a later hit on it.
                stampsOnce(key);
                mQueuedBytes += payload.size();
                mQueue.emplace_back(std::move(entry));
                return true;
            });
            if (queued || dropped)
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

    void Store::discard(const Key& key, const std::uint32_t kind)
    {
        mMonitor.give([&] {
            const auto held = mResident.find(key);
            if (held != mResident.end())
            {
                mResidentBytes -= residentBytesOf(held->second.mPayload->size());
                mResident.erase(held);
            }
            mQueue.emplace_back(DiscardTask{ .mKey = key, .mKind = kind });
            return true;
        });
    }

    void Store::flush()
    {
        mMonitor.await([&] { return mQueue.empty() && mInFlight == 0; });
    }

    Stats Store::getStats()
    {
        return mMonitor.under([&] { return mStats; });
    }

    std::shared_ptr<const Misc::ByteBuffer> Store::residentCopy(
        const std::uint32_t kind, const std::span<const std::byte> payload) const
    {
        if (!mPolicy.mHolding.has_value() || (payload.size() > sResidentMost && !isHeldAtAnySize(kind)))
            return nullptr;
        return std::make_shared<const Misc::ByteBuffer>(payload.begin(), payload.end());
    }

    void Store::keepResident(const Key& key, const std::uint32_t kind, const std::uint32_t version,
        std::shared_ptr<const Misc::ByteBuffer> payload)
    {
        // **Over what was held under the key**, as two writers of one key replace it.
        const auto held = mResident.find(key);
        const std::size_t replaced = held != mResident.end() ? residentBytesOf(held->second.mPayload->size()) : 0;
        const std::size_t bytes = residentBytesOf(payload->size());
        if (mResidentBytes - replaced + bytes > mPolicy.mHolding->mResidentBytesMost
            || (held == mResident.end() && mResident.size() >= mPolicy.mHolding->mResidentCountMost))
        {
            ++mStats.mUnheldInMemory;
            return;
        }

        mResident.insert_or_assign(key, Resident{ kind, version, std::move(payload) });
        mResidentBytes = mResidentBytes - replaced + bytes;
    }

    bool Store::isHeldAtAnySize(const std::uint32_t kind) const
    {
        return mPolicy.mHolding.has_value() && kind < 64 && ((mPolicy.mHolding->mAtAnySize >> kind) & 1) != 0;
    }

    std::size_t Store::residentBytesOf(const std::size_t payloadBytes)
    {
        // A node of the table, its key, entry and next link, and a bucket's link beside it.
        constexpr std::size_t node = sizeof(std::pair<const Key, Resident>) + 2 * sizeof(void*);
        return payloadBytes + node;
    }

    bool Store::ranks(const std::uint32_t kind, const std::size_t payloadBytes) const
    {
        return mPolicy.mHolding.has_value() && payloadBytes <= sResidentMost && !isHeldAtAnySize(kind);
    }

    bool Store::stampsOnce(const Key& key)
    {
        // A stamp ranks what the start loads into memory, which a store that holds nothing does not load.
        if (!mPolicy.mHolding.has_value())
            return false;

        // Past the bound a hit stamps nothing: each stamp is a task on the queue, and a key no longer
        // remembered would queue one at every hit.
        if (mStamped.size() >= mPolicy.mHolding->mStampedMost)
            return false;
        return mStamped.insert(key).second;
    }

    void Store::serve(const std::stop_token& stop)
    {
        Task task = StartTask{};
        mMonitor.serve(
            stop, [&] { return !mQueue.empty(); },
            [&] {
                task = std::move(mQueue.front());
                mQueue.pop_front();
                if (const Filed* const write = std::get_if<Filed>(&task))
                    mQueuedBytes -= write->mPayload.size();
                ++mInFlight;
            },
            [&](const std::stop_token&) {
                run(task);
                // Let go of the payload, so an idle store holds no write's bytes.
                task = StartTask{};
                mMonitor.hand([&] { --mInFlight; });
            });
    }

    void Store::run(Task& task)
    {
        std::visit(
            [this]<class Kind>(Kind& one) {
                if constexpr (std::is_same_v<Kind, StartTask>)
                    start();
                else if constexpr (std::is_same_v<Kind, Filed>)
                    writeFile(one);
                else if constexpr (std::is_same_v<Kind, StampTask>)
                    stampFile(one.mKey, one.mKind);
                else
                    refuse(pathOf(one.mKey, one.mKind), "a payload its owner could not read");
            },
            task);
    }

    void Store::start()
    {
        // **A kind held at any size is passed by unread**, its folder swept of temporaries alone: its owner reads the
        // entries its content asks for, and the start loads none of it.
        Swept swept;
        for (std::uint32_t kind = 0; kind < mPolicy.mKindCount; ++kind)
            sweepFolder(mDirectory / std::to_string(kind),
                mPolicy.mHolding.has_value() && !isHeldAtAnySize(kind) ? Listing::Entries : Listing::StaleOnly, swept);

        // **The oldest first**: an entry's time is its last use, and the start loads the newest.
        std::sort(swept.mKept.begin(), swept.mKept.end(),
            [](const StoredFile& a, const StoredFile& b) { return a.mTime < b.mTime; });
        if (!mPolicy.mHolding.has_value())
        {
            mMonitor.under([&] { mStats.mStale += swept.mStale; });
            return;
        }

        // The tables sized once, here, at their bounds, off the frame: a table that grows rehashes under the lock,
        // on the frame's thread or while it waits on the lock, and a session adds keys that no file held at its
        // start. Each bound is exact: `stampsOnce` notes no more keys, and `keepResident` keeps no more entries. The
        // cost is the buckets of both tables, which the owner's caps state (`Holding`).
        const Holding& holding = *mPolicy.mHolding;
        mMonitor.under([&] {
            mStats.mStale += swept.mStale;
            mStamped.reserve(holding.mStampedMost);
            mResident.reserve(holding.mResidentCountMost);
        });

        // The small entries into memory, newest first, so an ask on the frame's thread finds a fact without waiting
        // on the disk (`Reach::Memory`), and the bound keeps the newest. None of a kind held at any size
        // (`Holding`), whose owner reads its content's own; its kind is the file's, so it is read to be told apart.
        Misc::ByteBuffer payload;
        for (const auto& entry : std::views::reverse(swept.mKept))
        {
            if (entry.mBytes > tailBytesOf(1) + sResidentMost)
                continue;

            // Not read where it could not be kept: a store that only grows would read every small
            // file at every start, past the bound, for nothing. A small entry is one segment.
            const std::size_t costs
                = residentBytesOf(entry.mBytes - std::min<std::uintmax_t>(entry.mBytes, tailBytesOf(1)));
            if (mMonitor.under([&] {
                    return mResidentBytes + costs > holding.mResidentBytesMost
                        || mResident.size() >= holding.mResidentCountMost;
                }))
                continue;

            std::uint32_t kind = 0;
            std::uint32_t version = 0;
            if (!readFile(entry.mPath, entry.mKey, kind, version, payload))
                continue;
            if (std::shared_ptr<const Misc::ByteBuffer> kept = residentCopy(kind, payload))
                mMonitor.under([&] { keepResident(entry.mKey, kind, version, std::move(kept)); });
        }
    }

    bool Store::readFile(const std::filesystem::path& path, const Key& key, std::uint32_t& kind, std::uint32_t& version,
        Misc::ByteBuffer& payload)
    {
        // **Bounded before it is read**: a file is untrusted, and one past the most a payload holds is
        // refused on its size, unread rather than allocated for.
        if (const Result<void, Platform::IoError> read
            = Platform::readWhole(path, payload, tailBytesOf(sSegmentsMost) + sPayloadMost);
            !read.isOk())
        {
            if (read.error().mFailure == Platform::IoFailure::TooLarge)
                refuse(path, "its size");
            // The first failure of a read that is no missing file is said, and the rest counted: a folder the store
            // cannot read is a miss a player should hear of.
            else if (read.error().mFailure != Platform::IoFailure::NotFound
                && !mReadFailureLogged.exchange(true, std::memory_order_relaxed))
                Log(Debug::Warning) << "The content store could not read " << Files::pathToUnicodeString(path)
                                    << ", and counts every miss after it: " << read.error().mCode.message();
            return false;
        }

        EntryTrailer trailer{};
        if (payload.size() < sizeof(trailer))
        {
            refuse(path, "its length");
            return false;
        }
        const std::size_t fileBytes = payload.size();
        std::memcpy(&trailer, payload.data() + fileBytes - sizeof(trailer), sizeof(trailer));
        std::string_view failed = checkTrailer(trailer, key, fileBytes, mPolicy.mKindCount);
        if (failed.empty())
            failed = checkSegments(trailer, payload);
        if (!failed.empty())
        {
            refuse(path, failed);
            return false;
        }
        payload.resize(static_cast<std::size_t>(trailer.mPayloadBytes));

        kind = trailer.mKind;
        version = trailer.mVersion;
        return true;
    }

    void Store::refuse(const std::filesystem::path& path, const std::string_view check)
    {
        std::error_code failed;
        std::filesystem::remove(path, failed);
        mMonitor.under([&] { ++mStats.mRefused; });
        if (!mRefusalLogged.exchange(true, std::memory_order_relaxed))
            Log(Debug::Warning) << "The content store refused " << Files::pathToUnicodeString(path) << " on " << check
                                << ", deleted it, and counts every refusal after it";
    }

    void Store::writeFile(const Filed& task)
    {
        const std::filesystem::path path = pathOf(task.mKey, task.mKind);
        std::error_code failed;

        // A key whose file stands is stamped and not written again: a key is its entry's whole input, so the file
        // holds these bytes, and the frame's thread, whose asks reach memory alone, offers an entry too large to hold
        // there in every session. A file a fault left is refused by the next read that reaches it, and the next
        // write makes it.
        if (std::filesystem::exists(path, failed))
        {
            if (ranks(task.mKind, task.mPayload.size()))
                stampFile(task.mKey, task.mKind);
            return;
        }

        std::filesystem::create_directories(path.parent_path(), failed);

        // One segment, the whole payload, where the entry states none.
        std::array<SegmentRecord, sSegmentsMost> records{};
        const std::span<const std::uint64_t> ends = task.mSegmentEnds;
        const std::uint64_t whole = task.mPayload.size();
        const auto segments = static_cast<std::uint32_t>(std::max<std::size_t>(ends.size(), 1));
        std::uint64_t start = 0;
        for (std::uint32_t at = 0; at < segments; ++at)
        {
            const std::uint64_t end = ends.empty() ? whole : ends[at];
            records[at] = SegmentRecord{ .mEnd = end,
                .mDigest = digestOf(std::span<const std::byte>(task.mPayload)
                        .subspan(static_cast<std::size_t>(start), static_cast<std::size_t>(end - start))) };
            start = end;
        }
        const std::span<const SegmentRecord> table(records.data(), segments);

        const EntryTrailer trailer{ .mMagic = sMagic,
            .mFormat = sFormat,
            .mKind = task.mKind,
            .mVersion = task.mVersion,
            .mKey = task.mKey.mHash,
            .mTableDigest = digestOf(std::as_bytes(table)),
            .mPayloadBytes = whole,
            .mSegments = segments,
            .mReserved = 0 };

        // **Written whole** (`Platform::writeWhole`), so a reader sees a whole entry or none, and two
        // writers of one key, which write the same bytes, leave one.
        const std::array<std::span<const std::byte>, 3> parts{ std::span<const std::byte>(task.mPayload),
            std::as_bytes(table), std::as_bytes(std::span(&trailer, 1)) };
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

    void Store::stampFile(const Key& key, const std::uint32_t kind)
    {
        std::error_code failed;
        std::filesystem::last_write_time(pathOf(key, kind), std::filesystem::file_time_type::clock::now(), failed);
        if (!failed)
            mMonitor.under([&] { ++mStats.mStamped; });
    }
}
