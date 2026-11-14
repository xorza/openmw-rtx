#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <volk.h>

#include <components/rtx/common/ownedby.hpp>
#include <components/rtxvulkan/device/memory/batch.hpp>
#include <components/rtxvulkan/device/memory/stagingring.hpp>

#include "owned.hpp"
#include "retiring.hpp"

namespace Rtx
{
    class Device;
    class LentCommands;
    class Recording;

    /// The one command pool, the device's own, and both ways a submit is made out of it: a load
    /// asks the queue and waits, and a frame cannot wait, because a frame that drained the queue
    /// could not hand the CPU the next one to walk.
    ///
    /// **The device's thread's alone**, asserted where a command buffer is taken, begun, deferred or
    /// submitted: reached through the `const Device&` the compile hands hold too, and a Vulkan pool is
    /// externally synchronized.
    class CommandPool
    {
    public:
        /// Records `record` into a fresh command buffer, submits it, and waits for the queue. For
        /// the one-off; anything that happens once per resource wants a `Batch`, or the queue is
        /// asked to do one thing three hundred times.
        template <class F>
        void submitAndWait(F&& record);

        /// `count` command buffers for a caller that records into them again and again — a ring's,
        /// a drawer's, a presenter's — given back when what this returns ends.
        LentCommands lend(std::uint32_t count);

        /// Begins one of them, one-shot like everything this pool hands out: a frame's recording, a
        /// placement's or a trace's, open until the `Recording` is submitted or ended. **No other
        /// submit is made while it is open**, which `submitWithDeferred` asserts.
        Recording begin(VkCommandBuffer commands);

        /// Takes a recorded batch to submit ahead of the next submit this pool makes — what lets an
        /// arrival ride the placement that follows it instead of costing a round trip of its own.
        /// Ends `commands`. What the batch read is the batch's to bury, under that same submit.
        void defer(VkCommandBuffer commands);

        /// Submits whatever was deferred and waits for it, for the two paths that take the pool
        /// apart — a resize and shutdown — which have no next submit to give a deferred batch.
        void finishDeferred();

        const Device& getDevice() const { return mDevice; }

        /// The device's staging, which a batch writes its uploads into.
        StagingRing& getStaging() { return mStaging; }
        const StagingRing& getStaging() const { return mStaging; }

    private:
        friend class Batch;
        friend class LentCommands;
        friend class Recording;

        /// A command buffer to record into: off the spare list where one has been given back, allocated
        /// where none has. The pool allows individual reset, so a buffer given back is begun again with
        /// `vkBeginCommandBuffer` alone and an arrival allocates nothing once the busiest frame has been
        /// seen. Nothing is freed until the pool goes.
        VkCommandBuffer take();

        /// Gives command buffers the queue has finished with back for the next `take`.
        void recycle(std::span<const VkCommandBuffer> commands);

        /// Gives command buffers back once the next submit has run: a submit before it may still
        /// carry them, and every one after it was recorded with them out of reach.
        void retire(std::span<const VkCommandBuffer> commands);

        /// `Recording::submit`'s: submits `commands` behind whatever was deferred and does not
        /// wait. Ends `commands`. The deferred batches' command buffers retire under the value this
        /// signals, and `collect` gives them back once a wait has passed it.
        std::uint64_t submit(VkCommandBuffer commands, std::span<const VkSemaphoreSubmitInfo> waits,
            std::span<const VkSemaphoreSubmitInfo> signals);

        /// `Recording::end`'s: ends a recording nobody will submit, so its owner can begin it again.
        void end(VkCommandBuffer commands);

        /// What a `Recording` an exception left goes back to: reset, so its owner can begin it
        /// again, and no longer counted open. The buffer stays its owner's.
        void unwind(VkCommandBuffer commands);

        /// Gives back the retired command buffers the timeline has passed. The device's, after
        /// every wait, as the graveyard's `collect` is.
        friend class Device;
        void collect();

        /// Gives back every retired command buffer, for a queue nothing is on: asserted, with
        /// nothing deferred, because the same call one wait too early is a buffer begun again
        /// while the queue still executes it.
        void collectIdle();

        /// What both share: every retired buffer stamped at or below `finished` goes to the spare
        /// list.
        void releaseRetired(std::uint64_t finished);

        /// The device's alone, because the device's waits are what collect it: a second pool's
        /// retired buffers would wait for a collect that never came.
        explicit CommandPool(const Device& device);

        VkCommandBuffer beginBatch();
        void endAndWait(VkCommandBuffer commands);

        /// Begins `commands` and records the head barrier: what `begin` does for a frame and a
        /// batch alike, without the frame's bookkeeping.
        void open(VkCommandBuffer commands);

        /// Takes `commands` off the open frame recordings where it is one.
        void close(VkCommandBuffer commands);

        /// Gives back a recording nobody will submit. `Batch::~Batch` says when that happens.
        void discard(VkCommandBuffer commands);

        /// A hold nothing else has, as an index, taken until `giveHold`.
        std::size_t takeHold();
        BatchHold& holdAt(std::size_t hold) { return mHolds[hold]; }
        void giveHold(std::size_t hold);

