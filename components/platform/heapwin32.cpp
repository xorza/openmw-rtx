#include "heap.hpp"

#include <malloc.h>

namespace Platform
{
    void trimHeap()
    {
        _heapmin();
    }
}
