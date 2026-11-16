#include "heapdescription.hpp"

#include <algorithm>
#include <cstddef>
#include <format>
#include <vector>

#include <components/rtx/common/mebibytes.hpp>

namespace RtxTool
{
    std::string describeHeap(const Platform::HeapHeld& heap)
    {
        std::string said = std::format("the heap took {:.1f} MiB, {:.1f} MiB of it free", Rtx::mebibytes(heap.mTaken),
            Rtx::mebibytes(heap.mTaken - std::min(heap.mInUse, heap.mTaken)));
        if (heap.mArenas.empty())
            return said;

        // The four keeping the most, in that order, of however many there are; a tie in the order glibc numbers them.
        constexpr std::size_t sMost = 4;
        std::vector<Platform::HeapArena> most(heap.mArenas);
        std::ranges::stable_sort(most, [](const auto& a, const auto& b) { return a.mFree > b.mFree; });
        most.resize(std::min(most.size(), sMost));
        said += std::format(
            ", in {} {}, the most free", heap.mArenas.size(), heap.mArenas.size() == 1 ? "arena" : "arenas");
        for (std::size_t at = 0; at < most.size(); ++at)
            said += std::format("{} {:.1f} of {:.1f} MiB", at == 0 ? "" : ",", Rtx::mebibytes(most[at].mFree),
                Rtx::mebibytes(most[at].mTaken));
        return said;
    }
}
