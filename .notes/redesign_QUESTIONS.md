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

**Blocks.** Nothing else; the step stays in the plan with a pointer here.
