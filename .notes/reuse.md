# The bounce's reuse: what it buys, where, and why

Findings after the denoiser plan was executed, on `nightly`. Release builds, 1920×1080 from
FSR quality unless stated, `noise` figures as the harness prints them: frame noise / bias against
the converged reference. Every A/B ran on one card, one run after the other.

## nightly against master

Same card, run master, nightly, master, nightly back to back, 10 s a place, the mean of the two
runs of each. `master` is 11 commits behind `nightly`, all of them the plan's.

| place | median, master → nightly (ms) | change | p99, master → nightly (ms) |
|---|---|---|---|
| Vivec | 8.21 → 6.07 | −2.13 (−26%) | 9.22 → 7.06 |
| Seyda Neen's ship | 7.84 → 6.13 | −1.71 (−22%) | 8.66 → 7.34 |
| the ship at dawn | 8.24 → 6.66 | −1.58 (−19%) | 9.80 → 8.16 |
| Balmora | 6.61 → 5.09 | −1.52 (−23%) | 7.33 → 5.80 |
| Ald-ruhn | 6.57 → 5.06 | −1.51 (−23%) | 7.40 → 5.86 |
| Dagon Fel | 6.22 → 4.94 | −1.29 (−21%) | 7.14 → 5.62 |
| Sadrith Mora | 5.56 → 4.46 | −1.10 (−20%) | 6.36 → 5.31 |
| Seyda Neen's shore | 6.39 → 5.33 | −1.05 (−16%) | 8.93 → 6.65 |
| Addamasartus | 6.50 → 5.59 | −0.92 (−14%) | 8.62 → 8.17 |
| mages' guild | 6.57 → 5.71 | −0.85 (−13%) | 7.55 → 7.45 |
| the Andrano tomb | 5.65 → 4.82 | −0.84 (−15%) | 6.24 → 6.40 |
| Seyda Neen's customs | 5.91 → 5.11 | −0.80 (−14%) | 6.66 → 6.78 |
| Arkngthand | 3.60 → 3.15 | −0.45 (−12%) | 4.32 → 4.31 |
| Vivec's canalworks | 3.85 → 3.62 | −0.23 (−6%) | 4.88 → 4.89 |

Outdoors the reuse is off now (about 1.1 ms), the lighter wavelet saves 0.15 to 0.25 ms and the
cloud shadows' read is gone. In lit rooms the reuse is temporal only (about 0.5 ms) and the
wavelet saves about 0.3 ms. The canalworks and Arkngthand have little bounce to filter, and move by
up to 0.2 ms between runs of the same build.

## Why the reuse lost its noise gain: part 1

Bisect of `--ab=bounce-reuse=temporal,off --suite=bounce --still`, frame noise temporal / off:

| place, leg | before part 1 (`07532515d8`) | part 1 (`e896492f9c`) | step 16 (`5f5cce3fcb`) | step 17 (`b67892c413`) |
|---|---|---|---|---|
| mages' guild, still | 0.63 / 0.69 | 0.40 / 0.38 | 0.40 / 0.38 | 0.39 / 0.38 |
| guild's planter, still | 0.76 / 0.89 | 0.43 / 0.41 | 0.43 / 0.41 | 0.41 / 0.40 |
| mages' guild, walked | 1.34 / 1.49 | 1.26 / 1.23 | 1.26 / 1.23 | 1.25 / 1.23 |
| guild's planter, strafed | 1.43 / 1.62 | 1.33 / 1.30 | 1.33 / 1.30 | 1.31 / 1.29 |

Part 1 took the gain; steps 16 and 17 moved nothing. The reuse was taking off the lanterns' glow,
a bounce that finds a small bright surface rarely and brightly, which part 1 found was the lamps'
light counted twice and removed. The rooms' noise fell by more than the reuse ever took (the guild
0.69 → 0.38 with no reuse).

On `nightly`, in the bounce suite's lit rooms:

- **With the filter off** (`--filter=false`), the reuse takes 1 to 8% off a frame's noise (the
  guild 1.36 → 1.34 still, the planter 2.55 → 2.44 still and 6.41 → 5.92 strafed).
- **Two, four and eight frames after a cut**, no place moves by more than 0.01.

