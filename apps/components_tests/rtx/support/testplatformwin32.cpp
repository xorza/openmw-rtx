#include "testplatform.hpp"

#include <cstddef>

#include <malloc.h>

namespace Rtx::Testing::TestPlatform
{
    void* allocateAligned(const std::size_t size, const std::size_t alignment) noexcept
    {
        return _aligned_malloc(size == 0 ? 1 : size, alignment);
    }

    void freeAligned(void* memory) noexcept
    {
        _aligned_free(memory);
    }

    // A crash on Windows leaves a dump only where something asks for one, and the test binaries ask
    // for none.
    void disableCoreDump() {}
}
