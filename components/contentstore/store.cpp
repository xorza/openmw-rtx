#include "store.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <format>
#include <fstream>
#include <optional>
#include <stdexcept>
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
            if (trailer.mSegments == 0 || trailer.mSegments > sSegmentsMost || trailer.mReserved != 0)
                return "its segments";
            const std::uint64_t tail = tailBytesOf(trailer.mSegments);
            if (fileBytes < tail || trailer.mPayloadBytes != fileBytes - tail)
                return "its length";
            // On every path, the ranged read's among them, which takes a buffer by what the trailer says.
            if (trailer.mPayloadBytes > Store::sPayloadMost)
                return "its size";
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

        /// How many temporaries and entries of another version a sweep took away.
        struct Swept
        {
            std::uint64_t mStale = 0;
            std::uint64_t mOutdated = 0;
        };

        /// What a set of versions is, as the marker of the last sweep holds it: an FNV-1a over each in order.
        std::uint64_t signatureOf(const std::span<const std::uint32_t> versions)
        {
            std::uint64_t hash = 0xcbf29ce484222325ull;
            for (const std::uint32_t version : versions)
                for (std::uint32_t at = 0; at < 4; ++at)
                {
                    hash ^= (version >> (8 * at)) & 0xffu;
                    hash *= 0x100000001b3ull;
                }
            return hash;
        }

        /// Removes every temporary file under `folder`, one kind's, older than `Platform::sPartialStaleAfter`, and
        /// every entry of another version than `version` where there is one, counting each into `swept`.
        void sweepFolder(const std::filesystem::path& folder, const std::optional<std::uint32_t> version, Swept& swept)
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

                    if (const Platform::Partial partial = Platform::sweepStalePartial(file, now);
                        partial != Platform::Partial::None)
                    {
                        swept.mStale += partial == Platform::Partial::Removed ? 1 : 0;
                        continue;
                    }

                    if (!version.has_value() || !isEntryName(Files::pathToUnicodeString(file.path().filename())))
                        continue;

                    // **An entry of another version than its kind's now is one no ask reaches again**: removed, read
                    // by its trailer, once after the versions moved.
                    if (const std::optional<TailRead> tail = readTrailer(file.path());
                        tail.has_value() && tail->mTrailer.mVersion != *version)
                    {
                        std::error_code removed;
                        swept.mOutdated += std::filesystem::remove(file.path(), removed) ? 1 : 0;
                    }
                }
            }
        }
    }

    Store::Store(const std::filesystem::path& directory, const Policy& policy)
        : mDirectory(directory / ("v" + std::to_string(sFormat)))
        , mPolicy(policy)
    {
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

    bool Store::read(const Key& key, const std::uint32_t kind, const std::uint32_t version, Misc::ByteBuffer& payload)
    {
        assert(kind < mPolicy.mKindCount && "a kind the store's owner does not have");

        std::uint32_t readKind = 0;
        std::uint32_t readVersion = 0;
        const std::filesystem::path path = pathOf(key, kind);
        bool found = readFile(path, key, readKind, readVersion, payload);

        // The key is made of its maker's name and version, so a file under it that names another kind is a fault in
        // the file, and not another kind's entry.
        if (found && (readKind != kind || readVersion != version))
        {
            refuse(path, readKind != kind ? "the kind asked for" : "the version asked for");
            found = false;
        }

        mMonitor.under([&] { ++(found ? mStats.mFound : mStats.mMissed); });
        return found;
    }

    std::optional<Opened> Store::open(const Key& key, const std::uint32_t kind, const std::uint32_t version)
    {
        assert(kind < mPolicy.mKindCount && "a kind the store's owner does not have");

        // The file opened only where it stands: a miss is the common case at a content's first sessions, and opening
        // a missing file throws.
        Opened opened;
        opened.mPath = pathOf(key, kind);
        std::error_code missing;
        const bool found
            = std::filesystem::is_regular_file(opened.mPath, missing) && openFile(opened, key, kind, version);

        mMonitor.under([&] { ++(found ? mStats.mFound : mStats.mMissed); });
        if (!found)
            return std::nullopt;
        return opened;
    }

    bool Store::openFile(Opened& opened, const Key& key, const std::uint32_t kind, const std::uint32_t version)
    {
        std::uint64_t fileBytes = 0;
        try
        {
            opened.mFile = Platform::File::open(opened.mPath);
            fileBytes = Platform::File::size(opened.mFile);
        }
        // Either system's: POSIX throws `std::system_error` and Windows its base.
        catch (const std::runtime_error&)
        {
            opened.mFile = Platform::File::ScopedHandle();
            return false;
        }

        EntryTrailer trailer{};
        std::string_view failed;
        RangeRead read = fileBytes < sizeof(trailer)
            ? RangeRead::Short
            : readRange(opened, fileBytes - sizeof(trailer), std::as_writable_bytes(std::span(&trailer, 1)));
        if (read == RangeRead::Short)
            failed = "its length";
        if (read == RangeRead::Whole)
            failed = checkTrailer(trailer, key, fileBytes, mPolicy.mKindCount);
        if (failed.empty() && (trailer.mKind != kind || trailer.mVersion != version))
            failed = trailer.mKind != kind ? "the kind asked for" : "the version asked for";

        std::array<SegmentRecord, sSegmentsMost> records{};
        if (read == RangeRead::Whole && failed.empty())
        {
            read = readRange(
                opened, trailer.mPayloadBytes, std::as_writable_bytes(std::span(records.data(), trailer.mSegments)));
            if (read == RangeRead::Short)
                failed = "its length";
        }
        // **A read the system failed is a miss, and the file stays**: the fault is the disk's or the moment's, and an
        // entry deleted for it is one a play computes again for nothing.
        if (read == RangeRead::Failed)
        {
            opened.mFile = Platform::File::ScopedHandle();
            return false;
        }
        if (failed.empty()
            && digestOf(std::as_bytes(std::span(records.data(), trailer.mSegments))) != trailer.mTableDigest)
            failed = "its table's digest";

        std::uint64_t start = 0;
        for (std::uint32_t at = 0; failed.empty() && at < trailer.mSegments; ++at)
        {
            const bool last = at + 1 == trailer.mSegments;
            if (records[at].mEnd < start || records[at].mEnd > trailer.mPayloadBytes
                || (last && records[at].mEnd != trailer.mPayloadBytes))
                failed = "its segments";
            opened.mEnds[at] = records[at].mEnd;
            opened.mDigests[at] = records[at].mDigest;
            start = records[at].mEnd;
        }

        if (!failed.empty())
        {
            opened.mFile = Platform::File::ScopedHandle();
            refuse(opened.mPath, failed);
            return false;
        }
        opened.mSegments = trailer.mSegments;
        return true;
    }

    bool Store::readSegments(
        Opened& entry, const std::uint32_t first, const std::uint32_t count, const std::span<std::byte> into)
    {
        assert(count > 0 && "a read of no segment");
        assert(first + count <= entry.mSegments && "segments past the entry's");
        const std::uint64_t start = entry.startOf(first);
        assert(into.size() == entry.endOf(first + count - 1) - start && "a read into another length than its segments");

        const RangeRead read = readRange(entry, start, into);
        bool whole = read == RangeRead::Whole;
        for (std::uint32_t at = first; whole && at < first + count; ++at)
            whole = digestOf(into.subspan(static_cast<std::size_t>(entry.startOf(at) - start),
                        static_cast<std::size_t>(entry.endOf(at) - entry.startOf(at))))
                == entry.mDigests[at];
        if (!whole)
        {
            entry.mFile = Platform::File::ScopedHandle();
            if (read != RangeRead::Failed)
                refuse(entry.mPath, read == RangeRead::Short ? "its length" : "its digest");
        }
        return whole;
    }

    Store::RangeRead Store::readRange(const Opened& entry, const std::uint64_t offset, const std::span<std::byte> into)
    {
        const Result<std::size_t, std::error_code> read
            = Platform::File::readAt(entry.mFile, offset, into.data(), into.size());
        if (read.isOk())
            return read.value() == into.size() ? RangeRead::Whole : RangeRead::Short;

        // Said once, as a whole file that would not read is (`readFile`), and counted as the miss it is.
        if (!mReadFailureLogged.exchange(true, std::memory_order_relaxed))
            Log(Debug::Warning) << "The content store could not read " << Files::pathToUnicodeString(entry.mPath)
                                << ", and counts every miss after it: " << read.error().message();
        return RangeRead::Failed;
    }

    bool Store::holds(const Key& key, const std::uint32_t kind)
    {
        assert(kind < mPolicy.mKindCount && "a kind the store's owner does not have");

        bool found = false;
        const std::filesystem::path path = pathOf(key, kind);
        if (const std::optional<TailRead> tail = readTrailer(path))
        {
            std::string_view failed = checkTrailer(tail->mTrailer, key, tail->mBytes, mPolicy.mKindCount);
            if (failed.empty() && tail->mTrailer.mKind != kind)
                failed = "the kind asked for";
            found = failed.empty();
            if (!found)
                refuse(path, failed);
        }

        mMonitor.under([&] { ++(found ? mStats.mHeld : mStats.mUnheld); });
        return found;
    }

    void Store::write(Filed entry)
    {
        assert(entry.mKind < mPolicy.mKindCount && "a kind the store's owner does not have");
        assert(entry.mPayload.size() <= sPayloadMost && "an entry past what a stored file may hold");
        assert(entry.mSegmentEnds.size() < sSegmentsMost && "an entry of more segments than a file may hold");
        assert(std::ranges::is_sorted(entry.mSegmentEnds) && "segments whose ends fall");
        assert((entry.mSegmentEnds.empty() || entry.mSegmentEnds.back() == entry.mPayload.size())
            && "segments that do not end where the payload does");

        // **Room in the queue, or an empty queue**: one payload past the bound still goes alone, and the writer waits
        // for room otherwise.
        const std::size_t bytes = entry.mPayload.size();
        const std::size_t most = mPolicy.mQueuedBytesMost;
        const auto fits = [&] { return mQueuedBytes == 0 || bytes <= most - std::min(mQueuedBytes, most); };
        while (true)
        {
            const bool queued = mMonitor.give([&] {
                if (!fits())
                    return false;
                mQueuedBytes += bytes;
                mQueue.emplace_back(std::move(entry));
                return true;
            });
            if (queued)
                return;

            // No worker left to make room, as once the store is stopping: dropped, where a wait would spin for ever.
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
            mQueue.emplace_back(DiscardTask{ .mKey = key, .mKind = kind });
            return true;
        });
    }

    void Store::flush()
    {
        mMonitor.await([&] { return mQueue.empty() && mInFlight == 0; });
    }

    std::size_t Store::flushFor(const std::chrono::milliseconds patience)
    {
        std::size_t left = 0;
        const Misc::Monitor::Awaited awaited = mMonitor.awaitFor(patience, [&] {
            left = mQueue.size() + static_cast<std::size_t>(mInFlight);
            return left == 0;
        });
        return awaited == Misc::Monitor::Awaited::StillWaiting ? left : 0;
    }

    Stats Store::getStats()
    {
        return mMonitor.under([&] { return mStats; });
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
                else
                    refuse(pathOf(one.mKey, one.mKind), "a payload its owner could not read");
            },
            task);
    }

    void Store::start()
    {
        // **The entries of versions no owner makes swept once after the versions change**, the marker beside the kinds
        // saying which were last swept: every entry's trailer read then, and none at a start after.
        const std::filesystem::path marker = mDirectory / "versions";
        const std::uint64_t signature = signatureOf(mPolicy.mVersions);
        Misc::ByteBuffer read;
        const bool sweeps = !mPolicy.mVersions.empty()
            && (!Platform::readWhole(marker, read, sizeof(signature)).isOk() || read.size() != sizeof(signature)
                || std::memcmp(read.data(), &signature, sizeof(signature)) != 0);
        assert((mPolicy.mVersions.empty() || mPolicy.mVersions.size() == mPolicy.mKindCount)
            && "a version for every kind, or none");

        Swept swept;
        for (std::uint32_t kind = 0; kind < mPolicy.mKindCount; ++kind)
            sweepFolder(mDirectory / std::to_string(kind),
                sweeps ? std::optional(mPolicy.mVersions[kind]) : std::nullopt, swept);
        if (sweeps)
        {
            std::error_code made;
            std::filesystem::create_directories(mDirectory, made);
            std::ignore
                = Platform::writeWhole(marker, std::as_bytes(std::span(&signature, 1)), Platform::Durability::Whole);
        }

        mMonitor.under([&] {
            mStats.mStale += swept.mStale;
            mStats.mOutdated += swept.mOutdated;
        });
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
        // **What the delete answered is read**: a file that stays is refused again by the next read that reaches it,
        // and a delete the system failed is said once, beside the refusal.
        std::error_code failed;
        const bool deleted = std::filesystem::remove(path, failed);
        mMonitor.under([&] { ++mStats.mRefused; });
        if (!mRefusalLogged.exchange(true, std::memory_order_relaxed))
            Log(Debug::Warning) << "The content store refused " << Files::pathToUnicodeString(path) << " on " << check
                                << (deleted ? ", deleted it" : ", could not delete it: " + failed.message())
                                << ", and counts every refusal after it";
    }

    void Store::writeFile(const Filed& task)
    {
        const std::filesystem::path path = pathOf(task.mKey, task.mKind);
        std::error_code failed;

        // A key whose file stands is not written again: a key is its entry's whole input, so the file holds these
        // bytes. A file a fault left is refused by the next read that reaches it, and the next write makes it.
        if (std::filesystem::exists(path, failed))
            return;

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
}
