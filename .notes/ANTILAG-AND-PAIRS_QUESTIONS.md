# Questions from executing `ANTILAG-AND-PAIRS.md`

Each question names the plan's item, the options with what was measured, a recommendation, and what
waits on the answer.

## Q1. The accumulator's history: the wavelet's feedback, or the clamped mean (step A.5, AD5)

**Measured** (release, `noise --suite=bounce`; the sky's trail from `RtxBounceTrailTest` under the
spatiotemporal reuse, in pixels of lag):

| | Trail | Still noise, guild / planter / yurt / pier / pond | Strafed | Walked |
|---|---|---|---|---|
| Start (4a59dcb) | 16.61 | 0.65 / 0.70 / 0.74 / 0.49 / 0.46 | 1.37 / 1.83 / 2.26 / 1.18 / 1.19 | 1.50 / 1.48 / 2.66 / 1.18 / 1.14 |
| Feedback, with the clamp (A.4, committed) | 13.07 | 0.63 / 0.67 / 0.73 / 0.49 / 0.46 | — | — |
| No feedback, with the clamp | 8.11 | 0.66 / 0.70 / 0.77 / 0.50 / 0.46 | 1.39 / 1.80 / 2.31 / 1.18 / 1.19 | 1.45 / 1.52 / 2.70 / 1.19 / 1.14 |

- **Feedback (today).** Quieter still frames; the trail 13 pixels.
- **No feedback (ReLAX's).** The trail 38% shorter (8.1 pixels); the yurt 0.03–0.05 noisier in
  every case, past the plan's gate of 0.02 still and nothing in motion; the guild walked 0.05 cleaner.

**Recommendation:** no feedback. The trail is what a player sees, and 0.03–0.05 levels at one place
is under what `noise` calls visible elsewhere in this tree. The plan's gate forbids it, so the
feedback stays until you decide.

**Waits on it:** step A.5 stays in the plan; A.6's sweep and A.7's record were taken with the
feedback.

## Q2. The anti-lag's cost (step A.7, A7 acceptance)

**Measured** (release, `bench`, guild and pier at 1280×720 traced):

| | `accumulate` | `clamp` | Together, over the start's 0.25 |
|---|---|---|---|
| Start | 0.25 | — | — |
| Fast means in full floats (A.3, A.4 as committed) | 0.44 | 0.35 | +0.54 |
| Fast means packed in `RGB9E5` | 0.28–0.33 | 0.25 | +0.28–0.33 |

The acceptance allows +0.1 ms. What the clamp buys with the feedback: the sky's trail 16.6 → 13.1
pixels; a floor whose sky halves follows within a tenth by frame 36 instead of after 64.

**Options:**

1. Keep it on at about +0.3 ms.
2. Keep it on, and cut its cost further: the acceleration's 5×5 read of the samples (it took the
   trail from 14.24 to 13.07) and the moments' correction are a third of what the clamp reads and
   writes.
3. Leave it off by default (`ReconstructionRequest::mAntilag`), and take Q1's answer alone: the
   history without feedback shortens the trail to 8.6 pixels with no clamp at all, at no cost.

**Recommendation:** 3 with Q1's "no feedback" if the yurt's 0.03–0.05 is acceptable; it is the
largest share of the trail for nothing. Otherwise 2.

**Waits on it:** the default of `mAntilag`, and A7's acceptance.

## Q3. The plan's trail target of 4 pixels (A7)

No combination measured reaches it: the best, no feedback with a fast cap of 2, left 6.5 pixels.
Under the reuse a reservoir carries samples from frames before, and the wavelet spreads what the
history keeps. **Recommendation:** take 8 pixels as the target for this plan, and record what is
left as a question for the reuse (`RESTIR-GI.md`), whose spatial half spreads old samples too.