## Where the reuse still works: a room lit by what glows in it

The bounce suite lost the case the reuse is for, so a place was added that holds it:
`akulakhan-chamber`, the heart's chamber under Red Mountain, where 53 materials glow and none is a
lamp's model, with its 7 lamps off (`lamps = false`, below). Its light is what the bounce finds
glowing.

With the filter off, temporal against off:

| leg | noise | bias | fireflies in 1000 |
|---|---|---|---|
| still | 3.56 against 5.43 (−34%) | 2.20 against 10.82 | 0.00 against 0.00 |
| strafed | 8.54 against 11.70 (−27%) | 3.55 against 13.03 | 0.21 against 0.92 |
| walked | 7.52 against 11.22 (−33%) | 2.25 against 10.80 | 0.22 against 1.08 |

The reuse does its job here: a third off the raw noise and most of the raw bias.

## Every kind of place, three modes

Noise / bias, off · temporal · spatiotemporal:

| place, leg | off | temporal | spatiotemporal |
|---|---|---|---|
| mages' guild, still | 0.38 / 1.33 | 0.39 / 1.33 | 0.39 / 1.29 |
| mages' guild, walked | 1.23 / 2.32 | 1.25 / 2.35 | 1.24 / 2.37 |
| guild's planter, still | 0.40 / 1.58 | 0.41 / 1.61 | 0.41 / 1.57 |
| guild's planter, strafed | 1.29 / 1.81 | 1.31 / 1.78 | 1.30 / 1.73 |
| Ahemmusa's yurt, still | 0.44 / 1.50 | 0.44 / 1.52 | 0.44 / 1.50 |
| **chamber, still** | 0.46 / **2.60** | 0.49 / **1.69** | 0.49 / **1.39** |
| **chamber, strafed** | 1.44 / **3.92** | 1.54 / **2.86** | 1.48 / **2.31** |
| **chamber, walked** | 1.48 / **2.95** | 1.59 / **2.10** | 1.53 / **1.86** |
| Seyda Neen's pier, walked | 1.17 / 2.09 | 1.18 / 2.15 | 1.18 / 2.16 |
| Seyda Neen's pond, still | 0.48 / 1.59 | 0.49 / 1.60 | 0.49 / 1.58 |
| **Balmora by day, still** | 0.36 / **1.07** | 0.36 / **1.17** | 0.36 / **1.15** |
| Balmora by day, strafed | 0.88 / 0.89 | 0.89 / 0.97 | 0.89 / 0.97 |
| the ship at dawn, still | 0.47 / 1.22 | 0.48 / 1.23 | 0.47 / 1.21 |
| Balmora at 23:00, still | 0.17 / 0.35 | 0.17 / 0.37 | 0.17 / 0.37 |
| the ship at 23:00, strafed | 0.46 / 0.58 | 0.47 / 0.58 | 0.46 / 0.58 |
| Balmora in fog at night | 0.07 / 0.24 | 0.07 / 0.24 | 0.07 / 0.24 |

The other legs agree with the rows shown. The runs are in the scratchpad's `results/one/`.

- **Outdoors at night**: no mode moves anything.
- **Outdoors by day**: either reuse adds 0.06 to 0.10 of bias in Balmora and at the pier.
- **Lit rooms**: within 0.03 either way.
- **The glow-lit chamber**: the reuse takes a third of the bias off, the spatial half a fifth more,
  at 0.03 to 0.11 more noise.

Cost, median frame:

| | off | temporal | spatiotemporal |
|---|---|---|---|
| exteriors (`bench --suite=exteriors`) | 4.48 to 6.23 ms | +0.57 to +1.05 | +0.89 to +1.80 |
| the chamber | 3.84 ms | +0.85 | +1.47 |
| lit rooms (step 18) | — | +0.24 to +0.77 against off | +0.5 to +0.6 more |

## What the reuse is really correcting

With the filter on, the reuse's gain in the chamber is bias and not noise. Without the reuse, the
denoiser loses the rare bright samples: a room lit by them comes out darker than the truth (bias
2.60 against 1.69). The reuse keeps those samples alive across frames, and the filter then keeps
their light. That is the open issue in `ISSUES.md`: a settled history of a rare bright bounce
filters to about half its light.

