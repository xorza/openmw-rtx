# The fog's rays a turn each, a kernel's shared memory, and three drivers timed: 2026-11-15

A high-end NVIDIA laptop GPU, driver 620.70.06; an AMD handheld's integrated GPU (RDNA 2, 8 CUs) under RADV, Mesa
26.2.0-devel, its clock held at 1600 MHz; and a low-end AMD integrated GPU (RDNA 2) under AMD's Windows driver
26.8.1. Vanilla Morrowind, the Fullrest Repack and the PBR stack; the release harness at `f3793d01bf`, one binary a
host reading a desk-built shader set a side (`--shader-source`). Desk: `bench --window=false --seconds=10` at
1920x1080, three legs a side in turn. Handheld: `bench --window=false --size=1280x800 --frames=300`, one leg a
side. AMD iGPU: `bench --window=false --size=1280x720 --frames=200`, two legs a side in turn, in its logged-on
session. Zones are medians in ms.

## How it was read

- **A desk-built set runs as a host's own**: on the AMD iGPU the shipped modules, its own build's with their
  source and the desk's stood within 0.3% a zone over two rounds. The modules with their source differ by the
  host that built them, the paths in them, so every side of a leg is a desk-built set.
- **AMD's Windows driver's registers are read in the logged-on session**: over SSH a run ends at its swapchain,
  with every tuple logged where the driver's cache holds the set and 82 of 574 pipelines where it does not.
- **The handheld's Mesa against the shim's**: 561 of 574 pipelines hold the same registers, shared memory and
  scratch; the water's stage with a relief and a mod's maps holds 160 registers there for the shim's 144, in four
  tuples, and `bounce sun moons dark` 80 for 96.

## The stages' change of `2026-11-15-radv-wave-tiers.md` on three drivers

Over the commit that reads a mod's maps; the frame, before against after:

| content | place | handheld | desk | AMD iGPU |
|---|---|---|---|---|
| vanilla | ship | 86.89 → 82.04 (−5.6%) | 5.84 → 5.64 (−3.4%) | −1.0% |
| vanilla | ship-dawn | 101.13 → 94.98 (−6.1%) | 6.31 → 6.02 (−4.6%) | −1.8% |
| vanilla | guild | 54.96 → 53.07 (−3.4%) | 5.09 → 5.08 | −1.0% |
| PBR | ship | 99.83 → 97.74 (−2.1%) | 6.78 → 6.73 (−0.8%) | 0.0% |
| PBR | ship-dawn | 118.13 → 117.57 (−0.5%) | 7.30 → 7.22 (−1.1%) | −1.6% |
| PBR | guild | 66.14 → 64.53 (−2.4%) | 6.22 → 6.25 (+0.5%) | −2.7% |
| Repack | ship | 119.26 → 114.17 (−4.3%) | 7.31 → 7.34 (+0.5%) | +0.6% |
| Repack | ship-dawn | 134.74 → 130.71 (−3.0%) | 7.69 → 7.70 | −1.3% |
| Repack | guild | 82.43 → 76.40 (−7.3%) | 6.38 → 6.32 (−0.9%) | −3.8% |

On AMD's Windows driver, which that note never ran:

- **`direct`** −3.9 to −7.0% under a sky and +3.2% in the vanilla guild: its tuple with no sky holds 109
  registers for 96, nine waves for ten. With the direct channel stored past the rays again that tuple and the
  sea's return to 96, and thirty tuples with the maps or the moons go a tier up (112 to 127 or 128, 128 to 144).
- **`landing`** +1.0 to +1.8% on vanilla, +4.1% at the PBR stack's ship and +3.7% at the Repack's, at the registers
  it held; −3.0 to −7.6% at the Repack's dawn and in the mapped contents' guilds. `landing dark` 96 to 112.
- **`panes`** +2.2 to +4.4% on vanilla: every tuple's registers up, `panes sun sea` 231 to 247, the most 256 for
  254, four waves both.
- **`bounce`** −1.9 to −7.6%, `trace` −1.3 to −3.7% but the Repack's guild, +1.5%.

## What a kernel's shared memory is worth on RADV

- **RADV caps a kernel's waves by its shared memory as by its registers**: of the shim's listing, a workgroup's
  declared bytes rounded up to 1024, and 65536 over that many workgroups a compute unit: 11264 bytes run 6 waves a
  SIMD, 9216 and 8192 run 8, 7168 and 6144 run 10, 5120 run 12, 4096 run 16. The registers the driver reports are
  then the budget of that many waves and not the demand: the clamp reports 168 on a demand of 53. A stage's ray
  query stack is 2048 bytes and caps nothing.
- **The clamp's two waves bought nothing.** Its tile holding each surface as its channel packs it, eight bytes a
  texel for twenty-four, decoded where a short history reads it: 10816 to 8512 bytes with the ring and the fill,
  6 waves to 8.

| the clamp's form | handheld | desk | AMD iGPU |
|---|---|---|---|
| surfaces packed, decoded at the tap | 3.647 → 3.646 at the vanilla ship, −1.7% guild, +2.5% under the PBR stack's sky | +2.1 to +8.0% | +1.1 to +2.1%, −0.9% guild |
| the same, its short history's taps a loop left a loop | +1.4 to +4.0% | +4.2 to +8.5% | — |
| the normals alone packed | — | +0.4 to +1.8% | — |
| no ring luminances where no ring runs | — | inside the spread | — |

