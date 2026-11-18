#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <vector>

namespace Resource
{
    /// A cache a `CacheBudget` holds to its bytes: it answers for its least recently used entry nothing outside it
    /// holds, and lets that entry go when told.
    class BudgetedCache
    {
    public:
        /// The use stamp of the least recently used entry nothing outside the cache holds, or nothing where every
        /// entry is held. Each held entry it passes on the way is in use, and moves to the front as just used.
        virtual std::optional<std::uint64_t> oldestUnheld() = 0;

        /// Lets go of the least recently used entry, where its stamp is still `used` and nothing holds it, and
        /// answers whether it did: another thread may have used it since `oldestUnheld` named it. Its value is kept
        /// until `destroyEvicted`, so whichever thread evicts destroys nothing.
        virtual bool evictOldest(std::uint64_t used) = 0;

        /// Destroys the values evicted since the last call, outside the cache's lock: the deriving worker's work
        /// (`CacheBudget::makeRoom`), never an inserting thread's, the frame's among them.
        virtual void destroyEvicted() = 0;

        BudgetedCache(const BudgetedCache&) = delete;
        BudgetedCache& operator=(const BudgetedCache&) = delete;

    protected:
        BudgetedCache() = default;
        ~BudgetedCache() = default;
    };

    /// **The bytes the caches of what is read from files may keep between them**, and one least-recently-used
    /// order over all of them (`docs/architecture.md`, "Loading"): an entry anything outside its cache holds
    /// stays, and one nothing holds stays until the caches' bytes pass the budget, when whatever was used longest
    /// ago goes first, whichever cache it is in. The thread whose insertion passed the budget evicts as many bytes
    /// as it added, so no frame absorbs a whole cache's eviction, and the worker that derives the budget destroys
    /// what went, so no frame absorbs a destruction either.
    class CacheBudget
    {
    public:
        /// The memory the system has left that the caches leave alone, as the content preparation keeps 1 GiB: room
        /// for what the rest of the game takes in the second before the budget is derived again.
        static constexpr std::uint64_t sReserveBytes = std::uint64_t{ 1 } << 30;

        /// A budget nothing passes, which keeps everything: where the system states no memory, and for a test.
        static constexpr std::uint64_t sUnbounded = std::numeric_limits<std::uint64_t>::max();

        /// What the caches may hold, where they hold `held`, the system leaves the process `available` beside it, and
        /// the process's heap keeps `heapFree` it took and holds free: all three, less `sReserveBytes`; `sUnbounded`
        /// where the system states no memory. The heap's free memory is the process's own to take again, which the
        /// system counts as taken. Derived again as the system's memory moves, so whatever else takes it — the rest
        /// of the process, a device that shares the system's memory, another program — leaves the caches that much
        /// less.
        static std::uint64_t derive(std::optional<std::uint64_t> available, std::uint64_t held, std::uint64_t heapFree);

        explicit CacheBudget(std::uint64_t bytes);
        ~CacheBudget();

        CacheBudget(const CacheBudget&) = delete;
        CacheBudget& operator=(const CacheBudget&) = delete;

        /// `cache` is held to this budget until it `leave`s, which it does before it is destroyed.
        void join(BudgetedCache& cache);
        void leave(BudgetedCache& cache);

        /// The next use stamp, later than every stamp before it.
        std::uint64_t stamp() { return mStamp.fetch_add(1, std::memory_order_relaxed) + 1; }

        /// A cache now holds `bytes` more. Counted under the cache's own lock in the step that puts the entry in, as
        /// `remove` is in the step that takes it out, so the budget never hears of an entry's going before its
        /// coming.
        void add(std::uint64_t bytes) { mHeld.fetch_add(bytes, std::memory_order_relaxed); }

        /// A cache let `bytes` go.
        void remove(std::uint64_t bytes);

        /// Evicts the least recently used entries nothing holds while the caches hold more than the budget: what
        /// the thread that put an entry in calls once it has let go of its cache's lock. Nothing where the last
        /// look found everything in use and no `makeRoom` has come since.
        void makeRoomForInsertion();

        /// Looks for room where the caches stand past the budget, lets the next insertion look again, and destroys what
        /// every eviction since the last let go of: called once the caches have updated, when what they held may have
        /// been let go, by the worker that derives the budget. Outside every cache's lock.
        void makeRoom();

        /// What the caches hold, held or not.
        std::uint64_t getHeld() const { return mHeld.load(std::memory_order_relaxed); }

        /// Whether the caches stand within the budget: what a guess at what is needed next, a preload, is made
        /// against. Past it, everything they hold is in use and an eviction found nothing to let go.
        bool hasRoom() const { return getHeld() < getBytes(); }

        std::uint64_t getBytes() const { return mBytes.load(std::memory_order_relaxed); }

        /// How many looks for room found everything in use past the budget: a run's measure of how often its
        /// content wanted more than the budget, which on a machine short of memory is the expected state and not a
        /// fault to log.
        std::size_t getStarvedLooks() const { return mStarvedLooks.load(std::memory_order_relaxed); }

        /// The budget the system's memory derives (`derive`), from which the device's claim and the limit are taken.
        /// Makes room at once.
        void setBytes(std::uint64_t bytes);

        /// Holds the budget to `bytes` where the system would leave more, or lets it follow the system again: what a
        /// run that asks how a machine short of memory loads does (`MWRender::RunSetup::mCacheBudget`). Makes room at
        /// once.
        void limit(std::optional<std::uint64_t> bytes);

        /// **What a device whose memory is the system's claims of the budget**: memory it gave back lately and is
        /// likely to take again, which the caches leave it (`docs/architecture.md`, "On a unified device the device
        /// and the caches split the system's memory"). Without it every block the device gave back was the caches'
        /// the next second, and the device's room fell after it. Told once a frame by the renderer; the room it makes
        /// is made by the next look (`makeRoom`, `makeRoomForInsertion`), never on the frame.
        void yieldTo(std::uint64_t claim);

    private:
        void evictToBudget();

        std::atomic<std::uint64_t> mHeld = 0;
        std::atomic<std::uint64_t> mStamp = 0;
        std::atomic<std::uint64_t> mBytes;
        std::atomic<std::uint64_t> mDerived;
        std::atomic<std::uint64_t> mLimit = sUnbounded;
        std::atomic<std::uint64_t> mClaim = 0;

        /// Sets the budget from what the system derives, less the device's claim, held to the limit.
        void refresh();

        /// Over the members and the evictions, so that two threads past the budget do not both evict for one
        /// insertion. Taken before a member's own lock, never after.
        std::mutex mMutex;
        std::vector<BudgetedCache*> mCaches;

        /// Whether the last eviction found everything in use past the budget. Until the next `makeRoom`, an
        /// insertion does not look again: a look walks every entry of every cache, and on a machine whose content is
        /// all in use past the budget each insertion would.
        std::atomic<bool> mStarved = false;
        std::atomic<std::size_t> mStarvedLooks = 0;
    };
}
