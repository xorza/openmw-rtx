#include "heap.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string_view>

#include <dlfcn.h>

// glibc alone keeps freed arena memory until it is trimmed and states what it holds; another libc has neither call.
#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include "glibctext.hpp"

namespace Platform
{
    void trimHeap()
    {
#if defined(__GLIBC__)
        malloc_trim(0);
#endif
    }

    void tuneHeap()
    {
#if defined(__GLIBC__)
        const char* const tunables = std::getenv("GLIBC_TUNABLES");
        if (tunables != nullptr && GlibcText::namesTheHeapsSettings(tunables))
            return;
        // NOLINTNEXTLINE(concurrency-mt-unsafe): called once by `init`, before any thread is made.
        mallopt(M_ARENA_MAX, 4);
        // NOLINTNEXTLINE(concurrency-mt-unsafe): called once by `init`, before any thread is made.
        mallopt(M_MMAP_THRESHOLD, 1 << 20);
#endif
    }

    bool heapReports()
    {
        return true;
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
            return HeapHeld{
                .mTaken = std::uint64_t{ mapped() }, .mInUse = std::uint64_t{ allocated() }, .mArenas = {}
            };

#if defined(__GLIBC__)
        // **Its report, every arena apart**, which every glibc since 2.10 writes: the release builds run on glibc 2.28,
        // which has no `mallinfo2`, and `mallinfo` counts in an `int`, past which a heap of gigabytes wraps.
        char* text = nullptr;
        std::size_t length = 0;
        FILE* const stream = open_memstream(&text, &length);
        if (stream == nullptr)
            return std::nullopt;
        const bool written = malloc_info(0, stream) == 0;
        // Closed before the text is read, which writes it out and its length.
        const bool closed = std::fclose(stream) == 0;
        std::optional<HeapHeld> held
            = written && closed ? GlibcText::heapHeld(std::string_view(text, length)) : std::nullopt;
        std::free(text);
        return held;
#else
        return std::nullopt;
#endif
    }
}
