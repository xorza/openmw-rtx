#include "sharedmemory.hpp"

#include <cstddef>
#include <string>
#include <string_view>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

namespace Platform
{
    namespace
    {
        /// A POSIX name begins with a slash. Keep names short: macOS takes 31 characters of one.
        std::string systemName(std::string_view name)
        {
            return "/" + std::string(name);
        }
    }

    SharedMemory SharedMemory::create(std::string_view name, std::size_t size)
    {
        SharedMemory memory;
        const std::string made = systemName(name);
        const int descriptor = shm_open(made.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0600);
        if (descriptor < 0)
            return memory;

        void* const view = ftruncate(descriptor, static_cast<off_t>(size)) == 0
            ? mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0)
            : MAP_FAILED;
        close(descriptor);
        if (view == MAP_FAILED)
        {
            shm_unlink(made.c_str());
            return memory;
        }

        memory.mData = view;
        memory.mSize = size;
        memory.mMadeName = made;
        return memory;
    }

    SharedMemory SharedMemory::open(std::string_view name, std::size_t size)
    {
        SharedMemory memory;
        const std::string opened = systemName(name);
        const int descriptor = shm_open(opened.c_str(), O_RDWR, 0600);
        if (descriptor < 0)
            return memory;

        void* const view = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
        close(descriptor);
        shm_unlink(opened.c_str());
        if (view == MAP_FAILED)
            return memory;

        memory.mData = view;
        memory.mSize = size;
        return memory;
    }

    SharedMemory::~SharedMemory()
    {
        if (mData == nullptr)
            return;

        munmap(mData, mSize);
        if (!mMadeName.empty())
            shm_unlink(mMadeName.c_str());
    }
}
