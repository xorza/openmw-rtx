# The trace's stages on a Steam Deck — 2026-10-30

Steam Deck LCD: AMD Custom APU 0405 (Van Gogh, RDNA 2, 8 CUs), RADV Mesa 26.2.0-devel
(git-5c65d4fc31), SteamOS 3.9.5 (build 20261023.100), kernel 7.2.7-valve2. The core clock held at
1600 MHz through `pp_od_clk_voltage` (`s 0 1600`, `s 1 1600`, `power_dpm_force_performance_level`
manual) under the default 15 W cap; 6144 MiB device-local heap. Vanilla Morrowind, the release
flavour at `1a6fc11730`, `bench --window=false --size=1280x800` (853x533 traced, upscale quality)
unless named. Zones are medians in ms.

## How the harness runs on the Deck

- **Built in the release-base container**, `CI/release-base.Dockerfile`, with the container's
  `build-release` mounted from `build-container/release`: 5 min 19 s from nothing. A build from the
  desk's Arch does not start — Arch's FFmpeg 8 binds `sinh` and `cosh` at `GLIBC_2.44`, and the
  Deck has 2.43 — and 28 of its libraries have no soname on SteamOS.
- **The libraries the Deck lacks beside it**: 59 sonames and OSG's nine `USED_OSG_PLUGINS`, 135 MB,
  copied out of the container where `ldconfig -p` on the Deck names no such soname, read through
  `LD_LIBRARY_PATH` and `OSG_LIBRARY_PATH`. glibc, `libstdc++`, the Vulkan loader (1.4.350) and RADV
  stay the Deck's.
- **At the desk's own path**, `/home/xxorza/Projects/openmw/build-release`: the harness's folder,
  views and suites are compiled-in absolute paths (`apps/rtxtool/harnessfolder.cpp`).
- **`SDL_VIDEO_DRIVER=offscreen` over SSH.** `--window=false` still initializes SDL's video, which
  fails with no display ("No available video device"); `dummy` has no Vulkan.
- **An RGP capture of one whole frame needs a present**: a window on the Deck's desktop
  (`WAYLAND_DISPLAY=wayland-0`, `SDL_VIDEO_DRIVER=wayland`, `--window=true`) with
  `MESA_VK_TRACE=rgp MESA_VK_TRACE_FRAME=300 RADV_THREAD_TRACE_BUFFER_SIZE=268435456`. Headless,
  `MESA_VK_TRACE_PER_SUBMIT=1` writes every submit, and at the default 32 MiB the frame's large
  submits were missing — every third number. A frame at `balmora-mages-guild` is 292 MB at 256 MiB
  and at 1 GiB alike, and the capture adds about 18 % to a dispatch.

## The default suite

| Place | frame median | p99 | fps (1 % low) | landing | trace | bounce | direct | air |
|---|---|---|---|---|---|---|---|---|
| `seyda-neen-ship` | 97.6 | 198.6 | 10.2 (5.0) | 19.40 | 16.37 | 15.04 | 12.69 | 7.65 |
| `seyda-neen-ship-dawn` | 123.5 | 256.4 | 8.1 (3.9) | 22.91 | 16.62 | 15.43 | 34.44 | 13.37 |
| `balmora-mages-guild` | 59.9 | 119.6 | 16.7 (8.4) | 19.17 | 5.07 | 9.17 | 5.28 | 1.24 |

- **GPU-bound everywhere**: `wait` 86, 112 and 55 ms of those frames, `update` 6.1, 6.0 and 2.5.
- **The clock never moved**: 1600 MHz at every reading (0.5 s apart), 77–80 °C, 13.0 W mean.
- **The p99 is twice the median at every place**, a frame in about a hundred waiting one more
  frame in `present` (p99 101.8–127.2 ms) with no swapchain.
- **`landing` is about 19 ms in the interior and the exterior alike**, where every other ray stage
  follows the scene.

## What the driver compiled

RADV's statistics for the four costliest stages, wave32 (`RADV_DEBUG` is not needed; the build logs
them):

| Kernel | VGPRs | waves a SIMD | spilled SGPRs | spilled VGPRs |
|---|---|---|---|---|
| `bounce` (no maps) | 96 | 10 | 0 | 0 |
| `bounce` (maps) | 128 | 8 | 0 | 0 |
| `direct` | 128–192 | 5–8 | 0 | 0 |
| `landing` | 160–240 | 4–6 | 0–57 | 0 |
| `panes sun moons sea maps` | 256 | 4 | 27 | 13 |

## One frame under RGP, `balmora-mages-guild`

73.1 ms (13.7 Hz), 382 events, GPU idle 0.01 %; the most expensive 5 % of events take 86 % of
the frame.

| Event | Dispatch | ms | Share |
|---|---|---|---|
| 282 `landing` | indirect (1024, 14, 1) | 22.65 | 31 % |
| 280 `bounce` | (108, 134, 1) | 11.75 | 16 % |
| 276 `direct` | (108, 134, 1) | 6.44 | 9 % |
| 274 `trace` | (108, 134, 1) | 6.19 | 8 % |
| 300 `filter0` | (107, 67, 1) | 3.53 | 5 % |
| 278 `panes` | indirect (1023, 1, 1) | 2.26 | 3 % |

- **`landing` lands every pixel in an interior**: 14 336 waves of 32 lanes, 458 752 threads, the
  whole 853x533 grid. 160 VGPRs, 108 SGPRs (128 allocated), 2048 bytes of LDS a group, nothing to
  scratch; the VGPRs hold it at 6 of 16 waves a SIMD, and 16 fewer would make 7. A wave runs
  151 µs on average. Its occupancy stays near 25–30 % through the dispatch, its ray-box rate is
  the lowest of the four ray stages, and the memory unit is rarely stalled. Instruction timing
  analyzed no wavefronts of it, at 256 MiB and at 1 GiB of trace buffer alike.
- **`bounce` and `direct` are ALU-bound in the traversal**, which RDNA 2 runs in the shader around
  each node test:

  | | `bounce` (280) | `direct` (276) |
  |---|---|---|
  | waves analyzed | 902 of 14 472 | 920 of 14 472 |
  | VALU / SALU / LDS / VMEM utilization | 69.5 / 21.3 / 9.8 / 1.3 % | 66.9 / 23.2 / 6.3 / 1.9 % |
  | VALU instructions a wave | 9 456 | 5 093 |
  | RAYTRACE instructions a wave | 130 | 55 |
  | VALU per BVH instruction | 73 | 93 |
  | waves a SIMD (VGPRs) | 10 (96) | 8 (128) |
  | wave duration | 67 000–318 000 clk | 15 900–224 500 clk |
  | branches taken | 31.8 % of 1 697 611 | 37.4 % of 803 404 |

- **`bounce`'s prologue is a chain of dependent reads**, once a wave: the G-buffer's `image_load`
  (456–502 clk at its wait), a 96-byte record in six `global_load_dwordx4` (329), two more levels
  (258, 250) and the triangle's corners in three `global_load_dwordx3` (231, 258) — about
  2 000–2 500 clk, 1–3 % of a wave, each wait 0.14–0.30 % of the kernel.
- **The flags are not the cost**: the geometry is built opaque (`structurebuild.cpp`), only cutout,
  translucent and additive rows force it otherwise (`SceneAcceleration::placeRow`), and a shadow
  ray terminates on its first hit unless its penumbra reads the nearest occluder
  (`passageAsk`). The interior holds 131 cutouts.
