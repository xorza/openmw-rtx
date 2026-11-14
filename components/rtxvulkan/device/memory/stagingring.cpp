#include "stagingring.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

namespace Rtx
{
    std::size_t StagingRing::take(const Device& device, const VkDeviceSize bytes)
    {
        const std::optional<std::size_t> fits
            = bestFit(mSpare, [&](const std::size_t block) { return mBlocks[block].getSize(); }, bytes);
        if (fits.has_value())
        {
            const std::size_t block = mSpare[*fits];
            mSpare[*fits] = mSpare.back();
            mSpare.pop_back();
            return block;
        }

        Buffer made = Buffer::staging(
            device, std::max(bytes, sStagingBlock), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "staging block");
        mBytes += made.getSize();
        if (!mLetGo.empty())
        {
            const std::size_t block = mLetGo.back();
            mLetGo.pop_back();
            mBlocks[block] = std::move(made);
            return block;
        }

        mBlocks.push_back(std::move(made));
        return mBlocks.size() - 1;
    }

    void StagingRing::releaseThrough(const std::uint64_t finished)
    {
        mRetiring.releaseThrough(finished, [&](const std::size_t block) { mSpare.push_back(block); });
    }

    void StagingRing::trim(const VkDeviceSize keep)
    {
        std::ranges::sort(mSpare, [&](const std::size_t one, const std::size_t other) {
            return mBlocks[one].getSize() < mBlocks[other].getSize();
        });

        VkDeviceSize kept = 0;
        std::size_t keeping = 0;
        while (keeping < mSpare.size() && kept + mBlocks[mSpare[keeping]].getSize() <= keep)
            kept += mBlocks[mSpare[keeping++]].getSize();

        for (std::size_t at = keeping; at < mSpare.size(); ++at)
        {
            mBytes -= mBlocks[mSpare[at]].getSize();
            mBlocks[mSpare[at]] = Buffer{};
            mLetGo.push_back(mSpare[at]);
        }
        mSpare.resize(keeping);
    }

    void StagingRing::shed(const VkDeviceSize keep)
    {
        if (mBytes <= keep || mSpare.empty())
            return;

        const auto largest
            = std::ranges::max_element(mSpare, {}, [&](const std::size_t block) { return mBlocks[block].getSize(); });
        const std::size_t block = *largest;
        *largest = mSpare.back();
        mSpare.pop_back();

        mBytes -= mBlocks[block].getSize();
        mBlocks[block] = Buffer{};
        mLetGo.push_back(block);
    }

    VkDeviceSize StagingRing::getSize(const VkBuffer block) const
    {
        const auto found
            = std::ranges::find_if(mBlocks, [&](const Buffer& staging) { return staging.getHandle() == block; });
        assert(found != mBlocks.end() && "a staging block the ring never made");
        return found->getSize();
    }
}