        /// Submits every deferred batch and then `commands`, as one submit signalling the next value of
        /// the timeline, which it returns. A deferred batch ends every upload and build in a barrier, so
        /// what `commands` reads of them is what it would have read had they been recorded into it.
        ///
        /// **With no frame recording open beside it.** What an open recording hands out is named for the
        /// next submit (`ReadStamp`), and this submit is the next: what the open recording named would
        /// read idle once this one ran, though the recording runs after it. A batch is not counted: the
        /// interface keeps one open across frames by design (`GuiTextures`) and names what it reads only
        /// when it is handed over.
        std::uint64_t submitWithDeferred(VkCommandBuffer commands, std::span<const VkSemaphoreSubmitInfo> waits,
            std::span<const VkSemaphoreSubmitInfo> signals);

        /// Logs the submit of `value` until the queue passes it, `Device::watchesSubmits`: how long it
        /// ran, and every doubling of a quarter second it is still running. An observer: it waits for
        /// nothing through the device, so what the clock knows of the queue (`Timeline`) is unchanged.
        void watch(std::uint64_t value) const;

        /// Submits each deferred batch alone and watches it to its end, ahead of the submit that
        /// would have carried them all: which batch a stall is in, and what recorded it
        /// (`mDeferredNotes`). They run where they would have run, ahead of it, and are given back
        /// once run.
        void submitDeferredWatched();

        const Device& mDevice;
        Owned<VkCommandPool, vkDestroyCommandPool> mHandle;
        OwnedBy mThread;

        /// Recorded and ended, waiting for the next submit to carry them first.
        std::vector<VkCommandBuffer> mDeferred;

        /// What the recording thread was doing as each of `mDeferred` was deferred, where submits
        /// are watched and empty otherwise.
        std::vector<std::string> mDeferredNotes;

        /// The recordings `begin` opened and nothing has closed: a frame's placements' and its
        /// trace's, one or two at a time, kept at its capacity.
        std::vector<VkCommandBuffer> mOpen;

        /// Carried by a submit and not yet known to have run.
        Retiring<VkCommandBuffer> mRetiring;

        /// Given back and not yet taken again.
        std::vector<VkCommandBuffer> mSpare;

        StagingRing mStaging;

        /// What each batch holds until it ends, lent to each batch in turn, emptied and never freed, so an
        /// arrival's batch allocates nothing once as many batches have been open at once as ever will be —
        /// the interface keeps one open across frames beside the load's.
        std::vector<BatchHold> mHolds;
        std::vector<std::size_t> mFreeHolds;

        /// Refilled per submit: a frame is three of them, and none allocates.
        std::vector<VkCommandBufferSubmitInfo> mSubmitScratch;
        std::vector<VkSemaphoreSubmitInfo> mSignalScratch;
    };

    /// Command buffers the pool lent out, owned: given back when this ends, under the next
    /// submit's value, so none is taken again while a submit that carries it may still run.
    class LentCommands
    {
    public:
        LentCommands() = default;
        LentCommands(LentCommands&& other) noexcept;
        LentCommands& operator=(LentCommands&& other) noexcept;
        ~LentCommands();

        VkCommandBuffer operator[](std::size_t at) const { return mBuffers[at]; }
        std::size_t size() const { return mBuffers.size(); }

        /// One more, kept with the rest: a ring grown to its busiest frame so far.
        VkCommandBuffer add();

    private:
        friend class CommandPool;

        LentCommands(CommandPool& pool, std::vector<VkCommandBuffer>&& buffers)
            : mPool(&pool)
            , mBuffers(std::move(buffers))
        {
        }

        /// Gives every buffer back, and holds none.
        void giveBack();

        CommandPool* mPool = nullptr;
        std::vector<VkCommandBuffer> mBuffers;
    };

    /// An open recording, from `CommandPool::begin` to its submit or its end, in one scope. An
    /// exception that leaves the scope gives the buffer back reset and no longer open, where a
    /// recording begun and closed by hand stayed open, and the next submit's assert named that in
    /// place of the error.
    class [[nodiscard]] Recording
    {
    public:
        Recording(Recording&& other) noexcept;
        Recording& operator=(Recording&&) = delete;

        /// Asserts that the recording was submitted or ended, unless an exception is unwinding.
        ~Recording();

        VkCommandBuffer get() const { return mCommands; }

        /// Submits it behind whatever was deferred and does not wait, and returns the value the
        /// submit signals on the device's timeline, which is what says it has run.
        ///
        /// @param waits,signals binary semaphores the submit waits and signals beside the timeline:
        ///        what a present's blit needs. Through the pool and not a submit of its own, because one
        ///        that took a timeline value without carrying the deferred batches would let the
        ///        graveyard free what they name before they run.
        std::uint64_t submit(
            std::span<const VkSemaphoreSubmitInfo> waits = {}, std::span<const VkSemaphoreSubmitInfo> signals = {}) &&;

        /// Ends it unsubmitted — a placement that placed nothing — so its owner can begin it again.
        void end() &&;

    private:
        friend class CommandPool;

        Recording(CommandPool& pool, VkCommandBuffer commands);

        CommandPool& mPool;
        VkCommandBuffer mCommands;

        /// How many exceptions were unwinding when it began: more at its end is an unwinding.
        int mUnwinding;
    };

    /// Through a batch, so a `record` that throws gives its recording back as an abandoned batch
    /// does. The buffer is begun whatever `record` does, so a submit that carries only what was
    /// deferred is still one.
    template <class F>
    void CommandPool::submitAndWait(F&& record)
    {
        Batch batch(*this);
        record(batch.getCommands());
        batch.flush();
    }

}
