#include "testplatform.hpp"

#include <cstddef>
#include <cstdlib>
#include <limits>

#include <sys/prctl.h>

namespace Rtx::Testing::TestPlatform
{
    void* allocateAligned(const std::size_t size, const std::size_t alignment) noexcept
    {
        // `aligned_alloc` is specified only for a size that is a multiple of the alignment, so the
        // request is rounded up to one, which a size within an alignment of the largest wraps.
        const std::size_t asked = size == 0 ? 1 : size;
        if (asked > std::numeric_limits<std::size_t>::max() - (alignment - 1))
            return nullptr;
        return std::aligned_alloc(alignment, (asked + alignment - 1) / alignment * alignment);
    }

    void freeAligned(void* memory) noexcept
    {
        std::free(memory);
    }

    void disableCoreDump()
    {
        // **Non-dumpable on Linux, where a zero core limit is not enough.** With `core_pattern` a pipe to
        // `systemd-coredump`, the kernel starts the collector for every abort whatever the limit says,
        // which was most of what a death test cost; non-dumpable starts nothing.
        prctl(PR_SET_DUMPABLE, 0);
    }
}
