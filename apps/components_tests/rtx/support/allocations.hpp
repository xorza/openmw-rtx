#pragma once

#include <cstddef>

namespace Rtx::Testing
{
    /// How many times the calling thread has been to the heap for a C++ object.
    ///
    /// Counted by replacing the global `operator new` in the one translation unit that defines this,
    /// which the linker then uses for the whole binary — so every allocation any of this code makes
    /// is here, including the ones inside the standard library.
    ///
    /// **The calling thread's and no other's**, because what a test measures is the code it called,
    /// and a frame's path runs on one thread. Counted for the whole process, a walk measured beside
    /// the ring's cell reader took that thread's reads for its own, one run in a few hundred under
    /// load. Work a measured call hands to another thread is not counted.
    ///
    /// **Not `malloc`.** A `--wrap=malloc` would also catch the driver's own allocations, which are
    /// not this renderer's to control and would turn a guard into a source of noise. Everything the
    /// frame path is forbidden to do — construct a `std::string`, grow an unreserved vector, capture
    /// a `std::function`, reach for `make_unique` — arrives here regardless.
    std::size_t getAllocationCount();
}
