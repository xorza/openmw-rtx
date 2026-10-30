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
- **`umr` cannot read a wave here.** SteamOS's `umr` 1.0.10 has no register of Van Gogh's graphics
  block ("Cannot find a 'gfx' IP block in this ASIC", `mmSQ_CMD` not found), so `--profiler` and
  `--waves` halt nothing and read no shader; `--profiler` then wrote 2.7 GB of those errors a
  minute.
- **What the driver compiled, as text**: `--pipeline-code=<dir>` writes RADV's NIR, ACO IR and
  final code of every pipeline (160 compiled, 182 MB, from cold in a 2 s bench; the first writing
  kept one file a name, so the shadow filter's nine wrote one, which `PipelineCodeFolder` now numbers). This RADV has no LLVM,
  so the final code is ACO's own listing with registers assigned, not a disassembly. The `landing`
  with no sun, moons, sea or maps (`landing.txt`) is 5 312 instructions in 417 blocks: 3 513 vector, 1 667 scalar,
  105 global loads, 4 LDS operations and one `image_bvh64_intersect_ray`.
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

## What the gather's candidates and the pixel count cost

`balmora-mages-guild`, 10 s a leg, back to back after a 3 s leg that compiled the pipelines.

| Leg | frame median | p99 | landing | bounce | direct | trace |
|---|---|---|---|---|---|---|
| `--lamp-candidates=8` (default) | 60.23 | 120.52 | 18.91 | 9.51 | 5.30 | 5.08 |
| `--lamp-candidates=4` | 58.37 | 116.73 | 17.15 | 9.51 | 5.30 | 5.07 |
| `--lamp-candidates=1` | 55.37 | 110.76 | 14.29 | 9.51 | 5.30 | 5.07 |
| `--upscale=performance` (640x400 traced) | 36.22 | 71.97 | 10.65 | 5.36 | 3.13 | 2.97 |
| `--lamp-candidates=8`, again | 60.24 | 120.47 | 18.90 | 9.51 | 5.30 | 5.07 |

- **The run repeats to 0.01 ms** in every zone, so a difference of a tenth is a change.
- **The candidates cost `landing` alone, and less each as they grow**: from 1 to 4 they add 0.95 ms
  a candidate, from 4 to 8 0.44. 14.29 ms of `landing` stays at one candidate. `direct` does not
  move with the switch.
- **The ray stages follow the pixel count**: 640x400 is 0.563 of 853x533, and `landing` went to
  0.563 of itself, `bounce` 0.564, `trace` 0.585, `direct` 0.591. `upscale` went only to 0.84.
- **The p99 stays twice the median** in every leg.

## The device's counters a zone, `balmora-mages-guild`

`bench --counters --validation=on --seconds=3`, Arch's validation layer 1.4.363 beside the build,
which reported nothing. RADV offers 17 counters for the queue's family and reads 15 in one pass;
`VRAM write size` and `L2 cache hit ratio` were left out. The core clock read 1300 MHz through
this run, not 1600, and not because the manual clock let go: RADV puts the card in amdgpu's profiling
state while it reads counters, `profile_standard` by default, and `RADV_PROFILE_PSTATE=peak` gave
`profile_peak` at 1300 MHz as well (2026-10-31). A counters run is at 1300 MHz whatever is held.
Each the median over the zone's runs, eight
zones by share.

| Counter | landing | bounce | direct | trace | upscale | filter0 | clamp | panes |
|---|---|---|---|---|---|---|---|---|
| GPU active cycles | 28.89M | 14.18M | 8.15M | 7.88M | 5.04M | 4.64M | 4.27M | 2.92M |
| Waves | 14.34k | 14.47k | 14.47k | 14.47k | 42.77k | 7169 | 21.51k | 1023 |
| Instructions | 350.74M | 236.06M | 133.69M | 121.23M | 42.01M | 42.40M | 31.41M | 29.66M |
| VALU instructions | 175.07M | 126.08M | 71.69M | 66.43M | 33.71M | 28.94M | 22.74M | 15.71M |
| SALU instructions | 88.43M | 47.95M | 29.84M | 23.60M | 3.63M | 6.23M | 2.95M | 6.48M |
| VMEM load instructions | 2.58M | 1.84M | 916.04k | 981.50k | 937.91k | 314.90k | 295.10k | 237.73k |
| LDS instructions | 4.61M | 3.50M | 1.29M | 1.23M | 15.67k | 1.72M | 1.82M | 268.75k |
| VALU busy | 45.97 | 69.16 | 65.53 | 63.79 | 100.18 | 91.56 | 70.01 | 40.54 |
| SALU busy | 38.27 | 42.31 | 45.79 | 37.44 | 9.00 | 16.77 | 8.62 | 27.77 |
| VRAM read size | 144.07M | 70.49M | 47.08M | 7.68M | 52.82M | 11.62M | 41.08M | 2.30M |
| L0 cache hit ratio | 58.85 | 58.29 | 85.60 | 89.16 | 77.56 | 29.24 | 30.52 | 95.27 |
| L1 cache hit ratio | 53.77 | 62.52 | 66.91 | 47.80 | 42.32 | 55.20 | 54.39 | 68.74 |

- **`landing` waits, where the other three ray stages compute**: its VALU is busy 46 % of its
  cycles against 64–69 % for `bounce`, `direct` and `trace`, while it reads 144 MB from memory a
  run — twice `bounce`'s, nineteen times `trace`'s — at the lowest L0 and L1 hit ratios of the four.
  With six waves a SIMD (160 VGPRs), too few are resident to cover those misses.
- **A wave of `landing` runs 24 459 instructions**, 12 209 of them vector, against 16 314 and
  8 713 for `bounce`.
- **RADV's busy figures pass 100**: `upscale` read 100.18.

## On the desk's NVIDIA card

RTX 4090 Laptop, driver 615.78.08. The driver offers no `VK_KHR_performance_query` (the loader
answers that the ICD does not support enumerating its counters), and gives no internal
representation of any pipeline: a `--counters --pipeline-code` bench reads no counter, writes no
file, and logs the first.

## What `landing`'s rays cost (2026-10-31)

`omw release remote deck@192.168.0.31 bench --views=balmora-mages-guild,seyda-neen-ship --seconds=8
--size=1280x800`, the clock held at 1600 MHz. Each leg drops one of the landing's two rays in
`landing.comp` (its answer then reads as open), a change for the measurement alone.

| Leg | balmora frame | balmora landing | seyda frame | seyda landing |
|---|---|---|---|---|
| as built | 59.64 | 18.86 | 100.23 | 19.53 |
| no ambient ray (`VISIT_AMBIENT`) | 53.22 | 12.51 | 91.97 | 11.42 |
| no lamp ray (`GATHER_LAMP`) | 50.84 | 10.05 | 96.72 | 16.04 |

- **The two rays are four fifths of an interior's `landing`**: the ambient ray 6.35 ms and the lamp's
  8.81 of 18.86, which leaves about 3.7 ms for resolving the landing and gathering its light.
- **Out of doors the ambient ray is the larger**, 8.11 ms against 3.49, though it is drawn at
  `AMBIENT_EXTERIOR_RATE` there; a room traces it at every landing to `ROOM_FILL_REACH`, 140 units.
- **Every other zone stayed to the hundredth**, so the legs moved `landing` alone.

## What coherent landings would save (2026-10-31)

The same two places, the clock held at 1600 MHz, with one change for the measurement alone: the
bounce's direction drawn once an 8x4 tile (`bounceDraw` keyed by the tile's corner) and not once a
pixel, so a wave's 32 landings stand near each other, as landings sorted by where they hit would.

