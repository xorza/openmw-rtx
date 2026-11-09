#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <span>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <components/misc/bytebuffer.hpp>
#include <components/rtx/common/monitor.hpp>
#include <components/rtx/common/worker.hpp>

#include "contentkey.hpp"
#include "contentpass.hpp"

namespace Rtx
{
    /// The folder a store stands in under the user's cache directory, the game's and the harness's
    /// both, so a measured run reads what the game wrote.
    inline constexpr std::string_view sContentStoreFolder = "rtx-content";

    /// A key's hash for the store's tables: its first word, since a key is a hash already.
    struct ContentKeyHash
    {
        std::size_t operator()(const ContentKey& key) const noexcept { return static_cast<std::size_t>(key.mHash[0]); }
    };

    /// What a store did since it was made, for a report and a test.
    struct ContentStoreStats
    {
        /// Entries read whole, from the memory the store holds or from their files.
        std::uint64_t mFound = 0;

        /// Asks no entry answered: no file, or one this refused.
        std::uint64_t mMissed = 0;

        /// Files refused and deleted: a wrong trailer, length or digest, or a payload its pass could not
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
    enum class StoreReach
    {
        Memory,
        Disk,
    };

    /// The content's derived data, kept on disk across sessions: one file per entry under its key, in
    /// 256 folders named by the key's first byte, as ccache and Bazel's disk cache keep theirs
    /// (`docs/rtx/architecture.md`, "One file an entry, and every entry untrusted"). **Every file is
    /// untrusted data**: its own trailer states the pass, its version, its key and its payload's length
    /// and digest, and a file that does not match is a miss and is deleted. **No cap, by the user's
    /// decision**: an entry stays until its file is refused or deleted by hand; each file's time, which
    /// a hit sets once a session, ranks what the start loads into memory. **Nothing here fails loudly**: a
    /// directory that cannot be made or written is a store that misses.
    ///
    /// One for a process, shared by every thread's cache. Reads run on the asking thread; writes,
    /// stamps, the start's sweep and the loading of the small entries into memory run on a thread of
    /// the store's own, behind a queue bounded in bytes. Pinned: the thread reaches every member.
    class ContentStore
    {
    public:
        /// The most bytes of payload the queue holds, past which a write waits or is dropped (`write`). Tens of
        /// megabytes against the largest entry the passes make, so a burst of misses at a cell's arrival queues whole.
        static constexpr std::size_t sQueuedMost = std::size_t{ 64 } << 20;

        /// The longest payload a file may say it holds before it is refused unread: past any pass's
        /// output, so a trailer a fault wrote cannot ask for gigabytes.
        static constexpr std::uint64_t sPayloadMost = std::uint64_t{ 1 } << 30;

        /// The largest payload held in memory once read or written, and loaded at start, but a shape's,
        /// held at any size: a fact or a shape, whose file read on the frame's thread would be a wait on
        /// the disk, and a shape the frame would otherwise compute again.
        static constexpr std::size_t sResidentMost = 4096;

        /// The most bytes held in memory, each entry's payload and its node in the table, past which an
        /// entry is read from its file again: tens of thousands of facts and the shapes a few cells
        /// read, with room to spare.
        static constexpr std::size_t sResidentBytesMost = std::size_t{ 64 } << 20;

        /// The most keys a session remembers stamping, past which a hit stamps its file again: ten times
        /// the files a large content list's store holds, its writes included.
        static constexpr std::size_t sStampedMost = std::size_t{ 1 } << 20;

        /// The file format's version, in every trailer; a file of another is a miss.
        static constexpr std::uint32_t sFormat = 2;

        /// @param directory where the store stands, made if it is not there.
        explicit ContentStore(std::filesystem::path directory);

        /// Finishes every write and stamp handed so far, then stops its thread.
        ~ContentStore();

        ContentStore(const ContentStore&) = delete;
        ContentStore& operator=(const ContentStore&) = delete;
        ContentStore(ContentStore&&) = delete;
        ContentStore& operator=(ContentStore&&) = delete;

