#pragma once

#include <cstdint>

#include "uniquehold.hpp"

/// A shared library opened by name at run time and asked for its symbols — the driver's
/// management library is the one the ray tracer opens, because a build must not link what only
/// one vendor's driver ships. One header over `libraryposix.cpp` and `librarywin32.cpp`, the way
/// `file.hpp` sits over its two.
namespace Platform::Library
{
    enum class Handle : std::intptr_t
    {
        Invalid = 0
    };

    /// Opens the library the system finds under `name`, by its own search — a bare file name and
    /// never a path, so the driver's copy is the one found. Invalid where there is none.
    Handle open(const char* name);

    /// The address `symbol` has in `handle`, or null where it exports none by that name.
    void* find(Handle handle, const char* symbol);

    void close(Handle handle);

    struct Closing
    {
        using Handle = Library::Handle;
        static constexpr Handle sNone = Handle::Invalid;
        static void close(Handle handle) noexcept { Library::close(handle); }
    };

    using ScopedHandle = UniqueHold<Closing>;
}
