#pragma once

#include <cstddef>
#include <optional>
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
        /// `size` bytes of noughts under `name`, made over whatever an earlier process of this user
        /// left under it. Null where the system refused, or where another user holds the name: a
        /// name a reader can predict, opened as it stood, shared what this side writes with them.
        static SharedMemory create(std::string_view name, std::size_t size);

        /// What another process made under `name`. The name is given up where the system can: nothing
        /// else opens it, and a process that crashes then leaves nothing in the system's list. Null
        /// where there is no such memory.
        static SharedMemory open(std::string_view name, std::size_t size);

        SharedMemory() = default;

        SharedMemory(SharedMemory&& other) noexcept { take(other); }

        /// Unmaps what this held before it takes the other's.
        SharedMemory& operator=(SharedMemory&& other) noexcept
        {
            if (this != &other)
            {
                unmap();
                take(other);
            }
            return *this;
        }

        SharedMemory(const SharedMemory&) = delete;
        SharedMemory& operator=(const SharedMemory&) = delete;
        ~SharedMemory() { unmap(); }

        void* data() const { return mData; }

        /// Whether the other side has opened what this side made, where the system can tell: a
        /// POSIX `open` gives the name up, so a name gone is memory opened. Nothing on a system that
        /// keeps the name for as long as any handle to it stands, and for memory this side opened.
        std::optional<bool> isOpenedElsewhere() const;

    private:
        /// What `other` held, leaving it holding nothing. This holds nothing first.
        void take(SharedMemory& other) noexcept
        {
            mData = std::exchange(other.mData, nullptr);
            mSize = std::exchange(other.mSize, 0);
            mHandle = std::exchange(other.mHandle, nullptr);
            mMadeName = std::move(other.mMadeName);
            other.mMadeName.clear();
        }

        /// Unmaps the memory, where any is held, and holds nothing.
        void unmap() noexcept;

        void* mData = nullptr;
        std::size_t mSize = 0;

        /// The mapping, on a system that holds memory by a handle.
        void* mHandle = nullptr;

        /// The name this side made the memory under, on a system that keeps a name until it is given
        /// up: given up here where the other side never opened it. Empty otherwise.
        std::string mMadeName;
    };
}
