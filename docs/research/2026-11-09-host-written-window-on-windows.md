# The host-written window on Windows without resizable BAR — 2026-11-09

From issue #49 against `rtx-v0.4.5`: a desktop RTX 4090 beside an i9-14900K's UHD Graphics 770, Windows 11, NVIDIA
621.14. The game ended at start with `vmaAllocateMemory failed: VK_ERROR_OUT_OF_DEVICE_MEMORY`; the same machine ran it
under Ubuntu.

## The cause

The reporter's `vulkaninfo` on Windows:

| heap | size | budget | usage | flags |
|---|---|---|---|---|
| 0 | 23.58 GiB | 22.83 GiB | 0 | device-local |
| 1 | 47.77 GiB | 47.02 GiB | 272 KiB | — |
| 2 | 214.00 MiB | 0 | 214.00 MiB | device-local |

- Heap 2 is the resizable BAR window at 256 MiB, so resizable BAR is off. The driver gives the process a budget of
  nought in it and reports all of it in use, in a process that made no allocation.
- Host-written buffers (`DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT`) came only from heap 2, and the first of a few
  kilobytes failed; device selection had asked nothing of that heap's budget.
- With resizable BAR on, NVIDIA's Windows driver gives the window whole (a laptop in #40 reported 5,994 MiB).
- **Confirmed**: with resizable BAR turned on in the BIOS, the reporter's game started.

Not the cause: the wrong device (the UHD 770 fails selection), nor one allocation past the driver's limit (the frame's
targets take 1,331 MiB at 3840x2160).

## Why Ubuntu ran

Inferred: the Linux driver gives the process room in the window (about 246 MiB on an RTX 20,
`2026-10-22-host-written-window.md`), or resizes BAR1 itself (`NVreg_EnableResizableBar`). Unknown whether every Windows
NVIDIA card without resizable BAR does this.

## What it meant

Host writes rested on a window Windows can give at nought, and Turing has no resizable BAR on any system: host writes
are staged on every card (`2026-11-10-host-writes-on-three-machines.md`).

## Sources

- Issues #49 and #40.
- [FFmpeg: disable host transfers if ReBAR is
  disabled](https://ffmpeg.org/pipermail/ffmpeg-cvslog/2025-June/149030.html)
- [NVIDIA open kernel modules: BAR1 resize behind
  `NVreg_EnableResizableBar`](https://github.com/NVIDIA/open-gpu-kernel-modules/pull/3)
- [VK_EXT_memory_budget](https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_EXT_memory_budget.html)
