# Register tiers and shared tiles on a low-end AMD integrated GPU — 2026-11-13

A low-end AMD integrated GPU (RDNA 2), AMD's Windows driver 26.8.1, in a Remote Desktop session.
`openmw-rtxtool bench --size=1280x720 --window=false --frames=200` (853x480 traced), the default suite, a
base and a candidate build interleaved, two runs a side unless a line says one. Base against base a GPU zone's
median stood within 0.1 to 1%. Three contents: vanilla, the Fullrest Repack and a stack of PBR replacers.
Registers are `numUsedVgprs` of the shipped modules, which a bench's log prints; the modules that count for
the census, which `shot` and `--pipeline-code` run by default, hold up to 17 more.

## Which tuple a content runs

By the record's instance counts (`mappedInstances`, `reliefInstances`, `darkAlphaInstances`):

| content | mapped at the ship | with a relief | tuples |
|---|---|---|---|
| vanilla | 0 | 0 | `sun sea`, `sun moons sea`, none in the guild |
| Fullrest Repack | 10 (6 in the guild) | 0 | the same with `maps` |
| PBR stack | 66 556 | 19 184 | the same with `maps relief` |

No content held a dark map's alpha.

## What a tier of registers is worth

The driver's compiler lands a stage (32 lanes) on 96, 112 or 128 registers, or between 141 and 145, on 161,
177, 190 and up to 256. An RDNA 2 SIMD holds 1024 registers a 32-lane wave in steps of 16
(Mesa, `aco_ir.cpp`, `vgpr_alloc_granule`): ten waves at 96, eight at 128, six at 145.

Vanilla under the whole tuple's kernels (`--variants=false`) against its own, the same rays:

| zone | own tuple | whole tuple | ship | dawn | guild |
|---|---|---|---|---|---|
| bounce | 96 | 144 | +42.5% | +41.5% | +42.3% |
| trace | 112 to 128 | 145 | +20.7% | +22.4% | +25.2% |
| direct | 96 to 112 | 144 | +17.9% | +12.7% | +23.3% |
| landing | 96 to 126 | 144 | +10.4% | +9.1% | +25.2% |
| panes | 192 to 238 | 254 | — | — | +34.8% |

The frame +11.7%, +12.6% and +17.3%. The Repack's bounce (12.6 ms at the ship against vanilla's 8.7) stands
where vanilla's does under the whole tuple (12.4), for ten instances that wear a normal map.

## Where a stage's registers peak

A backward dataflow over the vector registers of the driver's ISA (`--pipeline-code`), whose count of
registers named is the driver's on every kernel:

- A stage holds 70 to 105 registers live at its intersections and its most in `resolve`: the ground stack's
  loop, the maps' reads and a relief's march, and in the gather's lobe arithmetic.
- Written before the ground stack's loop, named nowhere in it and read after it: the vertex tint (4
  registers), the material's own texture point (7), the layers' tangent and bitangent (6), the interpolated
  normal beside the turned one (3), a relief site's axes (9).
- The driver unrolls a stage's rays: `direct`'s three turns are three traversals in its code.

## The eye's stage with a relief

Each change alone against the base, one run, the PBR stack's `trace` at the ship, at dawn and in the guild,
and the Repack's `bounce`:

| change | `visibility … relief` | PBR trace | Repack bounce |
|---|---|---|---|
| two independent statements of `resolve` swapped | 141 to 145 | +0.5%, +0.4%, +1.2% | +0.5%, 0.0%, 0.0% |
| the vertex colours fetched past the stack | 128 without the sea, 141 with it | −8.1%, −8.7%, −6.2% | −0.2%, −0.3%, −0.3% |
| the layers' frame made again past the stack | 128 | −13.2%, −16.8%, −7.1% | **+9.5%, +8.8%, +6.6%** |
| a relief site as its tangent, handedness and a flag | 141 to 145 | −0.4%, −0.3%, −0.5% | +0.3%, 0.0%, −1.4% |

Made again in every tuple, the layers' frame left the Repack's `bounce … maps` at 125 registers from 128 and
its traversal loop 24 instructions longer, with four more waits on its loads. Made again where the scene has
a relief alone, with the vertex colours past the stack, two runs: every `visibility … relief` at 128, the PBR
stack's trace −14.0%, −15.9%, −7.2%, its water stage +3.0%, +4.0% (0.04 and 0.06 ms), its frame's zones −1.7
to −1.9% together; the Repack's bounce −0.6%, −0.5%, −0.4% and its trace −0.5%, −0.1%, +0.1%; vanilla's
direct −0.8%, −1.2%, +0.4% and every other stage inside its spread.

## The denoiser's shared tiles

The denoiser's passes run 64-lane waves. Shared memory as the driver reports it, in steps of 512 bytes.

| change | module | bytes | registers | zone |
|---|---|---|---|---|
| no fill tile where the fill is the bounce whole | `atrous-wide-compose` | 8192 → 6656 | 64 | first level −4.0 to −4.8% with no bounce |
| the same in the clamp, alone | `accumulate-clamp-ring-no-fill` | 12288 → 10752 | 93 → 70 | clamp **+3.2 to +5.1%** with no bounce |
| the clamp's fast means as the ring's luminance and the square's YCoCg, two reads a tap | `accumulate-clamp-ring` | 12288 → 11264 | 85 → 81 | clamp **+2.2 to +2.9%** on every content |
| the same, the square's luma saying whether a pixel holds a surface, one read a tap | `accumulate-clamp-ring` | 12288 → 11264 | 85 → 81 | clamp −5.4 to −8.0% on every content |
| both of the clamp's | `accumulate-clamp-ring-no-fill` | 12288 → 9216 | 93 → 71 | clamp −11.9 to −13.9% with no bounce |

## The shadow filter's two fields

One walk of the taps for both fields, each tap's normal decoded and its plane weighed once: the two-field
modules 101 and 102 registers to 40 and 46, the driver no longer unrolling the walk. The shadow zone −3.2 to
−7.0% under a sky on every content, over five rounds; in the guild, where one field filters, between −0.6%
and +1.3%, inside its spread of up to 1.7%.

## The same change on a high-end NVIDIA laptop GPU

`shot --against` with the upscaler off, the change against the commit under it: vanilla at 36 views, the
Fullrest Repack at three and the PBR stack at ten.

- The trace is the same on every frame of every view, and no picture moved.
- The shadow denoiser's images differ on every frame of every view where both fields filter, 24 of vanilla's
  36, the first of them in the frame's order `sky-shadow-moments`; a view where one field filters is the same.
  Two runs of one build differed in the wavelet's images and never in the shadow's, and the change with the
  shadow filter alone put back drew the commit's shadow images.
- The two-field module with a walk of the taps a field, through the same helpers, differs from the commit's
  as well. A one-field module runs those helpers to the bit, so what moved is the driver's code for what the
  pinning leaves to the device, a division and an exponential (`spirvpin.hpp`), and not the order the
  arithmetic is written in.
- The pictures of those views stand within 1 of 255 on 0.00% of their pixels, where two runs of one build
  stand.