        /// The entry `key` filed for `pass` at `version`, into `payload`, answering whether a whole
        /// one was there within `reach`. A refused file is deleted.
        bool read(const ContentKey& key, ContentPassId pass, std::uint32_t version, StoreReach reach,
            Misc::ByteBuffer& payload);

        /// Whether an entry stands under `key` within `reach`, held in memory or as a file, with nothing
        /// of it read: one `stat`, and the session's stamp queued as a hit queues it. The file is not
        /// checked, which its reader does: what a preparation asks, which needs the entry stored and
        /// not its bytes.
        bool holds(const ContentKey& key, StoreReach reach);

        /// Files `payload` under `key` for `pass` at `version`, on the store's thread, once the queue has
        /// room for it, `sQueuedMost`, or is empty: a writer that reaches the disk waits for room, and one
        /// that reaches memory alone drops it, which a later ask answers as a miss.
        void write(const ContentKey& key, ContentPassId pass, std::uint32_t version, Misc::ByteBuffer payload,
            StoreReach reach);

        /// Deletes the entry `key` filed, whose payload its pass could not read, on the store's thread:
        /// a write of the output run in its place finds no file and writes it.
        void discard(const ContentKey& key);

        /// Waits until every write, stamp and the start's sweep handed so far are done.
        void flush();

        const std::filesystem::path& getDirectory() const { return mDirectory; }

        ContentStoreStats getStats();

        /// Where the entry `key` stands.
        std::filesystem::path pathOf(const ContentKey& key) const;

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
            ContentKey mKey{};
            ContentPassId mPass = ContentPassId::Count;
            std::uint32_t mVersion = 0;
            Misc::ByteBuffer mPayload{};
        };

        /// What an entry is: its pass and version, which an ask holds against its own, and its
        /// payload.
        struct Resident
        {
            ContentPassId mPass;
            std::uint32_t mVersion;
            Misc::ByteBuffer mPayload;
        };

        void serve(const Platform::StopToken& stop);
        void run(Task& task);
        /// The start's work: the sweep of stale temporaries, and the small entries loaded into memory.
        void start();
        void writeFile(const Task& task);
        void stampFile(const ContentKey& key);

        /// Reads the file at `path` for `key` into `pass`, `version` and `payload`, answering whether
        /// it was whole; refuses and deletes it where it was not. Nothing where there is no file.
        bool readFile(const std::filesystem::path& path, const ContentKey& key, ContentPassId& pass,
            std::uint32_t& version, Misc::ByteBuffer& payload);

        /// What an entry of `payloadBytes` costs `mResidentBytes`: its payload and its node.
        static std::size_t residentBytesOf(std::size_t payloadBytes);

        /// Whether a hit on `key` stamps its file now: true the first time this session, which notes the
        /// key, and false after it; true every time for a key met once `sStampedMost` keys are noted,
        /// since no more are remembered. Under the lock.
        bool stampsOnce(const ContentKey& key);

        /// Holds `payload` in memory under `key` where it is small enough or a shape (`sResidentMost`).
        /// Under the lock.
        void keepResident(
            const ContentKey& key, ContentPassId pass, std::uint32_t version, std::span<const std::byte> payload);

        void refuse(const std::filesystem::path& path);

        const std::filesystem::path mDirectory;

        /// What the store's thread and every asking thread share, under `mMonitor`.
        Monitor mMonitor;
        std::deque<Task> mQueue;
        std::size_t mQueuedBytes = 0;
        int mInFlight = 0;
        std::unordered_set<ContentKey, ContentKeyHash> mStamped;
        std::unordered_map<ContentKey, Resident, ContentKeyHash> mResident;
        std::size_t mResidentBytes = 0;
        ContentStoreStats mStats;

        /// Whether a failed write was logged: the first is, and the rest are counted (`mDropped`), as a
        /// full disk fails every write after it. The store's thread alone.
        bool mWriteFailureLogged = false;

        /// Last, for the reason `Worker` gives.
        Worker mWorker;
    };
}
