#pragma once

#include <vulkan/vulkan_core.h>

namespace Rtx::Testing
{
    /// An RTX 2060's memory as the Vulkan Hardware Database reports it: report 46422, driver
    /// 590.48.01 on Linux. Three heaps — six gigabytes of video memory, twenty-five of system memory,
    /// and a 246 MiB window the host writes into, listed last.
    inline VkPhysicalDeviceMemoryProperties turingMemory()
    {
        VkPhysicalDeviceMemoryProperties memory{};
        memory.memoryHeapCount = 3;
        memory.memoryHeaps[0] = VkMemoryHeap{ 6442450944ull, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT };
        memory.memoryHeaps[1] = VkMemoryHeap{ 25177847808ull, 0 };
        memory.memoryHeaps[2] = VkMemoryHeap{ 257949696ull, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT };

        memory.memoryTypeCount = 5;
        memory.memoryTypes[0] = VkMemoryType{ 0, 1 };
        memory.memoryTypes[1] = VkMemoryType{ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0 };
        memory.memoryTypes[2]
            = VkMemoryType{ VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 1 };
        memory.memoryTypes[3] = VkMemoryType{ VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
            1 };
        memory.memoryTypes[4] = VkMemoryType{ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            2 };
        return memory;
    }

    /// This box's RTX 4090 Laptop, as `vulkaninfo` reports it under resizable BAR: one heap of
    /// video memory, host-visible throughout, so the window the host writes into is the whole of it.
    inline VkPhysicalDeviceMemoryProperties adaMemory()
    {
        VkPhysicalDeviceMemoryProperties memory{};
        memory.memoryHeapCount = 2;
        memory.memoryHeaps[0] = VkMemoryHeap{ 17171480576ull, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT };
        memory.memoryHeaps[1] = VkMemoryHeap{ 50259394560ull, 0 };

        memory.memoryTypeCount = 5;
        memory.memoryTypes[0] = VkMemoryType{ 0, 1 };
        memory.memoryTypes[1] = VkMemoryType{ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0 };
        memory.memoryTypes[2]
            = VkMemoryType{ VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 1 };
        memory.memoryTypes[3] = VkMemoryType{ VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
            1 };
        memory.memoryTypes[4] = VkMemoryType{ VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            0 };
        return memory;
    }

    /// An RX 6800 under RADV, as `vulkaninfo` reports it on Mesa's drm-shim for `NAVI21`: the
    /// 256 MiB window last among the heaps, a second type of each kind that only buffers take (RADV's
    /// 32-bit ones), and AMD's device-coherent types after them, which no request may be placed in.
    inline VkPhysicalDeviceMemoryProperties rdna2Memory()
    {
        constexpr VkMemoryPropertyFlags local = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        constexpr VkMemoryPropertyFlags visible
            = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        constexpr VkMemoryPropertyFlags cached = visible | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        constexpr VkMemoryPropertyFlags coherent
            = VK_MEMORY_PROPERTY_DEVICE_COHERENT_BIT_AMD | VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD;

        VkPhysicalDeviceMemoryProperties memory{};
        memory.memoryHeapCount = 3;
        memory.memoryHeaps[0] = VkMemoryHeap{ 16911433728ull, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT };
        memory.memoryHeaps[1] = VkMemoryHeap{ 16746784768ull, 0 };
        memory.memoryHeaps[2] = VkMemoryHeap{ 268435456ull, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT };

        memory.memoryTypeCount = 11;
        memory.memoryTypes[0] = VkMemoryType{ local, 0 };
        memory.memoryTypes[1] = VkMemoryType{ local, 0 };
        memory.memoryTypes[2] = VkMemoryType{ visible, 1 };
        memory.memoryTypes[3] = VkMemoryType{ local | visible, 2 };
        memory.memoryTypes[4] = VkMemoryType{ local | visible, 2 };
        memory.memoryTypes[5] = VkMemoryType{ cached, 1 };
        memory.memoryTypes[6] = VkMemoryType{ cached, 1 };
        memory.memoryTypes[7] = VkMemoryType{ local | coherent, 0 };
        memory.memoryTypes[8] = VkMemoryType{ visible | coherent, 1 };
        memory.memoryTypes[9] = VkMemoryType{ local | visible | coherent, 2 };
        memory.memoryTypes[10] = VkMemoryType{ cached | coherent, 1 };
        return memory;
    }
}
