# Denoise and upscale: the run's record

The plan is `.notes/denoise-and-upscale.md`. What a person has to look at is
`.notes/verify-manually.md`. After a context compaction, read this file and the plan first.

- Branch `denoise-night`, from `bd42e0e219`. One commit per green phase, pushed; CI through
  `gh workflow run ci.yml --ref denoise-night`.
- Scratch: `/tmp/claude-60354/-home-xxorza-Projects-openmw/522b3b54-94d6-4bd6-b957-63fcbeef427d/scratchpad/run`.

## Summary

| Phase | State | Commit |
|---|---|---|
| 4 shadow denoiser | done | (this commit) |

## Phase 4: the shadow denoiser

Baselines at `bd42e0e219` (release, 1920×1080, `omw noise`; frame mean / p99, bar mean / p99):

| Profile | Place | Frame | Bar |
|---|---|---|---|
| vanilla | seyda-neen-pier | 18.54 / 92 | 5.92 / 30 |
| vanilla | balmora-mages-guild | 7.11 / 90 | 4.54 / 38 |
| PBR | seyda-neen-pier | 22.75 / 106 | 7.06 / 34 |
| PBR | balmora-mages-guild | 12.21 / 111 | 7.34 / 53 |

Checkpoints:

1. The sky source's term split off into `CHANNEL_SUNLIT` (payload 12 → 15 words): every picture of
   `shot --views=all --map`, filtered and unfiltered, within 1 level of the baseline, on 0.00% of
   the pixels.
2. `lib/surfacematch.glsl` (the accumulator's history test, the wavelet's plane and facing tests):
   every filtered frame identical to checkpoint 1 by hash.

Where the port differs from the plan, and why:

- **No mask pass and no mask buffer.** The classification reads the 24×24 square of bits its kernel
  and its three filter levels reach straight off `CHANNEL_SUNLIT` into shared memory. One dispatch
  fewer, no buffer, no `R32UI` format, no subgroup operation.
- **The tile record is one bit (`R8`)**: a cleared tile's value is already the temporal pass's own
  answer, written exactly nought or one, and the SDK's middle level leaves it there.
- **No `lib/shadowdenoise.glsl`**: the port lives in the two shaders that use it, each with the notice.
- **`lib/surfacematch.glsl`** in place of the planned `lib/history.glsl`: it holds the temporal
  test and the two spatial tests, so the shadow filter stops at the wavelet's edges.
- **Moments in `RGBA16F`**: the SDK's `R11G11B10_FLOAT` has no layout here.
- **The SDK's contrast step is not ported**: `pow(mean, max(1.2 - variance, 1))` darkened a penumbra
  by 3.4% against a 256-frame reference, on purpose. Correctness before looks.
- **The frame's edge is not a shadow**: the SDK counts pixels off the frame as shadowed in its local
  kernel, which held a penumbra's mean 1.3% low; the port normalises by the pixels inside.
- **Velocity dilation dropped**: the SDK takes the nearest-depth velocity of a quad; the port uses the
  accumulator's footprint and surface test, one rule for both histories.

Test figures (`theShadowDenoiserTakesTheNoiseOffAPenumbraAndLeavesItsLightWhereItWas`): RMS error
against a 256-frame reference 0.0762 raw, 0.0058 denoised; mean 0.64% under the reference, the
history clamp's own bias.

Debug noise (vanilla), first run: pier 18.54 / 92 → 4.34 / 18 (bar 5.92 / 30); guild unchanged.

The cost, and what it changed:

- The first port read the 24×24 square of bits straight off the channel (no mask pass). The bench
  split said the classification cost 0.24 ms whether it filtered or not, 0.34 ms in a room. The
  SDK's structure was the right one: the mask pass is back (`shadowmask.comp`, one `R32UI` word an
  8×4 tile), the classification reads 18 words, and a frame whose sky lights nothing
  (`Shaders::skySourceLights`) records no shadow pass at all; the composite then takes the channel's
  own bit (`CompositeConstants::mShadowed`).
- The filter levels no longer read `CHANNEL_SUNLIT`: the temporal pass marks a pixel that receives
  nothing with a variance of `SHADOW_NO_RECEIVER`, and the levels carry it.

Final figures (release, 1920×1080, `omw noise`, frame mean / p99 against bar mean / p99):

| Profile | Place | Before | After | Bar |
|---|---|---|---|---|
| vanilla | seyda-neen-pier | 18.54 / 92 | 2.33 / 12 | 5.92 / 30 |
| vanilla | balmora-mages-guild | 7.11 / 90 | 7.11 / 90 | 4.54 / 38 |
| PBR | seyda-neen-pier | 22.75 / 106 | 6.70 / 68 | 7.06 / 34 |
| PBR | balmora-mages-guild | 12.21 / 111 | 12.21 / 111 | 7.34 / 53 |

The PBR pier's p99 is the glossy speckle, which phase 9 is for.

Cost, back to back against `bd42e0e219` built in `~/Projects/openmw-base` (default suite, two legs
each after a warm-up; taken under the session):

| Place | Trace base → new | Shadow | Frame median base → new |
|---|---|---|---|
| seyda-neen-ship | 4.18, 4.21 → 4.23, 4.26 | 0.43, 0.44 | 9.09, 9.16 → 9.66, 9.75 |
| seyda-neen-ship-dawn | 4.64, 4.73 → 4.74, 4.81 | 0.51, 0.57 | 9.23, 9.40 → 10.05, 10.54 |
| balmora-mages-guild | 2.83, 2.85 → 2.83, 2.82 | — | 7.33, 7.71 → 7.38, 7.69 |

The trace's own growth, 0.05–0.1 ms outdoors, is the payload's three new words.

Proofs: every frame of `shot --views=all --map --upscale=off --filter=0` within 1 level of the
baseline; the 27 map tiles moved, because a map is a picture the wavelet always filters and the
shadow denoiser now filters it too (one frame, no history). `repeat --pairs=10` identical. `./omw
test` green. `./omw gate` clean.

