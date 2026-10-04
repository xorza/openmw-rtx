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
