#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include <components/misc/bytebuffer.hpp>
#include <components/misc/digestwords.hpp>
#include <components/misc/monitor.hpp>
#include <components/platform/file.hpp>
#include <components/platform/thread.hpp>

#include "key.hpp"

namespace ContentStore
{
    /// What an owner files in a store, stated once as it opens it: the kinds its entries are, `[0, mKindCount)`, a
    /// file of another being refused.
    struct Policy
    {
        std::uint32_t mKindCount{};

        /// **Each kind's version its owner makes now**, by kind, or none for an owner whose entries never outlive a
        /// version: an entry of another version is one no ask reaches again, swept by the start after the versions
        /// last swept changed (`Stats::mOutdated`), a marker beside the kinds holding what that was.
        std::span<const std::uint32_t> mVersions{};

        /// The most bytes of payload the queue holds, past which a write waits (`Store::write`). Tens of megabytes
        /// against the largest entry an owner makes, so a burst of misses at a cell's arrival queues whole.
        std::size_t mQueuedBytesMost = std::size_t{ 64 } << 20;
    };

    /// What a store did since it was made, for a report and a test.
    struct Stats
    {
        /// Entries read whole, or opened, from their files.
        std::uint64_t mFound = 0;

        /// Asks no entry answered: no file, or one this refused.
        std::uint64_t mMissed = 0;

        /// Entries `holds` found standing, and asks it found none for: apart from the reads, as it reads
        /// no payload.
        std::uint64_t mHeld = 0;
        std::uint64_t mUnheld = 0;

        /// Files refused and deleted: a wrong trailer, length or digest, or a payload its owner could not
        /// read (`discard`).
        std::uint64_t mRefused = 0;

        /// Entries written, and writes dropped because the write failed.
        std::uint64_t mWritten = 0;
        std::uint64_t mDropped = 0;

        /// Temporary files the start's sweep removed that a killed process left.
        std::uint64_t mStale = 0;

        /// Entries of a version their owner makes no more, removed by a start's sweep (`Policy::mVersions`).
        std::uint64_t mOutdated = 0;
    };

    /// An entry `write` files: its key, its kind and its version, its payload, and where the payload's segments end,
    /// each kept with a digest of its own, so a range of them reads and checks alone, as KTX 2 indexes its levels: a
    /// made chain's levels. No end for one segment, the whole payload.
    struct Filed
    {
        Key mKey;
        std::uint32_t mKind = 0;
        std::uint32_t mVersion = 0;
        Misc::ByteBuffer mPayload;

        /// Each segment's end, rising, the last the payload's length.
        std::vector<std::uint64_t> mSegmentEnds;
    };

    /// The most segments an entry holds: a chain's levels to a texel at the largest side a slot holds, and the form's
    /// byte, with room past them. A trailer that states more is refused unread.
    inline constexpr std::uint32_t sSegmentsMost = 64;

    /// An entry opened for its segments to be read alone (`Store::readSegments`): its table and its file. Moved, never
    /// copied.
    class Opened
    {
    public:
        std::uint32_t getSegmentCount() const { return mSegments; }

        /// Where segment `segment` starts and ends in the payload; the payload's length is the last's end.
        std::uint64_t startOf(std::uint32_t segment) const { return segment == 0 ? 0 : mEnds[segment - 1]; }
        std::uint64_t endOf(std::uint32_t segment) const { return mEnds[segment]; }

    private:
        friend class Store;

        std::filesystem::path mPath;
        Platform::File::ScopedHandle mFile;
        std::uint32_t mSegments = 0;
        std::array<std::uint64_t, sSegmentsMost> mEnds{};
        std::array<Misc::DigestWords, sSegmentsMost> mDigests{};
    };

    /// What is derived from the content, kept on disk across sessions: one file per entry under its key, in a folder
    /// of its kind and in 256 folders under it named by the key's first byte, as ccache and Bazel's disk cache keep
    /// theirs (`docs/architecture.md`, "One file an entry, and every entry untrusted"). The kind's folder lets the
    /// start load a kind's entries and pass another's by without opening a file. Every file is untrusted data: its
    /// own trailer states its kind, its version, its key, its payload's length and its segments' table, each segment
    /// with its digest, and a file that does not match is a miss and is deleted. No cap, by the user's decision: an
    /// entry stays until its file is refused or deleted by hand, or its version is one its owner makes no more.
    /// **Nothing held in memory**: what a frame needs of the content its owner keeps where it is used, as a geometry
    /// keeps its fold (`Rtx::ModelFold`) and a header its facts, so every ask reaches the disk and none comes from the
    /// frame's thread. Nothing here fails loudly: a directory that cannot be made or written is a store that misses.
    ///
    /// One for an owner's folder in a process, shared by every thread that asks it. Reads run on the asking thread;
    /// writes and the start's sweep run on a thread of the store's own, behind a queue bounded in bytes. Pinned: the
    /// thread reaches every member.
    class Store
    {
    public:
        /// The longest payload a file may say it holds before it is refused unread: past any owner's
        /// entry, so a trailer a fault wrote cannot ask for gigabytes.
        static constexpr std::uint64_t sPayloadMost = std::uint64_t{ 1 } << 30;

        /// The file format's version, in every trailer, and **the folder of the store's that holds its
        /// files** (`v3`): two builds of different formats on one machine keep apart, and neither deletes
        /// the other's entries as files it cannot read.
        static constexpr std::uint32_t sFormat = 3;

