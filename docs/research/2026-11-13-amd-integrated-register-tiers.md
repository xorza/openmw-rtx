# Register tiers and shared tiles on a low-end AMD integrated GPU — 2026-11-13

A low-end AMD integrated GPU (RDNA 2), AMD's Windows driver 26.8.1, in a Remote Desktop session.
`bench --size=1280x720 --window=false --frames=200` (853x480 traced), the default suite, base and candidate
interleaved, two runs a side; base against base a zone stood within 0.1 to 1%. Vanilla, the Fullrest Repack and a
stack of PBR replacers. Registers are `numUsedVgprs` of the shipped modules; the census's modules hold up to 17 more.

## What a tier of registers is worth

The driver lands a stage (32 lanes) on 96, 112 or 128 registers, or on 141–145, 161, 177, 190 and up to 256; an RDNA 2
SIMD holds 1024 registers a 32-lane wave in steps of 16, ten waves at 96, eight at 128, six at 145. Vanilla under the
whole tuple's kernels (`--variants=false`), the same rays through 144 registers where its own tuples hold 96–128:
`bounce` +42%, `trace` +21–25%, `direct` +13–23%, `landing` +9–25%, the frame +12–17%. The Repack's bounce runs as
slow as that for ten instances that wear a normal map: a tuple with maps costs every pixel.

## Where a stage's registers peak

A backward dataflow over the driver's ISA (`--pipeline-code`), whose count matches the driver's on every kernel:

- A stage holds 70–105 registers at its intersections and its most in `resolve`: the ground stack's loop, the maps'
  reads, a relief's march and the gather's lobe arithmetic.
- Carried untouched through the stack's loop: the vertex tint (4), the material's own texture point (7), the layers'
  tangent and bitangent (6), the interpolated normal beside the turned one (3), a relief site's axes (9).
- The driver unrolls a stage's rays: `direct`'s three turns are three traversals in its code.

## The eye's stage with a relief

- **The vertex colours fetched past the stack**: `visibility … relief` 141 → 128 without the sea; the PBR stack's trace
  −6 to −9%. Kept.
- **The layers' frame made again past the stack** in every tuple: the PBR stack's trace −7 to −17%, but the Repack's
  bounce +6.6 to +9.5% (its loop longer for no tier). Kept only where the scene has a relief: the PBR stack's trace −7
  to −16%, its water stage +3–4%, the Repack's bounce −0.5%.
- **A relief site as its tangent, handedness and a flag**, and two independent statements of `resolve` swapped: nothing
  moved, which says the rows above measure their own change and not the driver's whim.

## The denoiser's shared tiles

The denoiser's passes run 64-lane waves; shared memory in steps of 512 bytes.

| change | module | bytes | zone |
|---|---|---|---|
| no fill tile where the fill is the bounce whole | `atrous-wide-compose` | 8192 → 6656 | first level −4.0 to −4.8% with no bounce |
| the clamp's fast means as the ring's luminance, the square's luma saying whether a pixel holds a surface | `accumulate-clamp-ring` | 12288 → 11264 | clamp −5.4 to −8.0% |
| both of the clamp's, the fill tile dropped too | `accumulate-clamp-ring-no-fill` | 12288 → 9216 | clamp −11.9 to −13.9% with no bounce |

The fill tile dropped from the clamp alone ran it 3–5% longer, and the luminance read beside the colour at every tap
2–3% longer: a shared tile is worth what the waves it frees run.

## The shadow filter's two fields

One walk of the taps for both fields, each tap's normal decoded and its plane weighed once: the two-field modules 101 →
40–46 registers, the driver no longer unrolling the walk; the shadow zone −3.2 to −7.0% under a sky. On a high-end
NVIDIA laptop GPU no picture moved, but the shadow denoiser's images differ in the last place where both fields filter:
the driver's code for a division and an exponential, which the pinning leaves to the device (`spirvpin.hpp`).
