# The fog's rays a turn each, a kernel's shared memory, and three drivers timed — 2026-11-15

A high-end NVIDIA laptop GPU (the desk, 1920x1080, three legs a side); an AMD handheld under RADV, its clock held at
1600 MHz (1280x800, one leg a side); a low-end AMD integrated GPU under AMD's Windows driver (1280x720, two legs a side,
in its logged-on session). Vanilla, the Fullrest Repack and the PBR stack, one binary a host reading a desk-built
shader set a side (`--shader-source`). Zones are medians in ms.

## How it was read

- **A desk-built shader set runs as a host's own**: on the AMD iGPU its shipped modules, its own build's and the desk's
  stood within 0.3% a zone. The modules with their source carry the building host's paths, so every side of a leg is a
  desk-built set.
- **AMD's Windows driver's registers are read in the logged-on session**: over SSH a run ends at its swapchain.
- **The handheld's Mesa against the drm-shim's**: 561 of 574 pipelines hold the same figures.

## `2026-11-15-radv-wave-tiers.md` on AMD's Windows driver

Over the commit that reads a mod's maps, the frame on the AMD iGPU moved −3.8 to +0.6% (the three hosts' table is in
`.notes/PERF.md`). Its stages took the change unevenly: `direct` −3.9 to −7.0% under a sky but +3.2% in the vanilla
guild, its tuple with no sky a tier up (96 → 109); `landing` and `panes` up 1–4% on vanilla, where every `panes`
tuple's registers rose; `bounce` and `trace` down. Its tiers do not follow RADV's.

## What a kernel's shared memory is worth on RADV

- **RADV caps a kernel's waves by its shared memory as by its registers**: a workgroup's bytes rounded up to 1024, and
  65536 over that many workgroups a compute unit — 11264 bytes run 6 waves a SIMD, 9216 and 8192 run 8, 7168 and 6144
  run 10, 5120 run 12, 4096 run 16. The registers the driver reports are then that many waves' budget, not the demand
  (the clamp reports 168 on a demand of 53). A stage's ray query stack is 2048 bytes and caps nothing.
- **The clamp's two more waves bought nothing**: its tile packed from 24 bytes a texel to 8 and decoded at the tap, 6
  waves to 8, ran flat to +2.5% on the handheld, +2.1 to +8.0% on the desk and +1.1 to +2.1% on the AMD iGPU but its
  guild. Its short history's taps left a loop, the normals alone packed and no ring luminances were each no better.

## The fog's scatter launch

Its four rays (to the lamp, the sun, the moon and the ambient) were four traversals inlined, each asked before the
first was traced: 43 of its 94 registers on RADV carried untouched through the first traversal. Asked a turn each at
one traversal site, the lamps' light written ahead of the rays and the history read past them: 80–96 registers where it
held 96–112, a third fewer instructions. The `air` zone:

- **The desk** −6.0 to −6.9% by day and in a room, +0.9 to +1.7% at dawn (0.004–0.006 ms).
- **The AMD iGPU** −0.9 to −6.2% everywhere.
- **The handheld** −2.8% and −4.3% at dawn, where it gained a tier; +1.2 to +2.0% by day and in a room (0.02 to 0.13
  ms), where the history read past the rays costs it.

Read ahead of the rays the history ran the handheld 0.06 ms shorter and the other two drivers 6% longer, so it is
read past them. Skipping the moon's turn with no moons moved nothing. Neither NVIDIA's nor AMD's Windows driver says
what a launch holds.

## The layers' stage

It resolved every hit of its walk to learn whether the hit is a layer. Asked of the opacity alone first
(`layerOpacityOf`) and only a layer resolved: on RADV `panes` with maps holds 192 registers for 256, five waves for
four. `panes` in the guilds: the handheld −1.9%, −3.5% and −17.0% (the Repack's), the desk −2.3 to −4.8%, the AMD iGPU
0 to −6.9%; out of doors within 0.006 ms. No picture moved.