        /// @param directory where the store stands, made if it is not there; its files are in the folder
        ///        of its format under it.
        Store(const std::filesystem::path& directory, const Policy& policy);

        /// Finishes every write handed so far, then stops its thread.
        ~Store();

        Store(const Store&) = delete;
        Store& operator=(const Store&) = delete;

        /// The entry `key` filed as `kind` at `version`, into `payload`, answering whether a whole one was there. A
        /// refused file is deleted.
        bool read(const Key& key, std::uint32_t kind, std::uint32_t version, Misc::ByteBuffer& payload);

        /// The entry `key` filed as `kind` at `version`, opened for its segments to be read alone, its payload unread:
        /// nothing where none stands, its trailer or its table does not hold, which refuses and deletes the file, or it
        /// names another kind or version. A hit, as a read is.
        std::optional<Opened> open(const Key& key, std::uint32_t kind, std::uint32_t version);

        /// Reads `count` segments of `entry` from `first` into `into`, as long as they are, each checked against its
        /// digest: false where the file is shorter than its segments or a digest does not hold, which refuses and
        /// deletes it, and where the system failed the read, which leaves it; `into` is then unspecified.
        bool readSegments(Opened& entry, std::uint32_t first, std::uint32_t count, std::span<std::byte> into);

        /// Whether an entry stands under `key` as a file whose trailer holds for its size, its payload unread: what a
        /// preparation asks, which needs the entry stored and not its bytes. A file cut short or of another's trailer
        /// is refused and deleted, as a read refuses it; one whose payload a fault changed is found by the read's
        /// digest.
        bool holds(const Key& key, std::uint32_t kind);

        /// Files `entry` on the store's thread, once the queue has room for its payload, `Policy::mQueuedBytesMost`,
        /// or is empty: the writer waits for room, as no writer is the frame's thread.
        void write(Filed entry);

        /// Deletes the entry `key` filed as `kind`, whose payload its owner could not read, on the store's thread:
        /// a write of the entry made again in its place finds no file and writes it.
        void discard(const Key& key, std::uint32_t kind);

        /// Waits until every write and the start's sweep handed so far are done.
        void flush();

        /// `flush` for `patience` at most, answering how many of them are still to do, nought once none is or none
        /// can be done: what a start waits out under its loading screen, where a sweep after the versions moved
        /// reads every entry's trailer, thousands of them.
        std::size_t flushFor(std::chrono::milliseconds patience);

        /// The folder the store's files are in: its format's under the directory it was made at.
        const std::filesystem::path& getDirectory() const { return mDirectory; }

        Stats getStats();

        /// Where the entry `key` of `kind` stands.
        std::filesystem::path pathOf(const Key& key, std::uint32_t kind) const;

    private:
        /// What the store's thread is handed: the start's sweep, an entry to write, or a file to refuse.
        struct StartTask
        {
        };

        struct DiscardTask
        {
            Key mKey;
            std::uint32_t mKind = 0;
        };

        using Task = std::variant<StartTask, Filed, DiscardTask>;

        void serve(const std::stop_token& stop);
        void run(Task& task);
        /// The start's work: the sweep of stale temporaries, and of the entries of versions no owner makes.
        void start();
        void writeFile(const Filed& task);

        /// Opens `opened`'s file, which stands, for `key` filed as `kind` at `version`, and reads its trailer and its
        /// table; refuses and deletes it where either does not hold. A file the system will not open or read is a
        /// miss, and stays.
        bool openFile(Opened& opened, const Key& key, std::uint32_t kind, std::uint32_t version);

        /// What a read of a range of an entry's file came to.
        enum class RangeRead : std::uint8_t
        {
            Whole,

            /// The file ended first: an entry cut short, which is refused.
            Short,

            /// The system failed the read: a miss, said once a session.
            Failed,
        };

        /// Reads `into`'s length of `entry`'s file at `offset`.
        RangeRead readRange(const Opened& entry, std::uint64_t offset, std::span<std::byte> into);

        /// Reads the file at `path` for `key` into `kind`, `version` and `payload`, answering whether
        /// it was whole; refuses and deletes it where it was not. Nothing where there is no file.
        bool readFile(const std::filesystem::path& path, const Key& key, std::uint32_t& kind, std::uint32_t& version,
            Misc::ByteBuffer& payload);

        /// Deletes the file at `path`, which failed `check`, and says so once a session.
        void refuse(const std::filesystem::path& path, std::string_view check);

        const std::filesystem::path mDirectory;
        const Policy mPolicy;

        /// What the store's thread and every asking thread share, under `mMonitor`.
        Misc::Monitor mMonitor;
        std::deque<Task> mQueue;
        std::size_t mQueuedBytes = 0;
        std::int32_t mInFlight = 0;
        Stats mStats;

        /// Whether a failed write was logged: the first is, and the rest are counted (`mDropped`), as a
        /// full disk fails every write after it. The store's thread alone.
        bool mWriteFailureLogged = false;

        /// Whether a refused file, or a read that failed other than for its size, was logged: the first
        /// of each is, and the rest counted (`mRefused`, `mMissed`). Any thread, so atomic.
        std::atomic<bool> mRefusalLogged{ false };
        std::atomic<bool> mReadFailureLogged{ false };

        /// Last, so the thread is joined before the members it reads go.
        std::jthread mWorker;
    };
}
