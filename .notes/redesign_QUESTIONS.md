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

**Decided (2026-10-06): 3, the probe frame.** It is an item of the plan, after Phase 2.

**Blocks.** Nothing.
