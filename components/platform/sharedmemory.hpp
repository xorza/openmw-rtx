#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace Platform
{
    /// **Memory two processes map by one name**: made by one and opened by the other, over
    /// `sharedmemoryposix.cpp` and `sharedmemorywin32.cpp`. Move-only, and unmapped as it goes.
    class SharedMemory
    {
    public:
        /// `size` bytes of noughts under `name`, made over whatever an earlier process of that name
        /// left. Null where the system refused.
        static SharedMemory create(std::string_view name, std::size_t size);

        /// What another process made under `name`. The name is given up where the system can: nothing
        /// else opens it, and a process that crashes then leaves nothing in the system's list. Null
        /// where there is no such memory.
        static SharedMemory open(std::string_view name, std::size_t size);

        SharedMemory() = default;

        SharedMemory(SharedMemory&& other) noexcept { swap(other); }

        SharedMemory& operator=(SharedMemory&& other) noexcept
        {
            swap(other);
            return *this;
        }

        SharedMemory(const SharedMemory&) = delete;
        SharedMemory& operator=(const SharedMemory&) = delete;
        ~SharedMemory();

        void* data() const { return mData; }

    private:
        void swap(SharedMemory& other) noexcept
        {
            std::swap(mData, other.mData);
            std::swap(mSize, other.mSize);
            std::swap(mHandle, other.mHandle);
            std::swap(mMadeName, other.mMadeName);
        }

        void* mData = nullptr;
        std::size_t mSize = 0;

        /// The mapping, on a system that holds memory by a handle.
        void* mHandle = nullptr;

        /// The name this side made the memory under, on a system that keeps a name until it is given
        /// up: given up here where the other side never opened it. Empty otherwise.
        std::string mMadeName;
    };
}
