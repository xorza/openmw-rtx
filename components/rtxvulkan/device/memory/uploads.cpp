#include "uploads.hpp"

#include <algorithm>
#include <cassert>
#include <exception>
#include <functional>

#include <volk.h>

namespace Rtx
{
    void appendRun(std::vector<CopyRun>& into, const CopyRun& run)
    {
        if (run.mSize == 0)
            return;

        if (!into.empty())
        {
            CopyRun& last = into.back();
            if (last.mFrom + last.mSize == run.mFrom && last.mTo + last.mSize == run.mTo)
            {
                last.mSize += run.mSize;
                return;
            }
        }

        into.push_back(run);
    }

    void appendRowRuns(const std::span<const Index> rows, const VkDeviceSize stride, std::vector<CopyRun>& into)
    {
        assert(std::ranges::adjacent_find(rows, std::greater_equal<>()) == rows.end()
            && "rows out of order or named twice, which would write one row twice and join no runs");

        for (const Index row : rows)
        {
            const VkDeviceSize at = VkDeviceSize{ row } * stride;
            appendRun(into, CopyRun{ .mFrom = at, .mTo = at, .mSize = stride });
        }
    }

    Uploads::~Uploads()
    {
        assert(
            (!mUnordered || std::uncaught_exceptions() > 0) && "staged writes nobody ordered ahead of what reads them");
    }

    void Uploads::writeBytes(const Buffer& into, const VkDeviceSize offset, const std::span<const std::byte> bytes)
    {
        assert(into.getKind() == BufferKind::HostWritten && "a write through `Uploads` into another kind");

        // Vulkan refuses a copy of no bytes, which a scene with no lamps asks for its light table.
        if (bytes.empty())
            return;

        stageInto(mBatch, into, offset, bytes);
        mUnordered = true;
    }

    void Uploads::writeRuns(
        const Buffer& into, const std::span<const std::byte> source, const std::span<const CopyRun> runs)
    {
        assert(into.getKind() == BufferKind::HostWritten && "a write through `Uploads` into another kind");
        assert(std::ranges::all_of(runs, [&](const CopyRun& run) { return run.mFrom + run.mSize <= source.size(); })
            && "a run past the end of what it is written from");

        if (std::ranges::none_of(runs, [](const CopyRun& run) { return run.mSize > 0; }))
            return;

        mBatch.stageRuns(into, source, runs);
        mUnordered = true;
    }

    std::span<std::byte> Uploads::lend(const Buffer& into, const VkDeviceSize offset, const VkDeviceSize size)
    {
        assert(into.getKind() == BufferKind::HostWritten && "a write through `Uploads` into another kind");

        if (size == 0)
            return {};

        const StagingLend lent = mBatch.reserve(size);
        const VkBufferCopy region{ .srcOffset = lent.mRun.mOffset, .dstOffset = offset, .size = size };
        vkCmdCopyBuffer(mBatch.getCommands(), lent.mRun.mBuffer, into.getHandle(), 1, &region);
        into.nameForNext();
        mUnordered = true;
        return lent.mBytes;
    }

    void Uploads::order()
    {
        if (!mUnordered)
            return;

        orderStagedWrites(mBatch);
        mUnordered = false;
    }
}
