# Rest reuse and device memory budgets — 2026-11-16

## Rest reuse over the tours

`RestReuse` (Mattson et al. 1970's stack distance) counts, for each content woken from rest, the
least-recently-rested cache that would still have held it, so one tour gives every cache size at once. The table gives
the share of the woken bytes each cache size kept. The desk's tours ran before static meshes rested, so they count
textures alone; the handheld's ran on a tree taken while that change was in progress, so whether its meshes rested is
not recorded.

| Tour | Woke | 64 MiB | 128 MiB | 256 MiB | 512 MiB | 1 GiB | 2 GiB | 4 GiB | 8 GiB |
|---|---|---|---|---|---|---|---|---|---|
| High-end NVIDIA laptop GPU, repack | 34.3 GiB | 20.0% | 32.9% | 49.8% | 72.0% | 92.5% | 97.5% | 100% | 100% |
| High-end NVIDIA laptop GPU, PBR replacers | 43.8 GiB | 9.4% | 17.0% | 26.8% | 45.9% | 67.2% | 88.4% | 95.5% | 100% |
| Handheld AMD APU (RADV), repack | 33.3 GiB | 20.2% | 33.3% | 50.3% | 72.6% | 93.2% | 98.2% | 100% | 100% |
| Handheld AMD APU (RADV), PBR replacers | 25.0 GiB | 12.8% | 24.4% | 42.3% | 67.5% | 91.6% | 100% | 100% | 100% |

The handheld's video heap is 6,144 MiB of system memory; it ended an earlier repack tour, before the rest was bounded,
with 2,981 MiB of its 3,545 MiB of textures resting.

## What a device's budget is

- **Vulkan.** `VkPhysicalDeviceMemoryBudgetPropertiesEXT::heapBudget` is "a guideline for how much total memory the
  current process can use at any given time", which "may change based on other activity in the system";
  `heapUsage` is "an estimate of how much memory the current process has allocated from that heap"
  ([reference](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceMemoryBudgetPropertiesEXT.html)).
- **RADV** computes `heap_budget = heap_size - global_heap_usage + app_heap_usage`: what is free plus what the process
  holds, with no margin
  ([patch](https://lists.freedesktop.org/archives/mesa-dev/2019-June/220081.html),
  [heap fix](https://lists.freedesktop.org/archives/mesa-commit/2021-May/118852.html)). NVIDIA's Linux formula is not
  published.
- **Windows.** The budget is the OS's and "can fluctuate noticeably as background processes wake-up and sleep; and
  fluctuate dramatically when the user switches away"; a process over it "will be intermittently frozen … and/or the
  creation APIs will return failure"; even simple applications should "destroy unused API objects roughly once a
  frame" ([D3D12 residency](https://learn.microsoft.com/en-us/windows/win32/direct3d12/residency),
  [DXGI_QUERY_VIDEO_MEMORY_INFO](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/ns-dxgi1_4-dxgi_query_video_memory_info)).
- **VMA** reads the budget inside `vmaSetCurrentFrameIndex`, to be called every frame; without the extension it
  assumes 80% of each heap; `VMA_ALLOCATION_CREATE_WITHIN_BUDGET_BIT` is for non-essential resources
  ([staying within budget](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/staying_within_budget.html)).
- **Dawn** caps its budget at 95% of the OS's, less an external reservation, and evicts least recently used heaps only
  when making one resident would pass it
  ([ResidencyManagerD3D12](https://dawn.googlesource.com/dawn/+/f56f19059b12dd5dd358937b5af3d6394762c45e/src/dawn_native/d3d12/ResidencyManagerD3D12.cpp)).
- **Unreal** sizes its texture streaming pool as a percentage of video memory (`r.Streaming.PoolSizeVRAMPercentage`,
  70 by default) with a clamp, or as a fixed size
  ([configuration](https://dev.epicgames.com/documentation/unreal-engine/texture-streaming-configuration-in-unreal-engine)).
- **Metal** offers `recommendedMaxWorkingSetSize` and purgeable state: a volatile resource may be discarded by the
  system under pressure ([MTLPurgeableState](https://developer.apple.com/documentation/metal/mtlpurgeablestate.md)).
- **Priorities on Vulkan.** `VK_EXT_pageable_device_local_memory` lets `vkSetDeviceMemoryPriorityEXT` change an
  allocation's priority after it is made, so the driver demotes low priority first
  ([reference](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_pageable_device_local_memory.html));
  RADV does not implement it ([Mesa issue 9224](https://gitlab.freedesktop.org/mesa/mesa/-/issues/9224)).
