#pragma once

#include <cstddef>

namespace Rtx::Testing
{
    /// How many times the calling thread has been to the heap for a C++ object.
    ///
    /// Counted by replacing the global `operator new` in the one translation unit that defines this, so
    /// every allocation any of this code makes is here, the standard library's included.
    ///
    /// **The calling thread's and no other's**, because a test measures the code it called and a frame's
    /// path runs on one thread: counted for the whole process, a walk beside the ring's cell reader took
    /// that thread's reads for its own. **Not `malloc`**, which would also catch the driver's own
    /// allocations, noise a guard has no use for.
    std::size_t getAllocationCount();
}
