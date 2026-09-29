# Denoise and upscale: the run's record

The plan is `.notes/denoise-and-upscale.md`. What a person has to look at is
`.notes/verify-manually.md`. After a context compaction, read this file and the plan first.

- Branch `denoise-night`, from `bd42e0e219`. One commit per green phase, pushed; CI through
  `gh workflow run ci.yml --ref denoise-night`.
- Scratch: `/tmp/claude-60354/-home-xxorza-Projects-openmw/522b3b54-94d6-4bd6-b957-63fcbeef427d/scratchpad/run`.

## Summary

| Phase | State | Commit |
|---|---|---|
| 4 shadow denoiser | done | 85c2647ea6 |
| 5a lamps join the diffuse signal | done | a5d9d089da |
| 6a fast history | stopped, reverted; a lag guard kept | f426a7e18e |
| — race in the shadow classification | fixed | 5ff7a51f80 |
| 7 the upscaler's sizes in the core | done | (this commit) |

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


## Phase 5a: the lamps join the diffuse signal

Where `gather` splits, its diffuse half is the lamps alone, so no new field was needed:
`shadeSolid` adds it to the bounce, and `litSurface` now takes the diffuse and the specular light
apart instead of a `DirectLight`.

Test (`theFilterTakesTheLampsNoiseOffAFloorAndLeavesTheirLightWhereItWas`, four coloured lamps):
the filtered error is 0.21–0.23 of the raw, channel by channel, and the mean within 0.2–0.7%.

| Profile | Place | Before | After | Bar |
|---|---|---|---|---|
| vanilla | seyda-neen-pier | 2.33 / 12 | 2.29 / 12 | 5.92 / 30 |
| vanilla | balmora-mages-guild | 7.11 / 90 | 4.77 / 76 | 4.54 / 38 |
| PBR | seyda-neen-pier | 6.70 / 68 | 6.59 / 68 | 7.06 / 34 |
| PBR | balmora-mages-guild | 12.21 / 111 | 10.25 / 105 | 7.34 / 53 |

**What is left in the guild is the paper screens.** Nearly every pixel over 60 levels off the
reference is on the screens the lamps light from behind: a see-through layer's light is composed in
the trace and never reaches a filter. FSR's accumulation (phase 8) is the next thing that acts on
it; measured again there.

Cost: unchanged (default suite, trace 4.25 / 4.77 / 2.82, filter 2.38 / 1.85 / 3.12, shadow 0.42 /
0.52). Proofs: every frame within 1 level unfiltered; the map tiles moved again, because the lamps
are now filtered in them too. `repeat --pairs=10` identical, `./omw test` green, gate clean.

## Phase 6a: the fast history — stopped

The design (plan 5.3): a fast mean of each pixel's luminance over `ACCUMULATE_FAST_FRAMES = 4`,
kept in the accumulator's free moment channel, and the long mean clamped to it ± k sigmas of one
sample, with its frame count cut to the fast one's where the clamp moved it.

What it did (GPU tests):

| Setting | Lamp out, light left after 8 frames | Four lamps, filtered/raw error | Lone pixel with history |
|---|---|---|---|
| no clamp (today) | 24% | 0.21–0.23 | quieter than alone, mean in 2% |
| k = 2 | 8% | 0.31 | noisier than alone, mean 2.4% dark |
| k = 3 | 8% | fails the mean by 1% | noisier, mean 2.1% dark |
| k = 4 | ≤ 12% | passes | 0.0044 against 0.0048 alone: history nearly worthless |

**Why:** one lamp drawn out of four gives heavy-tailed samples, and a per-pixel mean of four of
them sits below its expectation most of the time. Clamping the long mean to it biases the picture
dark and throws the history away on noise. ReLAX clamps to the fast history's *spatial
neighbourhood* (a 3×3 or 5×5 box of it, in a pass of its own), which is the second design; its cost
and its benefit want a person looking at a flickering torch, so it is not built tonight. The code is
reverted. `aLampThatGoesOutLeavesTheHistoryWithinAFewFrames` stays, as a guard on today's lag (24%,
bound 30%).

## A race in phase 4's classification (found in phase 7)

Phase 7 changes no shader, yet `shot --against` phase 5a's pictures differed on 0.1–0.2% of the
pixels of every sunlit exterior, by up to 147 levels, and on no interior. The cause: rewriting the
classification for the mask words dropped the barrier between lane 0's reset of the shared
receiver count and the other lanes' additions, so a tile was skipped or filtered by its lanes'
timing. `repeat --pairs=10` had passed over it twice; a hash comparison of two runs found it. Fixed
in 5ff7a51f80, and three runs of every view agree by hash. The phase 4 and 5a noise figures were
taken with the race in; they may move by a little.

## Phase 7: the upscaler's sizes in the core

- `upscaleRatio`, `extentsFor`, `jitterPhasesFor` and `sUpscaleLevelBias` in `frame/upscale.hpp`;
  `Reconstruction::mJitterPhases`; the level bias takes FSR's minus one; `sampleFrame` wraps the
  Halton index at the phase count.
- `device/upscalerextensions.hpp` and its uses in the instance, the device and the profile are gone;
  `Upscaler::renderSizeFor` is gone and `createTargets` asks `extentsFor`.
- Tests: the 1920×1080 table (640×360/72, 960×540/32, 1129×635/23, 1280×720/18, 1920×1080/8),
  the level biases with the upscaler's own level, the wrapped jitter.
- Proof: nothing upscales yet, and three runs of every view agree by hash; `./omw test` green, gate
  clean.
