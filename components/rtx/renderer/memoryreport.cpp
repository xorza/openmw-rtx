#include "memoryreport.hpp"

#include <cstddef>
#include <format>
#include <string_view>

namespace Rtx
{
    namespace
    {
        /// A heap's line up to its `reserved`, which the line under the heaps is padded to the
        /// width of, so that its columns read down the page with theirs. The index takes two
        /// places, which sixteen heaps need.
        constexpr std::string_view sHeapPrefix = "  heap {:>2}  {:<12}  {:8.1f} MiB   ";
    }

    std::string describeMemory(const MemoryReport& report)
    {
        std::string out;
        for (std::uint32_t heap = 0; heap < report.mHeapCount; ++heap)
        {
            const HeapUse& use = report.mHeaps[heap];

            // **Both flags are named**, because resizable BAR makes the whole of video memory one
            // heap the host writes into, and a label that read the host's flag alone called it the
            // small window of a card without. The system's heap is named too, because on such a
            // card it is the largest of the three and read as video memory it is a card with more
            // room than it has.
            const char* const kind = !use.mDeviceLocal ? "system" : use.mHostVisible ? "device+host" : "device-only";
            out += std::format(sHeapPrefix, heap, kind, megabytes(use.mSize));
            out += std::format("reserved {:7.1f}   live {:7.1f}", megabytes(use.mReserved), megabytes(use.mLive));

            // Nought from a driver that would not say, which is not the same answer as a budget of
            // none — so the columns are left off rather than printed as zeroes.
            if (use.mBudget > 0)
                out += std::format("   budget {:7.1f}   held {:7.1f}", megabytes(use.mBudget), megabytes(use.mHeld));

            out += std::format("   {} blocks\n", use.mBlocks);
        }

        // Under the heaps and in their columns, because it cuts across them. A card with
        // resizable BAR states one heap that is host-visible throughout, so this is the only line
        // that says what would have to fit in the small aperture of a card without it.
        const std::size_t column = std::formatted_size(sHeapPrefix, 0u, "", 0.0);
        out += std::format("{:<{}}reserved {:7.1f}   live {:7.1f}\n", "  host-written", column,
            megabytes(report.mHostWrittenReserved), megabytes(report.mHostWrittenLive));

        return out;
    }
}
