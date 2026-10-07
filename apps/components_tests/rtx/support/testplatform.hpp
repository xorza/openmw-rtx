#pragma once

#include <cstddef>

/// What the test binaries ask of the system and the game never does: an allocation at an alignment,
/// which the counting `operator new` hands out (`allocations.cpp`), and a process that leaves no
/// core (`death.hpp`). One header over a POSIX and a Windows file that CMake chooses, the way
/// `components/platform` keeps the game's own.
namespace Rtx::Testing::TestPlatform
{
    /// `size` bytes at `alignment`, a power of two, or null, as `operator new` reports a failure.
    /// Zero bytes is still a distinct pointer, as `new` promises, and a size no rounding to the
    /// alignment can hold is a failure. Freed by `freeAligned` and nothing else: the runtime that
    /// has `aligned_alloc` frees it with `free`, and the one that does not pairs its own two.
    void* allocateAligned(std::size_t size, std::size_t alignment) noexcept;
    void freeAligned(void* memory) noexcept;

    /// Leaves the system nothing to keep of this process when it aborts: for a process that dies on
    /// purpose, as a death test's child does, whose core nobody wants.
    void disableCoreDump();
}
