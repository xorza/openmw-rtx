#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <components/misc/bytebuffer.hpp>
#include <components/misc/monitor.hpp>
#include <components/platform/thread.hpp>

#include "key.hpp"

namespace ContentStore
{
    /// What a store holds in memory: its small entries (`Store::sResidentMost`), loaded at the start's sweep, and
    /// the kinds held at any size, a bit each, once a reader asks for them. A session's first hit stamps an entry,
    /// so an ask that reaches memory alone finds it. The start loads none of a kind held at any size: its owner
    /// reads the entries its content asks for, where a sweep that ranks files by their time alone would take the
    /// room of with another content's.
    struct Holding
    {
        std::uint64_t mAtAnySize = 0;
    };

    /// What an owner files in a store, stated once as it opens it: the kinds its entries are, `[0,
    /// mKindCount)`, a file of another being refused, and what the store holds in memory, or nothing, for
    /// an owner whose every ask reaches the disk, with nothing loaded and nothing stamped.
    struct Policy
    {
        std::uint32_t mKindCount;
        std::optional<Holding> mHolding;
    };

    /// What a store did since it was made, for a report and a test.
    struct Stats
    {
        /// Entries read whole, from the memory the store holds or from their files.
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

        /// Entries written, and writes dropped because the queue held its bound or the write failed.
        std::uint64_t mWritten = 0;
        std::uint64_t mDropped = 0;

        /// Entries whose file had its time set for this session's use: by a hit, or by a write of a key
        /// whose file stood.
        std::uint64_t mStamped = 0;

        /// Temporary files the start's sweep removed that a killed process left.
        std::uint64_t mStale = 0;
    };

    /// Where an ask may reach for an entry: the memory the store holds alone, which the frame's thread
    /// asks, since it waits on no disk; or that memory and then the entry's file, which a reader asks.
    enum class Reach
    {
        Memory,
        Disk,
    };

    /// What is derived from the content, kept on disk across sessions: one file per entry under its key, in 256
    /// folders named by the key's first byte, as ccache and Bazel's disk cache keep theirs (`docs/rtx/architecture.md`,
    /// "One file an entry, and every entry untrusted"). Every file is untrusted data: its own trailer states its
    /// kind, its version, its key and its payload's length and digest, and a file that does not match is a miss and
    /// is deleted. No cap, by the user's decision: an entry stays until its file is refused or deleted by hand; each
    /// file's time, which a hit sets once a session where the store holds entries in memory, ranks what the start
    /// loads. Nothing here fails loudly: a directory that cannot be made or written is a store that misses.
    ///
    /// One for an owner's folder in a process, shared by every thread that asks it. Reads run on the asking thread;
    /// writes, stamps, the start's sweep and the loading of the small entries into memory run on a thread of the
    /// store's own, behind a queue bounded in bytes. Pinned: the thread reaches every member.
    class Store
    {
    public:
        /// The most bytes of payload the queue holds, past which a write waits or is dropped (`write`). Tens of
        /// megabytes against the largest entry an owner makes, so a burst of misses at a cell's arrival queues whole.
        static constexpr std::size_t sQueuedMost = std::size_t{ 64 } << 20;

        /// The longest payload a file may say it holds before it is refused unread: past any owner's
        /// entry, so a trailer a fault wrote cannot ask for gigabytes.
        static constexpr std::uint64_t sPayloadMost = std::uint64_t{ 1 } << 30;

        /// The largest payload held in memory once read or written, and loaded at start, where the store
        /// holds entries, but a kind held at any size (`Holding::mAtAnySize`).
        static constexpr std::size_t sResidentMost = 4096;

        /// The most bytes held in memory, each entry's payload and its node in the table, past which an entry is read
        /// from its file again: the largest content measured, the Fullrest Repack, reaches 222 MiB of shapes on the
        /// frame and a store's small entries came to 87 MiB; twice their sum, for the larger lists to come.
        static constexpr std::size_t sResidentBytesMost = std::size_t{ 512 } << 20;

        /// The most keys a session remembers stamping, past which a hit stamps nothing: ten times the
        /// files a large content list's store holds, its writes included.
        static constexpr std::size_t sStampedMost = std::size_t{ 1 } << 20;

        /// The file format's version, in every trailer, and **the folder of the store's that holds its
        /// files** (`v2`): two builds of different formats on one machine keep apart, and neither deletes
        /// the other's entries as files it cannot read.
        static constexpr std::uint32_t sFormat = 2;

        /// @param directory where the store stands, made if it is not there; its files are in the folder
        ///        of its format under it.
        Store(const std::filesystem::path& directory, const Policy& policy);

        /// Finishes every write and stamp handed so far, then stops its thread.
        ~Store();

        Store(const Store&) = delete;
        Store& operator=(const Store&) = delete;
        Store(Store&&) = delete;
        Store& operator=(Store&&) = delete;