So the reuse is paying 0.6 to 1.8 ms a frame to hide a denoiser fault that costs nothing to have
right. **The single pipeline worth building is no reuse anywhere, with that darkening fixed in the
denoiser**: the cheapest everywhere (0.24 to 0.85 ms back in rooms, the outdoor time as it is), and
the glow-lit rooms keep their light without a reuse pass. The test is direct: with the fix and the
reuse off, the chamber's bias must come down from 2.60 to near the reuse's 1.69, and no other place
may move. If it does, the indoor/outdoor rule (`BounceReuseRule::Rooms`) goes away. Until then
`rooms` stays the default.

## Which stage darkens

On the chamber, each stage turned off alone, noise / bias, against the full filter's 0.46 / 2.60
with no reuse and 0.49 / 1.69 with temporal reuse:

| turned off | no reuse | temporal reuse |
|---|---|---|
| the anti-lag clamp (`--antilag=false`) | 0.46 / 2.02 | 0.49 / 1.71 |
| the anti-firefly ring (`--antifirefly=false`) | 0.52 / 1.90 | 0.55 / 1.29 |
| the history fix (`--history-fix=false`) | 0.48 / 2.84 | 0.51 / 1.89 |
| the upscaler (`--upscale=off`) | 0.31 / 1.20 | 0.31 / 0.97 |
| the clamp, the ring and the upscaler | **0.28 / 0.98** | **0.31 / 0.98** |

**With those three off, no reuse and temporal reuse are the same picture** (0.98 and 0.98): the
whole of the reuse's gain in the chamber is a correction for three stages that each lose a rare
bright sample's light. The clamp costs 0.58 of bias, the ring 0.70, and the upscaler the most,
though its share is partly its resolution, which a bias against a native reference also counts.
The history fix helps (2.84 without it).

- **The ring** was an open question after part 1; its bias was known.
- **The clamp** holds the slow mean to a box of two-frame fast means. Its two frames were chosen
  (`ACCUMULATE_FAST_FRAMES`) on `RtxBounceTrailTest`, whose bar's motion was its whole travel since
  it stood still (the fault in `ISSUES.md`). With the placement table advanced as the game does,
  the sky's trail behind the bar, in pixels of lag and columns of darkness left behind:

