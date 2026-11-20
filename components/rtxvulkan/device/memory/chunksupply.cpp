#include "chunksupply.hpp"

#include <cassert>
#include <mutex>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include <components/platform/thread.hpp>

namespace Rtx
{
    ChunkSupply::ChunkSupply(MemoryAllocator& allocator)
        : mAllocator(allocator)
    {
        mRequests.reserve(sInFlightMost);
        mMade.reserve(sInFlightMost);
        mThread = Platform::startThread("chunks", [this](const std::stop_token& stop) { serve(stop); });
    }

    ChunkSupply::~ChunkSupply()
    {
        mThread.request_stop();
        mThread = std::jthread();
        for (BlockMade& made : mMade)
            if (made.mMemory.has_value())
                mAllocator.giveBlock(std::move(*made.mMemory));
        for (Request& request : mRequests)
            if (auto* memory = std::get_if<DeviceMemory>(&request))
                mAllocator.giveBlock(std::move(*memory));
    }

    void ChunkSupply::ask(const BlockIndex block, const std::uint32_t type, const VkDeviceSize bytes)
    {
        {
            const std::scoped_lock lock(mMutex);
            assert(mRequests.size() < sInFlightMost && "more blocks asked than an arena holds");
            mRequests.emplace_back(Ask{ .mBlock = block, .mType = type, .mBytes = bytes });
        }
        mWake.notify_one();
    }

    void ChunkSupply::give(DeviceMemory&& block)
    {
        {
            const std::scoped_lock lock(mMutex);
            assert(mRequests.size() < sInFlightMost && "more blocks given back than an arena holds");
            mRequests.emplace_back(std::move(block));
        }
        mWake.notify_one();
    }

    void ChunkSupply::take(std::vector<BlockMade>& into)
    {
        const std::scoped_lock lock(mMutex);
        for (BlockMade& made : mMade)
            into.push_back(std::move(made));
        mMade.clear();
    }

    bool ChunkSupply::isIdle() const
    {
        const std::scoped_lock lock(mMutex);
        return mRequests.empty() && !mBusy;
    }

    void ChunkSupply::awaitIdle() const
    {
        std::unique_lock lock(mMutex);
        mIdle.wait(lock, [&] { return mRequests.empty() && !mBusy; });
    }

    void ChunkSupply::serve(const std::stop_token& stop)
    {
        std::vector<Request> working;
        working.reserve(sInFlightMost);
        std::vector<BlockMade> made;
        made.reserve(sInFlightMost);
        std::unique_lock lock(mMutex);
        while (mWake.wait(lock, stop, [&] { return !mRequests.empty(); }))
        {
            // Every request so far, in the order asked, with the lock let go, so the frame queues behind no
            // allocation.
            std::swap(working, mRequests);
            mBusy = true;
            lock.unlock();

            for (Request& request : working)
            {
                if (auto* asked = std::get_if<Ask>(&request))
                {
                    Result<DeviceMemory, MemoryRefusal> block = mAllocator.takeBlock(asked->mType, asked->mBytes);
                    made.push_back(BlockMade{ .mBlock = asked->mBlock,
                        .mMemory = block.isOk() ? std::optional(std::move(block.value())) : std::nullopt });
                }
                else
                    mAllocator.giveBlock(std::move(std::get<DeviceMemory>(request)));
            }
            working.clear();

            lock.lock();
            mBusy = false;
            for (BlockMade& each : made)
                mMade.push_back(std::move(each));
            made.clear();
            mIdle.notify_all();
        }
    }
}
