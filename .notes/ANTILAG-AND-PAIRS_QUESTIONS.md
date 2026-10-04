# Questions from executing `ANTILAG-AND-PAIRS.md`

Each question names the plan's item, the options with what was measured, a recommendation, and what
waits on the answer.

## Q1. The anti-lag's cost (step A.7, A7 acceptance)

**Measured** (release, `bench`, guild and pier at 1280×720 traced):

| | `accumulate` | `clamp` | Together, over the start's 0.25 |
|---|---|---|---|
| Start | 0.25 | — | — |
| Fast means in full floats (A.3, A.4 as committed) | 0.44 | 0.35 | +0.54 |
| Fast means packed in `RGB9E5` | 0.28–0.33 | 0.25 | +0.28–0.33 |

The acceptance allows +0.1 ms. What the clamp buys, with the fast cap of 2 the sweep chose: the
sky's trail 16.6 → 7.7 pixels; a floor whose sky halves within a tenth of its new level by frame 31,
where without it the floor stands 39% over it at frame 40; and still, strafed and walked frames
quieter at every place of the bounce suite.

**Options:**

1. Keep it on at about +0.3 ms.
2. Keep it on, and cut its cost further: the acceleration's 5×5 read of the samples (it took the
   trail from 14.24 to 13.07) and the moments' correction are a third of what the clamp reads and
   writes.
3. Leave it off by default (`ReconstructionRequest::mAntilag`), and drop the wavelet's feedback
   instead: the trail 8.6 pixels with no clamp at all and at no cost, but the yurt 0.03–0.05
   noisier, which the plan's gate forbids.

**Recommendation:** 1, then 2 as a separate change. It halves the trail and takes noise off every
place, which nothing else measured did.

**Waits on it:** the default of `mAntilag`, and A7's acceptance.

## Q2. The plan's trail target of 4 pixels (A7)

No combination measured reaches it: the clamp with a fast cap of 2 leaves 7.7 pixels, and without
the feedback as well 6.5.
Under the reuse a reservoir carries samples from frames before, and the wavelet spreads what the
history keeps. **Recommendation:** take 8 pixels as the target for this plan, and record what is
left as a question for the reuse (`RESTIR-GI.md`), whose spatial half spreads old samples too.

## Q3. The reuse's cost against §7's 1.0 ms (step B.5, B6 acceptance)

**Measured** (release, `bench --suite=bounce`, the four zones validate + temporal + pairs +
resolve, median ms; then noise strafed and walked at the yurt, against the start's 2.26 and 2.66):

| | Guild | Planter | Yurt | Pier | Pond | Yurt strafed / walked |
|---|---|---|---|---|---|---|
| Start (4a59dcb) | 1.66 | 1.97 | 1.64 | 2.06 | 1.36 | 2.26 / 2.66 |
| Paired, two neighbours (landed) | 1.28 | 1.42 | 1.34 | 1.43 | 0.96 | 2.26 / 2.65 |
| Paired, one neighbour | 0.99 | 1.09 | 0.99 | 1.10 | 0.75 | 2.32 / 2.74 |

**Options:**

1. Keep two neighbours and move §7's limit to 1.3 ms at the guild.
2. One neighbour: the limit met at four of five places, the yurt 0.06 and 0.08 noisier in motion,
   past the plan's gate.
3. Profile the temporal merge (0.31 ms for a dispatch that traces nothing) inside the kernel, which
   needs `ncu` (Nsight Compute), not installed here: `sudo pacman -S nsight-compute` or NVIDIA's
   installer.

**Recommendation:** 1 now, and 3 if the limit matters: the merge is a quarter of what is left.

**Waits on it:** B6's cost acceptance.

