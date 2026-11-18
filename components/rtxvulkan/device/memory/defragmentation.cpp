#include "defragmentation.hpp"

#include <algorithm>
#include <cassert>
#include <span>
#include <tuple>
#include <utility>

#include <vk_mem_alloc.h>

#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/timeline.hpp>

#include "memory.hpp"

namespace Rtx
{
    Defragmentation::Defragmentation(MemoryAllocator& owner, VmaPool_T* const pool, const VkDeviceSize bytesPerPass,
        const std::uint32_t rangesPerPass)
        : mOwner(&owner)
    {
        // The fast algorithm: a range moves into the blocks ahead of it, the fewest copies, which a frame's share of
        // bytes a pass bounds.
        const VmaDefragmentationInfo info{ .flags = VMA_DEFRAGMENTATION_FLAG_ALGORITHM_FAST_BIT,
            .pool = pool,
            .maxBytesPerPass = bytesPerPass,
            .maxAllocationsPerPass = rangesPerPass,
            .pfnBreakCallback = nullptr,
            .pBreakCallbackUserData = nullptr };
        mOwner->checkDefragmentation(
            vmaBeginDefragmentation(mOwner->mLibrary.get(), &info, &mContext), "vmaBeginDefragmentation");
    }

    Defragmentation::Defragmentation(Defragmentation&& other) noexcept
        : mOwner(std::exchange(other.mOwner, nullptr))
        , mContext(std::exchange(other.mContext, nullptr))
        , mMoves(std::exchange(other.mMoves, nullptr))
        , mMoveCount(std::exchange(other.mMoveCount, 0))
        , mCopiedBy(std::exchange(other.mCopiedBy, 0))
        , mMoved(other.mMoved)
    {
    }

    Defragmentation& Defragmentation::operator=(Defragmentation&& other) noexcept
    {
        if (this != &other)
        {
            end();
            mOwner = std::exchange(other.mOwner, nullptr);
            mContext = std::exchange(other.mContext, nullptr);
            mMoves = std::exchange(other.mMoves, nullptr);
            mMoveCount = std::exchange(other.mMoveCount, 0);
            mCopiedBy = std::exchange(other.mCopiedBy, 0);
            mMoved = other.mMoved;
        }
        return *this;
    }

    Defragmentation::~Defragmentation()
    {
        end();
    }

    void Defragmentation::end() noexcept
    {
        if (mContext == nullptr)
            return;
        if (isPassOpen())
        {
            if (mCopiedBy != 0)
                mOwner->mDevice.waitFor(mCopiedBy, "a defragmentation's copies");
            // Done or not, the context ends below.
            std::ignore = endPass();
        }
        if (mContext != nullptr)
            finish();
    }

    void Defragmentation::finish() noexcept
    {
        VmaDefragmentationStats stats{};
        vmaEndDefragmentation(mOwner->mLibrary.get(), std::exchange(mContext, nullptr), &stats);
        mMoved.mBytesFreed = stats.bytesFreed;
        mMoved.mBlocksFreed = stats.deviceMemoryBlocksFreed;
    }

    bool Defragmentation::isCopied() const
    {
        assert(isPassOpen() && "a pass asked after that is not open");
        return mCopiedBy == 0 || mOwner->mDevice.getTimeline().hasFinished(mCopiedBy);
    }

    bool Defragmentation::beginPass(std::vector<MemoryMove>& moves)
    {
        assert(mMoves == nullptr && "a pass opened over one that is open");
        moves.clear();

        VmaDefragmentationPassMoveInfo pass{};
        const VkResult result = vmaBeginDefragmentationPass(mOwner->mLibrary.get(), mContext, &pass);
        if (result == VK_SUCCESS)
        {
            finish();
            return false;
        }
        mOwner->checkDefragmentation(result == VK_INCOMPLETE ? VK_SUCCESS : result, "vmaBeginDefragmentationPass");

        mMoves = pass.pMoves;
        mMoveCount = pass.moveCount;
        moves.reserve(mMoveCount);
        for (std::uint32_t at = 0; at < mMoveCount; ++at)
        {
            VmaAllocationInfo to{};
            vmaGetAllocationInfo(mOwner->mLibrary.get(), mMoves[at].dstTmpAllocation, &to);
            moves.push_back(MemoryMove{
                .mRange = mMoves[at].srcAllocation, .mTo = to.deviceMemory, .mToOffset = to.offset, .mAt = at });
        }
        std::ranges::sort(moves, {}, &MemoryMove::mRange);
        mOwner->openPass(moves);
        return true;
    }

    void Defragmentation::ignore(const std::uint32_t at)
    {
        assert(at < mMoveCount && "a move the open pass does not hold");
        mMoves[at].operation = VMA_DEFRAGMENTATION_MOVE_OPERATION_IGNORE;
    }

    bool Defragmentation::endPass()
    {
        assert(isPassOpen() && "a pass ended that was never opened");
        assert(isCopied() && "a pass ended before its copies ran");
        VmaDefragmentationPassMoveInfo pass{ .moveCount = mMoveCount, .pMoves = mMoves };
        for (const VmaDefragmentationMove& move : std::span(mMoves, mMoveCount))
            if (move.operation == VMA_DEFRAGMENTATION_MOVE_OPERATION_COPY)
            {
                VmaAllocationInfo moved{};
                vmaGetAllocationInfo(mOwner->mLibrary.get(), move.srcAllocation, &moved);
                mMoved.mBytes += moved.size;
                ++mMoved.mRanges;
            }

        const VkResult result = vmaEndDefragmentationPass(mOwner->mLibrary.get(), mContext, &pass);
        mMoves = nullptr;
        mMoveCount = 0;
        mCopiedBy = 0;
        mOwner->closePass();
        if (result == VK_SUCCESS)
        {
            finish();
            return false;
        }
        mOwner->checkDefragmentation(result == VK_INCOMPLETE ? VK_SUCCESS : result, "vmaEndDefragmentationPass");
        return true;
    }
}
