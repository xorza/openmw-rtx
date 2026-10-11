#include "sharedmemory.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

#include <components/misc/windows.hpp>

namespace Platform
{
    namespace
    {
        /// In the session's own namespace, which a process needs no privilege to make a name in.
        std::string systemName(std::string_view name)
        {
            return "Local\\" + std::string(name);
        }
    }

    SharedMemory SharedMemory::create(std::string_view name, std::size_t size)
    {
        SharedMemory memory;
        const std::uint64_t bytes = size;
        const HANDLE mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
            static_cast<DWORD>(bytes >> 32), static_cast<DWORD>(bytes), systemName(name).c_str());
        if (mapping == nullptr)
            return memory;

        void* const view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
        if (view == nullptr)
        {
            CloseHandle(mapping);
            return memory;
        }

        // A name an earlier process of this session left is opened as it stood, which a fresh one is
        // not: noughts either way, as `create` says.
        std::memset(view, 0, size);

        memory.mData = view;
        memory.mSize = size;
        memory.mHandle = mapping;
        return memory;
    }

    SharedMemory SharedMemory::open(std::string_view name, std::size_t size)
    {
        // The name lives as long as a handle to it does, so there is nothing to give up here.
        SharedMemory memory;
        const HANDLE mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, systemName(name).c_str());
        if (mapping == nullptr)
            return memory;

        void* const view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
        if (view == nullptr)
        {
            CloseHandle(mapping);
            return memory;
        }

        memory.mData = view;
        memory.mSize = size;
        memory.mHandle = mapping;
        return memory;
    }

    std::optional<bool> SharedMemory::isOpenedElsewhere() const
    {
        // A mapping's name stands for as long as any handle does, so it says nothing of who opened it.
        return std::nullopt;
    }

    void SharedMemory::unmap() noexcept
    {
        if (mData == nullptr)
            return;

        UnmapViewOfFile(std::exchange(mData, nullptr));
        CloseHandle(std::exchange(mHandle, nullptr));
        mSize = 0;
    }
}