| fast frames | with the clamp | without it |
|---|---|---|
| 2 (now) | 7.86 / 1.40 | 19.02 / 2.70 |
| 4 | 9.78 / 1.54 | 18.99 / 2.70 |
| 6 (ReLAX's) | 12.59 / 1.76 | 18.99 / 2.69 |

  The clamp is worth more than the old figures said (19.0 → 7.9 pixels, where they read 11.9 →
  8.5), and a longer fast mean gives back lag quickly.
- **The upscaler** rectifies its history against the neighbourhood of each pixel, which holds a
  sparse bright pixel down; a denoised input that keeps the rare light spread over frames, as the
  reuse's does, survives it.

With no reuse and the ring off, at fast means of 2, 4 and 6, noise / bias (the rest of the
bounce suite moves by 0.01 or less in noise):

| setup | chamber still | chamber strafed | chamber walked |
|---|---|---|---|
| reuse in rooms, ring on (the default before) | 0.49 / 1.69 | 1.54 / 2.86 | 1.59 / 2.10 |
| no reuse, ring off, fast mean 2 | 0.52 / 1.90 | 1.44 / 2.37 | 1.49 / 2.23 |
| no reuse, ring off, fast mean 4 | 0.51 / 1.59 | 1.45 / 2.67 | 1.49 / 2.32 |
| no reuse, ring off, fast mean 6 | 0.51 / 1.54 | 1.45 / 2.84 | 1.50 / 2.43 |

In the lit rooms, no reuse and no ring against the default before: the bias 0.03 to 0.09 lower
(the guild 1.33 → 1.28 and the planter 1.61 → 1.53 still, the planter 1.78 → 1.69 strafed), the
noise the same. Outdoors nothing moves, the reuse being off there already.

## Decided: one pipeline

**No reuse, no anti-firefly ring, a fast mean of two frames, everywhere.** In the chamber it stands
level with the reuse over the three legs (bias 2.17 against 2.22 on average: the still frame 0.21
worse, the strafed one 0.49 better and less noisy), every lit room is a little better, outdoors is
as it was, and every room saves the reuse's 0.24 to 0.85 ms. A longer fast mean helps only a
standing eye, at the moving one's cost and at the trail's (7.9 → 12.6 pixels at six).

- `ReconstructionRequest::mBounceReuse` defaults to `off` and `mAntiFirefly` to false.
  `BounceReuseRule` and its `rooms` are gone: a request holds a mode, and `resolve` no longer reads
  the sky. The reuse's modes stay, for A/Bs (`--bounce-reuse=`), and the world's chain keeps its
  reservoirs.
- `RtxBounceTrailTest` advances the placement table and runs under no reuse, the game's default:
  the clamp takes the sky's trail from 16.74 to 6.08 pixels and its darkness from 2.05 to 0.75
  columns. Under the whole reuse it left 7.86 pixels: the reservoirs dragged the old light too.
- The chamber's still frame keeps 0.21 more bias than with the reuse: the clamp's and the
  upscaler's share of the darkening, which `ISSUES.md` keeps open.
- The ring's code stays as an A/B, and its 16×16 square is still loaded with the switch off, which
  `look.h` measured at 0.05 ms of the clamp; a clamp kernel without the ring would take that back.

## Changes not yet committed

- `files/rtx/views.cfg`: the `lamps` key (`lamps = false` hides every lamp, as the game's own
  `Mask_Lighting` does) and the `akulakhan-chamber` view.
- `files/rtx/benches.cfg`: the chamber in the bounce suite.
- `apps/rtxtool`: `Stand::mLamps`, read by the view file, printed in the window's Home block, and
  staged by `CameraDriver::showLamps`; and `Stand::approachFrom` carries it onto a flown stand,
  which it first did not (a strafed or walked leg then flew in with the lamps on and was held
  against a reference without them).
- Tests: `RtxViewsTest`, `RtxViewpointTest`, `RtxBenchRunTest`.
- `apps/components_tests/rtxvulkan/trace/visibility/trail.cpp`: the trail tests advance the
  placement table after each hand-over, and the clamp's test runs under no reuse with its bounds
  and figures measured again.
- The one pipeline: the request's defaults, `BounceReuseRule` removed, the bench report's mode per
  place removed (every place runs the run's), and the tests that named the rule.

## After the redesign's cheap fixes: removed (2026-10-06)

Decision 4 of `redesign.md` made the reuse's cheap corrections first — the validation at the whole
rate, the far-ground flag, allocation on demand, the pairs skip — and then ran the A/B again
(`noise --ab=bounce-reuse=…,off --suite=bounce`, strafed, walked and still), with the rule: remove
the reuse if it does not gain.

Noise / bias, reuse against off:

| place, leg | temporal | spatiotemporal | off |
|---|---|---|---|
| mages' guild, still | 0.49 / 1.14 | 0.48 / 1.12 | 0.48 / 1.14 |
| guild's planter, still | 0.44 / 1.33 | 0.43 / 1.30 | 0.42 / 1.29 |
| Ahemmusa's yurt, still | 0.48 / 1.36 | 0.47 / 1.35 | 0.49 / 1.35 |
| **chamber, still** | 0.57 / **3.15** | 0.55 / **2.50** | 0.55 / **1.63** |
| **chamber, strafed** | 1.50 / **3.19** | 1.46 / **2.90** | 1.44 / **2.11** |
| **chamber, walked** | 1.56 / **3.46** | 1.51 / **3.14** | 1.50 / **2.08** |
| Seyda Neen's pier, still | 0.46 / 1.33 | 0.46 / 1.33 | 0.46 / 1.33 |
| Seyda Neen's pond, still | 0.51 / 1.48 | 0.51 / 1.47 | 0.51 / 1.47 |

The other legs agree within 0.02. **No place gains.** The chamber, the one place the reuse was
kept for, lost its darkening to the denoiser's own fixes since (bias 2.60 → 1.63 with no reuse),
and the reuse now adds bias there. So the reuse, its four kernels, its reservoirs, the pairing
textures and `--bounce-reuse` are removed; the reuse's cost, 0.24 to 1.8 ms where it ran, is gone
with them, and D4's replay is not built.
