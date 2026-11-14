#include "commands.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include <components/crashcatcher/crashnote.hpp>
#include <components/debug/debuglog.hpp>

#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>

#include "device.hpp"
#include "handles.hpp"
#include "result.hpp"
#include "timeline.hpp"

namespace Rtx
{
    namespace
    {
        /// Waits for what `poll` answers for — `VK_SUCCESS` once done, `VK_NOT_READY` while it runs, an
        /// error where the asking failed — and logs how long `what` ran to its end or failure, and at every
        /// doubling past a quarter of a second while it runs, with what `where` says of the queue. **No
        /// longer than any wait on the device**, `sPatience`, past which it ends as one that never answered.
        template <class Poll, class Where>
        void watchToItsEnd(const std::string& what, Poll poll, Where where)
        {
            using Clock = std::chrono::steady_clock;
            const Clock::time_point start = Clock::now();
            std::chrono::milliseconds reportAt(250);
            while (true)
            {
                const VkResult status = poll();
                const auto ran = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start);
                if (status == VK_SUCCESS)
                {
                    Log(Debug::Info) << what << " ran in " << ran.count() << " ms";
                    return;
                }
                if (status != VK_NOT_READY)
                {
                    Log(Debug::Error) << what << ": " << resultName(status) << " after " << ran.count() << " ms";
                    return;
                }
                if (ran >= std::chrono::nanoseconds(sPatience))
                    deviceFailed(timedOut(what.c_str(), sPatience) + where());
                if (ran >= reportAt)
                {
                    Log(Debug::Warning) << what << " still running after " << ran.count() << " ms" << where();
                    reportAt *= 2;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }

    CommandPool::CommandPool(const Device& device)
        : mDevice(device)
    {
        const VkCommandPoolCreateInfo create{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = device.getQueueFamily(),
        };
        mHandle = Owned<VkCommandPool, vkDestroyCommandPool>::make(
            device, vkCreateCommandPool, create, "vkCreateCommandPool");
    }

    void CommandPool::defer(VkCommandBuffer commands)
    {
        mThread.check();
        close(commands);
        checkVk(mDevice, vkEndCommandBuffer(commands), "vkEndCommandBuffer");
        mDeferred.push_back(commands);
        if (mDevice.watchesSubmits())
            mDeferredNotes.emplace_back(Crash::ownNote());
    }

    void CommandPool::submitDeferredWatched()
    {
        const VkFenceCreateInfo fenceInfo{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = nullptr, .flags = 0 };
        const std::size_t count = mDeferred.size();
        for (std::size_t at = 0; at < count; ++at)
        {
            const std::string_view note = mDeferredNotes.empty() ? std::string_view() : mDeferredNotes.front();
            Log(Debug::Info) << "deferred batch " << at + 1 << " of " << count << ", recorded while " << note;

            const Fence fence = Fence::make(mDevice, vkCreateFence, fenceInfo, "vkCreateFence");
            const VkCommandBufferSubmitInfo batch{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                .pNext = nullptr,
                .commandBuffer = mDeferred.front(),
                .deviceMask = 0,
            };
            const VkSubmitInfo2 submit{
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                .pNext = nullptr,
                .flags = 0,
                .waitSemaphoreInfoCount = 0,
                .pWaitSemaphoreInfos = nullptr,
                .commandBufferInfoCount = 1,
                .pCommandBufferInfos = &batch,
                .signalSemaphoreInfoCount = 0,
                .pSignalSemaphoreInfos = nullptr,
            };
            checkVk(mDevice, vkQueueSubmit2(mDevice.getQueue(), 1, &submit, fence.get()), "vkQueueSubmit2");

            watchToItsEnd(
                "deferred batch " + std::to_string(at + 1),
                [&] { return vkGetFenceStatus(mDevice.getHandle(), fence.get()); }, [] { return std::string(); });

            // **Run, and so given back here, one at a time**: the submit after this one carries
            // none of those that ran, and a throw at a later batch leaves only the batches that did
            // not run to the queue, where a give-back at the end handed every one to it again.
            recycle(std::span<const VkCommandBuffer>(&mDeferred.front(), 1));
            mDeferred.erase(mDeferred.begin());
            if (!mDeferredNotes.empty())
                mDeferredNotes.erase(mDeferredNotes.begin());
        }
    }

    std::uint64_t CommandPool::submitWithDeferred(VkCommandBuffer commands,
        const std::span<const VkSemaphoreSubmitInfo> waits, const std::span<const VkSemaphoreSubmitInfo> signals)
    {
        assert(mOpen.empty() && "a submit while a frame's recording is open, which would take the value it named for");

        // Watched, each deferred batch goes alone ahead of this submit, which then carries none.
        const bool watched = mDevice.watchesSubmits();
        if (watched)
            submitDeferredWatched();

        mSubmitScratch.clear();
        mSubmitScratch.reserve(mDeferred.size() + 1);
        for (const VkCommandBuffer deferred : mDeferred)
            mSubmitScratch.push_back(VkCommandBufferSubmitInfo{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
                .pNext = nullptr,
                .commandBuffer = deferred,
                .deviceMask = 0,
            });
        mSubmitScratch.push_back(VkCommandBufferSubmitInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .pNext = nullptr,
            .commandBuffer = commands,
            .deviceMask = 0,
        });

        // The timeline's signal and then whatever the caller adds, which is a present's. **The value
        // is taken once the submit is made**: a submit refused for room counted on the clock
        // left it ahead of the queue for good, and every later wait on a value past it timed out
        // and blamed the device. Only the device's thread submits, so nothing takes it between.
        Timeline& timeline = mDevice.getTimeline();
        const std::uint64_t value = timeline.getNext();
        mSignalScratch.clear();
        mSignalScratch.reserve(signals.size() + 1);
        mSignalScratch.push_back(timeline.signal(value));
        mSignalScratch.insert(mSignalScratch.end(), signals.begin(), signals.end());

        const VkSubmitInfo2 submit{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .pNext = nullptr,
            .flags = 0,
            .waitSemaphoreInfoCount = static_cast<std::uint32_t>(waits.size()),
            .pWaitSemaphoreInfos = waits.data(),
            .commandBufferInfoCount = static_cast<std::uint32_t>(mSubmitScratch.size()),
            .pCommandBufferInfos = mSubmitScratch.data(),
            .signalSemaphoreInfoCount = static_cast<std::uint32_t>(mSignalScratch.size()),
            .pSignalSemaphoreInfos = mSignalScratch.data(),
        };
        if (watched)
            Log(Debug::Info) << "submit " << value << " while " << Crash::ownNote();

        checkVk(mDevice, vkQueueSubmit2(mDevice.getQueue(), 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit2");

        [[maybe_unused]] const std::uint64_t taken = timeline.next();
        assert(taken == value && "a timeline value taken off the device's thread between the signal and the submit");

        if (watched)
            watch(value);
        return value;
    }

    void CommandPool::watch(const std::uint64_t value) const
    {
        std::uint64_t reached = 0;
        watchToItsEnd(
            "submit " + std::to_string(value),
            [&] {
                const VkResult read
                    = vkGetSemaphoreCounterValue(mDevice.getHandle(), mDevice.getTimeline().getHandle(), &reached);
                return read != VK_SUCCESS ? read : reached >= value ? VK_SUCCESS : VK_NOT_READY;
            },
            [&] { return ", the queue at " + std::to_string(reached); });
    }

    void CommandPool::finishDeferred()
    {
        if (mDeferred.empty())
            return;

        submitAndWait([](VkCommandBuffer) {});
    }

    std::uint64_t CommandPool::submit(VkCommandBuffer commands, const std::span<const VkSemaphoreSubmitInfo> waits,
        const std::span<const VkSemaphoreSubmitInfo> signals)
    {
        mThread.check();
        close(commands);
        checkVk(mDevice, vkEndCommandBuffer(commands), "vkEndCommandBuffer");

        const std::uint64_t value = submitWithDeferred(commands, waits, signals);

        // Under the value this submit signals: the deferred batches run ahead of `commands` and
        // are finished when it is.
        for (VkCommandBuffer deferred : mDeferred)
            mRetiring.hold(value, std::move(deferred));
        mDeferred.clear();
        mDeferredNotes.clear();

        return value;
    }

    void CommandPool::collect()
    {
        releaseRetired(mDevice.getTimeline().getKnownFinished());
    }

    void CommandPool::collectIdle()
    {
        assert(mDevice.getTimeline().isIdle() && "command buffers given back under a submit still on the queue");
        assert(mDeferred.empty() && "command buffers given back under a batch not yet submitted");
        assert(mOpen.empty() && "command buffers given back while a recording is open");

        releaseRetired(std::numeric_limits<std::uint64_t>::max());
    }

    void CommandPool::releaseRetired(const std::uint64_t finished)
    {
        mRetiring.releaseThrough(finished, [&](const VkCommandBuffer commands) { mSpare.push_back(commands); });
        mStaging.releaseThrough(finished);
    }

    void CommandPool::discard(VkCommandBuffer commands)
    {
        // Neither ended nor submitted: a buffer still being recorded is not pending, so this is
        // where a recording nobody wants goes back. Reset first, because a begin resets a buffer
        // that was ended and not one still recording.
        unwind(commands);
        recycle(std::span<const VkCommandBuffer>(&commands, 1));
    }

    void CommandPool::recycle(std::span<const VkCommandBuffer> commands)
    {
        mSpare.insert(mSpare.end(), commands.begin(), commands.end());
    }

    VkCommandBuffer CommandPool::take()
    {
        mThread.check();
        if (!mSpare.empty())
        {
            const VkCommandBuffer spare = mSpare.back();
            mSpare.pop_back();
            return spare;
        }

        const VkCommandBufferAllocateInfo allocate{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = mHandle.get(),
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        VkCommandBuffer commands = VK_NULL_HANDLE;
        checkVk(
            mDevice, vkAllocateCommandBuffers(mDevice.getHandle(), &allocate, &commands), "vkAllocateCommandBuffers");
        return commands;
    }

    LentCommands CommandPool::lend(const std::uint32_t count)
    {
        std::vector<VkCommandBuffer> buffers;
        buffers.reserve(count);
        for (std::uint32_t at = 0; at < count; ++at)
            buffers.push_back(take());

        return LentCommands(*this, std::move(buffers));
    }

    void CommandPool::retire(std::span<const VkCommandBuffer> commands)
    {
        const std::uint64_t until = mDevice.getTimeline().getNext();
        for (VkCommandBuffer buffer : commands)
            mRetiring.hold(until, std::move(buffer));
    }

    LentCommands::LentCommands(LentCommands&& other) noexcept
        : mPool(other.mPool)
        , mBuffers(std::move(other.mBuffers))
    {
        other.mBuffers.clear();
    }

    LentCommands& LentCommands::operator=(LentCommands&& other) noexcept
    {
        if (this != &other)
        {
            giveBack();
            mPool = other.mPool;
            mBuffers = std::move(other.mBuffers);
            other.mBuffers.clear();
        }
        return *this;
    }

    LentCommands::~LentCommands()
    {
        giveBack();
    }

    VkCommandBuffer LentCommands::add()
    {
        assert(mPool != nullptr && "a buffer added to commands no pool lent");
        mBuffers.push_back(mPool->take());
        return mBuffers.back();
    }

    void LentCommands::giveBack()
    {
        if (mBuffers.empty())
            return;
        mPool->retire(mBuffers);
        mBuffers.clear();
    }

    std::size_t CommandPool::takeHold()
    {
        if (!mFreeHolds.empty())
        {
            const std::size_t hold = mFreeHolds.back();
            mFreeHolds.pop_back();
            return hold;
        }

        mHolds.emplace_back();
        return mHolds.size() - 1;
    }

    void CommandPool::giveHold(const std::size_t hold)
    {
        [[maybe_unused]] const BatchHold& given = mHolds[hold];
        assert(given.mBuffers.empty() && given.mImages.empty() && given.mBlocks.empty()
            && "a hold given back still holding what its batch never released");
        mFreeHolds.push_back(hold);
    }

    Recording CommandPool::begin(VkCommandBuffer commands)
    {
        mThread.check();
        assert(std::ranges::find(mOpen, commands) == mOpen.end() && "a recording begun twice");
        mOpen.push_back(commands);
        try
        {
            open(commands);
        }
        catch (...)
        {
            close(commands);
            throw;
        }
        return Recording(*this, commands);
    }

    void CommandPool::unwind(VkCommandBuffer commands)
    {
        close(commands);
        checkVk(mDevice, vkResetCommandBuffer(commands, 0), "vkResetCommandBuffer");
    }

    Recording::Recording(CommandPool& pool, VkCommandBuffer commands)
        : mPool(pool)
        , mCommands(commands)
        , mUnwinding(std::uncaught_exceptions())
    {
    }

    Recording::Recording(Recording&& other) noexcept
        : mPool(other.mPool)
        , mCommands(std::exchange(other.mCommands, VK_NULL_HANDLE))
        , mUnwinding(other.mUnwinding)
    {
    }

    Recording::~Recording()
    {
        if (mCommands == VK_NULL_HANDLE)
            return;

        assert(std::uncaught_exceptions() > mUnwinding && "a recording neither submitted nor ended");
        tearDown("a recording an exception left would not be reset", [&] { mPool.unwind(mCommands); });
    }

    std::uint64_t Recording::submit(
        std::span<const VkSemaphoreSubmitInfo> waits, std::span<const VkSemaphoreSubmitInfo> signals) &&
    {
        const std::uint64_t submitted = mPool.submit(mCommands, waits, signals);
        mCommands = VK_NULL_HANDLE;
        return submitted;
    }

    void Recording::end() &&
    {
        mPool.end(mCommands);
        mCommands = VK_NULL_HANDLE;
    }

    void CommandPool::open(VkCommandBuffer commands)
    {
        const VkCommandBufferBeginInfo begin{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr,
        };
        checkVk(mDevice, vkBeginCommandBuffer(commands, &begin), "vkBeginCommandBuffer");

        // **Everything earlier on the queue, before anything in this buffer.** A buffer is recorded over a
        // frame still running and its barriers are between its own passes; they do not say its first
        // pass comes after that frame's last (a refit reading a pose row an arrival's skin pass writes
        // over, a copy into a table a trace reads through). One full barrier at the head says it for
        // every pass at once, where a dependency derived per resource would be far longer. A full barrier
        // costs this renderer nothing measurable, and every discard and first read of a copy in the
        // buffer leans on it, sourcing itself at nothing.
        handOver(commands, Use::sBufferAnyReadWrite, Use::sBufferAnyReadWrite);
    }

    void CommandPool::end(VkCommandBuffer commands)
    {
        close(commands);
        checkVk(mDevice, vkEndCommandBuffer(commands), "vkEndCommandBuffer");
    }

    VkCommandBuffer CommandPool::beginBatch()
    {
        const VkCommandBuffer commands = take();
        open(commands);
        return commands;
    }

    void CommandPool::close(VkCommandBuffer commands)
    {
        std::erase(mOpen, commands);
    }

    void CommandPool::endAndWait(VkCommandBuffer commands)
    {
        checkVk(mDevice, vkEndCommandBuffer(commands), "vkEndCommandBuffer");

        mDevice.waitFor(submitWithDeferred(commands, {}, {}), "a one-off submit");

        // The copies have run, so every buffer that carried a deferred batch can go back; what
        // the batches read was buried when they were handed over, and the wait above collected
        // it.
        recycle(mDeferred);
        recycle(std::span<const VkCommandBuffer>(&commands, 1));
        mDeferred.clear();
        mDeferredNotes.clear();
    }
}