| | balmora landing | bounce | seyda landing | bounce |
|---|---|---|---|---|
| as built | 18.86 | 9.44 | 19.53 | 15.36 |
| a direction a tile | 15.43 | 6.61 | 16.65 | 10.09 |

- **Coherent landings save `landing` 3.4 and 2.9 ms**, 18 % and 15 %, every other zone but `bounce`
  standing still. A sort of the landings would reach that or less, and pay for itself out of it.
- **The bounce's saving is no sort's**: its rays leave neighbouring pixels in one direction here, which
  a sort after the bounce cannot give it.
- **What changed with it**, at 1300 MHz under `--counters`: `landing` read 104.62 MB against 144.07,
  at an L0 hit ratio of 72.49 against 58.85, and its VALU stayed near half busy (47.61 against 45.97).

## The p99 that was the offscreen driver's (2026-10-31)

Every place's p99 on the Deck was twice its median because one frame in 31 waited a whole frame
in `present`: under SDL's offscreen driver Mesa's headless swapchain blocked `vkQueuePresentKHR`
for 58–60 ms, timed around the call. Moving the presenter's wait for an image's last blit to a
fence per acquire slot only moved the stall into `vkQueuePresentKHR`, so it is not the presenter's.
A hidden window on the Deck's own Wayland session presented 480 frames with no such wait.

| balmora-mages-guild | frame median | p99 | 1 % low fps |
|---|---|---|---|
| offscreen driver | 59.62 | 120.53 | 8.3 |
| hidden window on the session | 59.63 | 66.84 | 15.0 |

`omw remote` runs on the session where the machine has one. The tables above this section that
quote a Deck p99 carry the offscreen driver's stall.
