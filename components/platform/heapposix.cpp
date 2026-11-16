#include "heap.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

// glibc alone keeps freed arena memory until it is trimmed and states what it holds; another libc has neither call.
#if defined(__GLIBC__)
#include <dlfcn.h>
#include <malloc.h>
#endif

namespace Platform
{
    void trimHeap()
    {
#if defined(__GLIBC__)
        malloc_trim(0);
#endif
    }

    std::optional<HeapHeld> heapHeld()
    {
#if defined(__GLIBC__)
        // glibc's `struct mallinfo2`, field for field: the release builds compile against glibc 2.28, which
        // declares neither it nor the call, so the call is found at run time and its result laid out here.
        struct MallInfo2
        {
            std::size_t mArena;
            std::size_t mOrdinaryChunks;
            std::size_t mFastChunks;
            std::size_t mMappedChunks;
            std::size_t mMappedBytes;
            std::size_t mUnusedMaximum;
            std::size_t mFastFreeBytes;
            std::size_t mInUseBytes;
            std::size_t mFreeBytes;
            std::size_t mTrimmable;
        };
        using Query = MallInfo2 (*)();
        // A symbol `dlsym` hands back as data names a function here, as POSIX has it do.
        const auto query = reinterpret_cast<Query>(dlsym(RTLD_DEFAULT, "mallinfo2"));
        if (query == nullptr)
            return std::nullopt;
        const MallInfo2 info = query();
        // What the arenas took and the chunks mapped on their own; in use, what of the arenas' is handed out and
        // every mapped chunk, which is freed back to the system the moment it is freed.
        return HeapHeld{ .mTaken = std::uint64_t{ info.mArena } + std::uint64_t{ info.mMappedBytes },
            .mInUse = std::uint64_t{ info.mInUseBytes } + std::uint64_t{ info.mMappedBytes } };
#else
        return std::nullopt;
#endif
    }
}
