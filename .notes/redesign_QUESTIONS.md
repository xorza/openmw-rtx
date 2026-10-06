# Questions from executing the redesign

Each question is a decision the plan's execution could not take alone. The item it blocks, if any,
says so.

## The still check stands down on every world frame

**Found.** `./omw release shot` exits 1 at every place with a blended mask ("the driver's code
changed during …: depth or motion moved at frame 2"). It starts at `bd7fa766c1` (2026-10-04, before
this plan): a frame that others average with meets a soft edge's texels under the cut by a dither
drawn each frame (`Reconstruction::mAveraged`, `VisibilityConstants::mSoftEdgeDither`), so the eye's
own surface moves between two frames of a still. The check's premise — a still is one frame traced
again, and only a swapped driver code can move its depth or motion — no longer holds there. Motion
moves as well, so a check on motion alone does not hold either.

**Done now.** The check (`apps/rtxtool/measurer.cpp`) stands down where the frame is averaged, as it
does where the frame jitters. `shot` is green again. The cost: every world still is averaged, so the
check now guards only stills that are not, and no longer catches a swapped driver code in a `shot`.

**Options.**

1. Keep it stood down (now). The tripwire is gone for world stills; `repeat` and `--against` still
   compare runs frame for frame, which a swapped code also moves.
2. Hold the soft-edge dither for the frames of a frozen still: the check holds again, and a `shot`
   picture of a soft edge becomes one dither denoised instead of eight averaged — every baseline
   moves once.
3. A detector that does not read the eye's surface: for example, trace one probe frame with the
   first frame's number again at the end of each still, and compare it with the first.

**Recommendation.** 3: it keeps the pictures and the tripwire. It needs a harness change of its
own.

**Decided (2026-10-06): 3, the probe frame. Done.** Its extra engine frame between stops moved 17 later stops' pictures once, repeatably; decided: keep it and retake the baselines.

**Blocks.** Nothing.

## A spatiotemporal mask for the bounce (Phase 2 step 1, D3.6's rest)

**Found.** The bounce's pair is two scalar channels of the spatial tile, turned each frame by R2.
Per pixel, its draws over frames are an R2 sequence, which is low-discrepancy in time already. What
it lacks: the pair is not jointly blue across pixels within a frame, and the tile's 64-pixel period
stays on screen. A vector STBN mask (Wolfe et al. 2022) fixes both. It has to be made offline — an
optimiser over a 64 x 64 x 32 torus, minutes of work — and checked in as data, about 256 KB, with
a loader beside the tile's.

**Options.**

1. Build it: a generator in the tree (a verb of `openmw-rtxtool` or a small tool), the mask checked
   in under `files/rtx/`, read for `STREAM_BOUNCE`, measured with `noise` on every leg.
2. A scalar STBN pair: two 3D void-and-cluster masks, the same loader, no joint 2D blue. Cheaper to
   make and to trust, and it still takes the period off the screen.
3. Drop it: the per-pixel R2 sweep stays, and the item leaves the plan.

**Recommendation.** 1, measured against 3 before it stays: the bounce is the noisiest term the
denoiser takes, and the noise A/B decides.

**Decided (2026-10-06): 1, the vector STBN mask, kept only if `noise` on all three legs beats the
R2 sweep.**

**Done (2026-10-06): built, measured, and declined by that rule.** No leg beat the R2 sweep: every
`noise` figure stood within 0.01 of it. `redesign.md`, Phase 2 step 1, has the figures.

**Blocks.** Nothing.

## The glossy filter's roughness cap (Phase 3 step 5, D2 point 5)

**Found.** The plan takes ReBLUR's `1 − exp2(−200 r²)` cap on the glossy history's frames. NRD's
current ReBLUR (`REBLUR_TemporalAccumulation.cs.hlsl`, `_NRD_GetSpecMagicCurve`) scales the frames by
that curve times `r^0.25` only through its "responsive accumulation", whose `roughnessThreshold`
defaults to 0 (`NRDSettings.h`: "useful for animated water"): with the default, the factor is one
for every roughness over 0.001, and the cap is off. The anti-lag of the fast history is what bounds a
reflection's lag there, which step 5 gives the glossy filter.

**Options.**

1. Drop the cap: the glossy filter's fast companion and clamp bound the lag, as NRD's defaults do.
2. Take NRD's responsive accumulation whole, with a threshold of our own (for water), at least three
   frames kept, and measure it with the glossy trail test step 5 adds.
3. Take the plan's cap as written, always on.

**Recommendation.** 2. The glossy filter's fast companion was tried and declined (it took a still
mean off the mean of its frames, `redesign.md` Phase 3 step 5), so nothing else bounds a mirror's lag:
NRD's responsive accumulation with a threshold, measured with a trail test over a glossy floor.

**Blocks.** The cap alone.

**Decided (2026-10-06): 2, NRD's responsive accumulation with a threshold of our own, at least
three frames kept, measured with a trail test over a glossy floor.**

## Coverage-preserving alpha (Phase 6, D8's reduction)

**Found.** The plan puts coverage-preserving alpha into the device's mip chain, against the
material's alpha reference or 0.5. Two facts stand against it as written:

- The device builds a chain only for a file that carried none: 187 vanilla files, most of them
  particles — rain, smoke, flames — which blend and are not alpha-tested. Coverage preserved at 0.5
  makes a blended texture denser with distance, so far rain and smoke would thicken.
- A texture slot is keyed by file, wrap and encoding, not by material, so the reference a cutout
  is tested at does not reach the slot; and the foliage that thins with distance has its file's own
  chain, which this pass never touches.

Upstream's rasterizer corrects the same loss at sampling time for every alpha-tested material
(`adjust coverage for alpha test = true`, `alpha.glsl`: `alpha *= 1 + 0.25 * lod`), file chains
included.

**Options.**

1. The rasterizer's rule in the trace: in `candidateStops`, for a masked material that is not
   soft-edged, scale the sampled alpha by `1 + 0.25 * lod` before the test — the same picture as
   the rasterizer, file chains included, and nothing at load.
2. The plan as written, for generated chains only, at 0.5, for colour textures: fixes nothing a
   vanilla cutout shows, and thickens far rain and smoke.
3. True coverage preservation (Castaño) for every alpha-tested texture: rewrite each file's chain
   at arrival per reference — a slot per (file, reference), and a pass that counts coverage per
   level. The most exact, and the most cost at load and in memory.

**Recommendation.** 1: it is the rasterizer's own answer, so the two renderers keep one picture,
and it reaches the foliage the review saw thinning.

**Blocks.** D8's coverage point only; the rest of Phase 6 is done.

**Decided (2026-10-06): 1, the rasterizer's rule in `candidateStops`: a masked material that is
not soft-edged scales its sampled alpha by `1 + 0.25 · lod` before the test.**
