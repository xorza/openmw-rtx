#include "heap.hpp"

#include <cstdlib>

// glibc alone keeps freed arena memory until it is trimmed; another libc has no such call.
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
}
