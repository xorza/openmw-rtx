#include "sharedmemory.hpp"

#include <cstddef>
#include <cstdint>
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

    SharedMemory::~SharedMemory()
    {
        if (mData == nullptr)
            return;

        UnmapViewOfFile(mData);
        CloseHandle(mHandle);
    }
}
