# The trace's stages on an AMD handheld — 2026-10-30

An AMD handheld (RDNA 2, 8 CUs), RADV, the core clock held at 1600 MHz through `pp_od_clk_voltage` under the default
15 W cap. Vanilla, release, `bench --window=false --size=1280x800` (853x533 traced). The stages measured here have
been rewritten since; what stands is what the measurements taught.

## Tools on the handheld

- **RADV writes what it compiled as text**: `--pipeline-code=<dir>` gives each pipeline's NIR, ACO IR and final code.
  This RADV has no LLVM, so the final code is ACO's listing with registers assigned, not a disassembly.
- **An RGP capture of a whole frame needs a present**: a window on the handheld's Wayland session with
  `MESA_VK_TRACE=rgp MESA_VK_TRACE_FRAME=<n> RADV_THREAD_TRACE_BUFFER_SIZE=268435456`. Headless, a per-submit capture
  drops the frame's large submits at the default 32 MiB. A capture adds about 18% to a dispatch.
- **`umr` cannot read a wave** on this GPU (no register of its graphics block), so its profiler and wave dumps give
  nothing.
- **A counters run is at 1300 MHz whatever clock is held**: RADV puts the card in amdgpu's profiling state while it
  reads counters, `RADV_PROFILE_PSTATE=peak` included.
- **NVIDIA's desk driver offers no `VK_KHR_performance_query`** and no pipeline's internal representation.

## What the stages were bound by

Under RGP at `balmora-mages-guild` the GPU was never idle and the costliest 5% of events took 86% of the frame.

- **`bounce` and `direct` were ALU-bound in the traversal**, which RDNA 2 runs in the shader around each node test:
  VALU 67–70% busy, 73–93 vector instructions per BVH instruction.
- **`landing` waited**: VALU 46% busy, twice `bounce`'s memory read at the lowest cache hit ratios of the four, with
  too few waves resident (6 of 16 at 160 registers) to cover the misses. Its two rays were four fifths of its time.
- **The ray stages follow the pixel count** (0.56–0.59 of themselves at 0.563 of the pixels); the upscaler does not.

## What came of it

- **A stage works out its light before its rays and traces them in a fixed order**, no record live across a
  traversal: `landing` 160–256 → 128–144 registers and 6 → 8 waves, `direct` 128–192 → 112–128; the handheld's frame
  −10.3% at the ship, −12.8% at dawn, −7.5% in the guild, the desk's −2.2 to −6.8%, every other zone within 0.2%. A
  layer's light the same way took `panes` 256 → 192 registers.
- **Coherent landings were bounded and refused**: the bounce's direction drawn once a tile, as a sort by where landings
  hit would group them, saved `landing` 15–18% on the handheld; the sort itself, spiked later, regressed the desk.
- **The p99 at twice the median was SDL's offscreen driver**: Mesa's headless swapchain blocked `vkQueuePresentKHR` for
  58–60 ms one frame in 31. On a hidden window of the handheld's session the p99 fell from 120.5 to 66.8 ms at the
  guild, so `omw remote` runs there.
