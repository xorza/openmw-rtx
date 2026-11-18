#include "platform.hpp"

#include <cstdio>

#include "heap.hpp"

namespace Platform
{
    void init()
    {
        // More files open at once at the stream level than the C runtime's 512, which a large install passes:
        // https://learn.microsoft.com/cpp/c-runtime-library/reference/setmaxstdio#remarks
        static_cast<void>(_setmaxstdio(8192));
        tuneHeap();
    }
}
