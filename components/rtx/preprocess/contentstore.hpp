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
        std::size_t operator()(const ContentKey& key) const { return static_cast<std::size_t>(key.mHash[0]); }
    };

    /// What a store did since it was made, for a report and a test.
    struct ContentStoreStats
    {
        /// Entries read whole, from the memory the store holds or from their files.
        std::uint64_t mFound = 0;

        /// Asks no entry answered: no file, or one this refused.
        std::uint64_t mMissed = 0;

        /// Files refused and deleted: a wrong header, length or digest, or a payload its pass could not
        /// read (`discard`).
        std::uint64_t mRefused = 0;

        /// Entries written, and writes dropped because the queue held its bound or the write failed.
        std::uint64_t mWritten = 0;
        std::uint64_t mDropped = 0;

        /// Entries whose file had its time set for this session's use: by a hit, or by a write of a key
        /// whose file stood.
        std::uint64_t mStamped = 0;

        /// Entries a sweep removed to bring the store under its cap, at the start or past the cap in
        /// a session, and temporary files a sweep removed that a killed process left.
        std::uint64_t mEvicted = 0;
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
    /// (`.notes/content-cache.md`, the store's decisions). **Every file is untrusted data**: its own
    /// header states the pass, its version, its key and its payload's length and digest, and a file
    /// that does not match is a miss and is deleted. **Bounded by a cap, the least recently used
    /// leaving first**, by each file's time, which a hit sets once a session: at the start, and
    /// again whenever a session's writes pass it. **Nothing here fails
    /// loudly**: a directory that cannot be made or written is a store that misses.
    ///
    /// One for a process, shared by every thread's cache. Reads run on the asking thread; writes,
    /// stamps, the start's sweep and the loading of the small entries into memory run on a thread of
    /// the store's own, behind a queue bounded in bytes. Pinned: the thread reaches every member.
    class ContentStore
    {
    public:
        /// The cap a game's store keeps to, by the user's decision.
        static constexpr std::uint64_t sGameCap = std::uint64_t{ 16 } << 30;

        /// The most bytes of payload the queue holds; a write past it is dropped. Tens of megabytes
        /// against the largest entry the passes make, so a burst of misses at a cell's arrival
        /// queues whole.
        static constexpr std::size_t sQueuedMost = std::size_t{ 64 } << 20;

        /// The longest payload a file may say it holds before it is refused unread: past any pass's
        /// output, so a header a fault wrote cannot ask for gigabytes.
        static constexpr std::uint64_t sPayloadMost = std::uint64_t{ 1 } << 30;

        /// The largest payload held in memory once read or written, and loaded at start: a fact,
        /// whose file read on the frame's thread would be a wait on the disk.
        static constexpr std::size_t sResidentMost = 4096;

        /// The most bytes of payload held in memory, past which a small entry is read from its file
        /// again: tens of thousands of facts of a few dozen bytes, with room to spare.
        static constexpr std::size_t sResidentBytesMost = std::size_t{ 64 } << 20;

        /// How long a temporary file may stand before the sweep takes it for one a killed process
        /// left: well past any write's length, so no live write is swept.
        static constexpr std::chrono::hours sStaleAfter{ 24 };

        /// The file format's version, in every header; a file of another is a miss.
        static constexpr std::uint32_t sFormat = 1;

        /// @param directory where the store stands, made if it is not there.
        /// @param cap the most bytes of files it keeps after the start's sweep.
        ContentStore(std::filesystem::path directory, std::uint64_t cap);

        /// Finishes every write and stamp handed so far, then stops its thread.
        ~ContentStore();

        ContentStore(const ContentStore&) = delete;
        ContentStore& operator=(const ContentStore&) = delete;
        ContentStore(ContentStore&&) = delete;
        ContentStore& operator=(ContentStore&&) = delete;

        /// The entry `key` filed for `pass` at `version`, into `payload`, answering whether a whole
        /// one was there within `reach`. A refused file is deleted.
        bool read(const ContentKey& key, ContentPassId pass, std::uint32_t version, StoreReach reach,
            std::vector<std::byte>& payload);

        /// Files `payload` under `key` for `pass` at `version`, on the store's thread. Dropped where
        /// the queue holds `sQueuedMost`, which a later ask answers as a miss.
        void write(
            const ContentKey& key, ContentPassId pass, std::uint32_t version, std::span<const std::byte> payload);

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
            std::vector<std::byte> mPayload{};
        };

        /// What an entry is: its pass and version, which an ask holds against its own, and its
        /// payload.
        struct Resident
        {
            ContentPassId mPass;
            std::uint32_t mVersion;
            std::vector<std::byte> mPayload;
        };

        void serve(const Platform::StopToken& stop);
        void run(Task& task);
        /// The start's work: the sweep to the cap, and the small entries loaded into memory.
        void start();
        void writeFile(const Task& task);
        void stampFile(const ContentKey& key);

        /// Reads the file at `path` for `key` into `pass`, `version` and `payload`, answering whether
        /// it was whole; refuses and deletes it where it was not. Nothing where there is no file.
        bool readFile(const std::filesystem::path& path, const ContentKey& key, ContentPassId& pass,
            std::uint32_t& version, std::vector<std::byte>& payload);

        /// Holds `payload` in memory under `key` where it is small enough (`sResidentMost`).
        /// Under the lock.
        void keepResident(
            const ContentKey& key, ContentPassId pass, std::uint32_t version, std::span<const std::byte> payload);

        void refuse(const std::filesystem::path& path);

        const std::filesystem::path mDirectory;
        const std::uint64_t mCap;

        /// What the store's thread and every asking thread share, under `mMonitor`.
        Monitor mMonitor;
        std::deque<Task> mQueue;
        std::size_t mQueuedBytes = 0;
        int mInFlight = 0;
        std::unordered_set<ContentKey, ContentKeyHash> mStamped;
        std::unordered_map<ContentKey, Resident, ContentKeyHash> mResident;
        std::size_t mResidentBytes = 0;
        ContentStoreStats mStats;

        /// How many temporary files the store's thread has named, and how many bytes of entries it
        /// counts in the folder: what it alone reads.
        std::uint64_t mPartials = 0;
        std::uint64_t mStoredBytes = 0;

        /// Last, for the reason `Worker` gives.
        Worker mWorker;
    };
}