NVIDIA compiles the first to 79 registers for 65, of the points rebuilt at the tap: 62 with them decoded at the
load.

## The fog's scatter launch

- **Its four rays were four traversals inlined**, to the lamp, the sun, the moon and the ambient's, each asked
  before the first was traced. Under a sun and both moons on RADV the launch held 94 registers, 43 of them carried
  untouched through the first traversal: the later rays' asks, the sky's sources and the history's two texels.
- **A turn each at one traversal site**, the lamp by its row, the sky's sources read again at the moon's turn and
  the draws off their sequence again, the lamps' light written ahead of the rays and the history read past them:

| tuple | registers on RADV | demand | instructions |
|---|---|---|---|
| `sun moons sea` (dawn) | 96 → 80 | 94 → 74 | 4621 → 3026 |
| `moons` | 96 → 80 | 93 → 72 | 3739 → 2720 |
| `sun moons` | 112 → 80 | 96 → 74 | 4519 → 2962 |
| `sun sea` (day) | 80 | 78 → 71 | 3511 → 2434 |
| `sun moons sea dark` | 112 → 96 | 100 → 79 | 5032 → 3134 |

  Neither NVIDIA's nor AMD's Windows driver says what a launch holds.
- **The `air` zone, the history read past the rays and ahead of them**:

| place | handheld, past | handheld, ahead | desk, past | desk, ahead | AMD iGPU, past | AMD iGPU, ahead |
|---|---|---|---|---|---|---|
| vanilla ship | 7.767 → 7.875 (+1.4%) | +0.6% | 0.268 → 0.250 (−6.9%) | +0.6% | 3.393 → 3.183 (−6.2%) | +0.1% |
| vanilla ship-dawn | 13.277 → 12.907 (−2.8%) | −3.5% | 0.356 → 0.362 (+1.7%) | +1.5% | 5.492 → 5.287 (−3.7%) | −3.2% |
| vanilla guild | 1.204 → 1.224 (+1.7%) | +2.0% | 0.137 → 0.128 (−6.0%) | +0.7% | 0.781 → 0.774 (−0.9%) | +0.2% |
| PBR ship | 8.276 → 8.410 (+1.6%) | — | 0.284 → 0.265 (−6.8%) | — | 3.761 → 3.534 (−6.0%) | −0.6% |
| PBR ship-dawn | 14.346 → 13.736 (−4.3%) | — | 0.376 → 0.380 (+0.9%) | — | 6.357 → 6.169 (−3.0%) | −2.9% |
| PBR guild | 1.219 → 1.241 (+1.8%) | — | 0.142 → 0.133 (−6.5%) | — | 0.822 → 0.798 (−2.9%) | +1.4% |

  The turns are what the handheld's dawn gained, a tier; the history read past the rays is what NVIDIA and AMD's
  Windows driver gained by day and in a room, and it cost RADV 0.06 ms a place. At three night places on the
  handheld the form kept ran −0.8%, +1.3% and +1.9% (0.02 to 0.05 ms). A moon's turn skipped in a tuple with no
  moons moved nothing.

## The layers' stage

It resolved every hit of its walk, the solid it ends on among them, to learn from the opacity whether the hit is a
layer. Asked of the opacity alone first (`layerOpacityOf`), as the trace's peel asks, and only a layer resolved:
on RADV `panes` with the maps holds 192 registers for 256, five waves for four, in the tuples without a relief or
with the sea (demand 193 to 201 → 185 to 192), a tenth more instructions; NVIDIA's registers the same and a lane's
local memory within 96 bytes. The `panes` zone:

| place | handheld | desk | AMD iGPU |
|---|---|---|---|
| vanilla guild | 1.655 → 1.624 (−1.9%) | 0.115 → 0.112 (−2.4%) | 1.383 → 1.382 |
| PBR guild | 1.813 → 1.749 (−3.5%) | 0.140 → 0.133 (−4.8%) | 1.409 → 1.311 (−6.9%) |
| Repack guild | 2.590 → 2.149 (−17.0%) | 0.137 → 0.134 (−2.3%) | 1.581 → 1.562 (−1.2%) |
| vanilla ship, ship-dawn | −0.2%, +0.7% | −1.3%, −1.2% | +1.1%, +0.1% |
| Repack ship, ship-dawn | +1.1%, −0.2% | +0.4%, +1.6% | −0.1%, −0.6% |

Out of doors the zone is 0.08 to 0.6 ms and moved 0.002 to 0.006. No picture moved.

## The handheld's memory under the PBR stack

The tour's process holds 12.1 GiB of anonymous memory by its end on the desk, 6.7 GiB of it by its 34th second,
and 5.7 GiB of device memory by its fourth leg. On the handheld the two are one 16 GiB: its device heap is 6144
MiB with a budget of all of it, the device has no room for a texture and then a mesh at legs 3 and 4, and the
system's `earlyoom` ends the game during leg 5 of 93 with a `SIGTERM`, twice at the same frame, which the run logs
as the game ending with no reason. Vanilla and the Repack fly whole there, at 1.4 to 1.8 and 2.2 to 2.5 GiB of
device memory.
