# The host-written window on Windows without resizable BAR

Gathered 2026-11-09 from issue #49 and its replies, against `rtx-v0.4.5` (`69ca79e6ab`). A desktop
RTX 4090 beside an i9-14900K's UHD Graphics 770, Windows 11, NVIDIA 621.14, monitors on the 4090.
The game ended at start with `Ray tracing could not start: vmaAllocateMemory failed:
VK_ERROR_OUT_OF_DEVICE_MEMORY (-2)`. The same machine runs the game under Ubuntu 26.04.

## Where it stopped

- 0.32 s after `Ray tracing: upscale performance` and before `Ray tracing on <device>`: inside
  `VulkanRenderer`'s constructor. The full constructor took 1.15 s on a Windows NVIDIA laptop in #40.
- Every `MemoryAllocator::take` path reports the same name (`checkAllocated`), so the message does
  not say which of a buffer, an image or the arena failed, nor its size, heap, type or use.
- The chosen device is only a crash annotation (`Crash::annotate`), and a fatal error writes no dump,
  so the log does not name it. `PhysicalDevice::select` lists rejected devices only where none
  qualifies.

## Not the cause

- **The wrong device.** The UHD 770 has no ray tracing and fails selection on its extensions, so the
  4090 was chosen.
- **One allocation past the driver's limit.** NVIDIA's Windows driver states about 4 GB for one
  allocation. The frame's targets, measured on a high-end NVIDIA laptop GPU under Linux (driver
  615.78.08, release build of `736479fb8b`), take 1331 MiB at 3840x2160 and 2479 MiB at 7680x2160 in
  the performance mode, the arena a part of that.

## The cause

The reporter's `vulkaninfo` on Windows:

| heap | size | budget | usage | flags |
|---|---|---|---|---|
| 0 | 23.58 GiB | 22.83 GiB | 0 | device-local |
| 1 | 47.77 GiB | 47.02 GiB | 272 KiB | — |
| 2 | 214.00 MiB | 0 | 214.00 MiB | device-local |

- Heap 2 is the resizable BAR window at its 256 MiB size, so resizable BAR is off. The driver states
  214 MiB of it, gives the process a budget of nought, and reports all of it in use, in a process
  that made no allocation.
- `BufferKind::HostWritten` (`DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT`) came only from heap 2.
  The constructor makes host-written buffers of a few kilobytes (FSR's constants, the ripple
  impulses, the shader binding tables), and the first failed. The library tries an eighth of the heap
  as a block, smaller blocks, then the buffer alone; the driver refuses each.
- `PhysicalDevice::profileOf` refused a device with no host-written memory type, and asked nothing of
  the type's heap or budget, so the device passed selection.
- On Windows the driver gives the window whole where resizable BAR is on: a Windows NVIDIA laptop in
  #40 reported `host-written: 5994 MiB`, its device-local heap.
- **Confirmed**: with resizable BAR turned on in the BIOS, the reporter's game started under Windows 11.

## Why Ubuntu ran

Inferred, not measured on that machine. The BIOS setting is the same under both systems, so the
window is 256 MiB there too unless the Linux driver resized BAR1 itself (`NVreg_EnableResizableBar`,
since 530.30.02). Either way the Linux driver gives the process room in it: the window budget under
Linux was about 246 MiB on an RTX 20 (`2026-10-22-host-written-window.md`), and the world's tables
take 184 MiB of blocks at the room. Under Windows the 214 MiB the driver states is all reported in
use before the game asks. Unknown: whether that is every Windows NVIDIA card without resizable BAR,
or something on this machine filling the window (its loader lists the GOG Galaxy and Steam overlays).
`nvidia-smi -q` under Ubuntu, its `BAR1 Memory Usage` total, would tell the two Linux cases apart.

## What it meant

The room (`sWorldRoom`) and the refused staged path rested on 246 MiB of window, which Windows can
give at nought, and Turing has no resizable BAR on any system. What the fork does about it is
`architecture.md`'s "Host writes".

## Sources

- Issue #49 and its replies; issue #40's log.
- [FFmpeg: disable host transfers if ReBAR is disabled](https://ffmpeg.org/pipermail/ffmpeg-cvslog/2025-June/149030.html)
- [NVIDIA forum: Vulkan memory import size truncated on Windows](https://forums.developer.nvidia.com/t/vulkan-memory-import-size-truncated-on-windows/331810)
- [NVIDIA open kernel modules: BAR1 resize behind `NVreg_EnableResizableBar`](https://github.com/NVIDIA/open-gpu-kernel-modules/pull/3)
- [Phoronix: reading the BAR1 total to tell whether resizable BAR is active](https://www.phoronix.com/review/rtx3080-linux-rebar)
- [VK_EXT_memory_budget](https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_EXT_memory_budget.html)
