#include "heap.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>

#include <dlfcn.h>

// glibc alone keeps freed arena memory until it is trimmed and states what it holds; another libc has neither call.
#if defined(__GLIBC__)
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
        // **A sanitizer's allocator first, where one is linked in**: it serves `malloc` in glibc's place, so glibc's
        // figures describe arenas nothing allocates from. Its own (compiler-rt's `sanitizer/allocator_interface.h`)
        // are found at run time, as every build but a sanitized one has none.
        using Figure = std::size_t (*)();
        // A symbol `dlsym` hands back as data names a function here, as POSIX has it do.
        const auto mapped = reinterpret_cast<Figure>(dlsym(RTLD_DEFAULT, "__sanitizer_get_heap_size"));
        const auto allocated = reinterpret_cast<Figure>(dlsym(RTLD_DEFAULT, "__sanitizer_get_current_allocated_bytes"));
        if (mapped != nullptr && allocated != nullptr)
            return HeapHeld{ .mTaken = std::uint64_t{ mapped() }, .mInUse = std::uint64_t{ allocated() } };

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