        /// The entry `key` filed as `kind` at `version`, into `payload`, answering whether a whole
        /// one was there within `reach`. A refused file is deleted.
        bool read(const Key& key, std::uint32_t kind, std::uint32_t version, Reach reach, Misc::ByteBuffer& payload);

        /// Whether an entry stands under `key` within `reach`, held in memory or as a file whose trailer holds for
        /// its size, with its payload unread, and the session's stamp queued as a hit queues it: what a preparation
        /// asks, which needs the entry stored and not its bytes. A file cut short or of another's trailer is refused
        /// and deleted, as a read refuses it; one whose payload a fault changed is found by the read's digest.
        bool holds(const Key& key, Reach reach);

        /// Files `payload` under `key` as `kind` at `version`, on the store's thread, once the queue has
        /// room for it, `sQueuedMost`, or is empty: a writer that reaches the disk waits for room, and one
        /// that reaches memory alone drops it, which a later ask answers as a miss.
        void write(const Key& key, std::uint32_t kind, std::uint32_t version, Misc::ByteBuffer payload, Reach reach);

        /// Deletes the entry `key` filed, whose payload its owner could not read, on the store's thread:
        /// a write of the entry made again in its place finds no file and writes it.
        void discard(const Key& key);

        /// Waits until every write, stamp and the start's sweep handed so far are done.
        void flush();

        /// The folder the store's files are in: its format's under the directory it was made at.
        const std::filesystem::path& getDirectory() const { return mDirectory; }

        Stats getStats();

        /// Where the entry `key` stands.
        std::filesystem::path pathOf(const Key& key) const;

    private:
        enum class TaskKind
        {
            Start,
            Write,
            Stamp,
            Discard,
        };

        struct Task
        {
            TaskKind mKind;
            Key mKey{};
            std::uint32_t mEntryKind = 0;
            std::uint32_t mVersion = 0;
            Misc::ByteBuffer mPayload{};
        };

        /// What an entry is: its kind and version, which an ask holds against its own, and its payload. Shared and
        /// never changed, so a reader takes it under the lock and copies it after, and an entry replaced or discarded
        /// meanwhile goes when its last reader lets it go.
        struct Resident
        {
            std::uint32_t mKind;
            std::uint32_t mVersion;
            std::shared_ptr<const Misc::ByteBuffer> mPayload;
        };

        void serve(const std::stop_token& stop);
        void run(Task& task);
        /// The start's work: the sweep of stale temporaries, and the small entries loaded into memory.
        void start();
        void writeFile(const Task& task);
        void stampFile(const Key& key);

        /// Reads the file at `path` for `key` into `kind`, `version` and `payload`, answering whether
        /// it was whole; refuses and deletes it where it was not. Nothing where there is no file.
        bool readFile(const std::filesystem::path& path, const Key& key, std::uint32_t& kind, std::uint32_t& version,
            Misc::ByteBuffer& payload);

        /// What an entry of `payloadBytes` costs `mResidentBytes`: its payload and its node.
        static std::size_t residentBytesOf(std::size_t payloadBytes);

        /// Whether a hit on `key` stamps its file now: true the first time this session, which notes the
        /// key, and false after it; false for a key met once `sStampedMost` keys are noted, since no
        /// more are remembered, and the time a stamp sets only ranks what a start loads. Under the lock.
        bool stampsOnce(const Key& key);

        /// Whether `kind` is held in memory at any size (`Holding::mAtAnySize`), where the store holds entries.
        bool isHeldAtAnySize(std::uint32_t kind) const;

        /// The copy of `payload` the store holds in memory where it holds entries (`Policy::mHolding`)
        /// and it is small enough (`sResidentMost`) or of a kind held at any size, or null. Made outside
        /// the lock, which `keepResident` takes it under.
        std::shared_ptr<const Misc::ByteBuffer> residentCopy(
            std::uint32_t kind, std::span<const std::byte> payload) const;

        /// Holds `payload`, a `residentCopy`, in memory under `key` where the bound has room. Under the
        /// lock.
        void keepResident(
            const Key& key, std::uint32_t kind, std::uint32_t version, std::shared_ptr<const Misc::ByteBuffer> payload);

        /// Deletes the file at `path`, which failed `check`, and says so once a session.
        void refuse(const std::filesystem::path& path, std::string_view check);

        const std::filesystem::path mDirectory;
        const Policy mPolicy;

        /// What the store's thread and every asking thread share, under `mMonitor`.
        Misc::Monitor mMonitor;
        std::deque<Task> mQueue;
        std::size_t mQueuedBytes = 0;
        int mInFlight = 0;
        std::unordered_set<Key, KeyHash> mStamped;
        std::unordered_map<Key, Resident, KeyHash> mResident;
        std::size_t mResidentBytes = 0;
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
