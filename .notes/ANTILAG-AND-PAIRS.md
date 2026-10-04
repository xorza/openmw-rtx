# Anti-lag in the accumulator, and paired spatial reuse: research and plan

Status: a proposal. Nothing in this file is implemented. Written 2026-10-04.

This file has two parts. Part A removes the trail that a moving actor leaves in the bounce and the
sky's fill. Part B makes the bounce's spatial reuse cheaper (ReSTIR GI, `.notes/RESTIR-GI.md` §10).
Each part gives the measurements that started it, what the field does, the design for this tree,
the steps, the tests and the gates.

---

# Part A — anti-lag in the accumulator

## A1. The problem, as measured

A bar moves over a floor at 4 units a frame (about 0.74 traced pixels), seen from 300 units above
at 96 pixels square. The picture after 40 frames of motion is compared with a still, converged
picture of the bar at the same place. *Edge lag* is how far the half-way level of the trailing edge
stands behind the still picture's, in output pixels. *Tail* is the darkness left in the lit part
behind the edge, in pixels of full darkness. Each figure is the mean of four runs.

| What the bar blocks | Upscale | Bounce reuse | Edge lag | Tail |
|---|---|---|---|---|
| The sun, bar 100 over the floor | native | off | 0.27 | 0.15 |
| The sun | `quality` | off | 1.73 | 0.25 |
| The sun, raw frame | `quality` | off | 1.66 | 0.59 |
| The sky's fill, bar 20 over the floor | native | off | 10.28 | 2.04 |
| The sky's fill | native | temporal | 17.37 | 2.64 |
| The sky's fill | native | spatiotemporal | 16.61 | 2.61 |
| The sky's fill, `ACCUMULATE_FRAMES` 16 | native | spatiotemporal | 13.00 | 2.38 |
| The sky's fill, raw frame | native | spatiotemporal | 0.90 | 0.66 |

**The trail that a player sees under a walking NPC is the sky's fill, not the sun.** The fill goes
through the accumulator (`accumulate.comp`) and the wavelet. The sun's shadow goes through the
shadow denoiser, and trails 40 to 60 times less. Its 1.7 pixels at `quality` are FSR's: FSR clamps
its history to a 3×3 box of render pixels, 1.5 output pixels at `quality`, and a full reactive or
transparency mask reduced the lag only to 1.13–1.16 pixels.

**Why the accumulator trails:**

1. **Nothing in it detects a change of the light on a surface that did not move.** It drops a
   history only where the surface test fails (`heldSurfaceMatches`). There is no clamp: the comment
   at the blend says why there is no outlier clamp on the *sample*, and nothing clamps the
   *history*.
2. **Its history is the wavelet's first level** (SVGF's feedback, `atrous.comp`). The old dark
   area is blurred again every frame, so the trail spreads sideways as it decays.
3. **The bounce reuse makes it longer** (10 → 17 pixels). Each reservoir carries samples from frames
   before, so the accumulator's input is correlated in time, and the accumulator's mean holds the
   past longer.
4. **A shorter history is not the cure**: 16 frames give 13 pixels, at 40–80% more still noise
   (`look.h`, `ACCUMULATE_FRAMES`).

The experiment that measured this is `RtxBounceTrailTest` (`trail.cpp`), which holds the sun's
figure today and takes the sky's bound in step A.3.

## A2. What the field does

| | A-SVGF (Schied 2018) | ReLAX (NRD) | SIGMA (NRD, shadows) |
|---|---|---|---|
| Change detection | Temporal gradient: re-shade a stratified subset (1 in 3×3) of last frame's samples with their own random numbers, at their new places | A fast history (6 frames) beside the slow one (30); the slow one is clamped to the fast one's 5×5 colour box | A short history (5 frames) clamped to the spatially filtered current frame's 5×5 box |
| What reacts | The blend factor per pixel, from the reconstructed gradient | Clamp, then "acceleration" toward the input, then a partial reset | History length × (1 − √(clamp change)) |
| History | The filtered output (SVGF feedback) | The clamped temporal accumulation, **not** the à-trous output | The stabilized output |
| Made for | Path-traced signals with replayable samples | RTXDI / ReSTIR signals | One shadow bit a pixel |

**ReLAX's numbers** (`NRDSettings.h`, `RELAX_HistoryClamping.cs.hlsl`):

- `diffuseMaxAccumulatedFrameNum = 30`, `diffuseMaxFastAccumulatedFrameNum = 6`: the fast history
  is "usually 5x–7x shorter than the main history".
- Both histories blend the same input: `alpha = max(1 / (cap + 1), 1 / historyLength)`, each with
  its own cap.
- `fastHistoryClampingSigmaScale = 2.0`: the slow history is clamped, in YCoCg, to the fast
  history's 5×5 mean ± 2σ, and the box is grown to include the centre pixel's fast value ("to
  minimize introduced bias").
- **Clamping factor** `f = (clamped − slow) / (fast − slow)` on luma, in [0, 1].
- **Acceleration**: `accelerationAmount = 0.3`, scaled by 10. Both histories move toward the 5×5
  mean of the raw input, by `0.3 × 10 × f × |fast − slow|` in luma, never past the input.
- **Reset**: `resetAmount = 0.5`. Where `|slow − noisy mean|` is larger than `4.5 σ_spatial(fast) +
  0.5 σ_temporal(noisy)`, both histories blend toward the raw input by up to half.
- **Second-moment correction**: the stored second moment moves by `L_out² − L_in²`, so the variance
  stays the variance of the value kept.
- NRD advises a history in seconds, not frames (`RELAX_DEFAULT_ACCUMULATION_TIME = 0.5 s`).

**Why not A-SVGF here.** Its gradient re-shades last frame's sample with last frame's random
numbers. The bounce under reuse is a reservoir, not a draw that can be replayed, and a replay of the
trace's bounce would cost a second bounce ray at one pixel in nine. The fast-history clamp needs no
replay, and NVIDIA built it for exactly the ReSTIR signals this tree now has.

**Licence.** NRD is under NVIDIA's licence, which does not fit a GPL program, as RTXDI did not. The
plan takes the published method and its default values, and copies no code.

## A3. Decisions for this tree

**AD1. A fast history beside the slow one, for the bounce and the fill.** Two more image pairs at
the traced extent: the fast bounce and the fast fill, in `ACCUMULATE_COLOUR`. Both reproject by the
same bilinear, surface-tested taps the slow history uses (`historyFootprint`, `sameSurface`), so a
surface change resets both at once. The fast cap starts at ReLAX's 6
(`ACCUMULATE_FAST_FRAMES`), and a step sweeps it.

**AD2. The clamp in a pass of its own, after the accumulator and before the wavelet.** The clamp
reads the fast history of a pixel's neighbours, which exist only when the accumulator finished every
pixel. A new kernel, `accumulateclamp.comp`, with a 5×5 window in shared memory, as ReLAX's. It
writes what the cascade reads (`blendedOut`, `fillBlendedOut`) and the moments.

**AD3. Clamp on luminance, and move the fill with the same factor.** `CHANNEL_FILL` is a share of
the bounce (`Arriving`, `bouncereuse.h`). A per-channel YCoCg clamp of each could make the fill
larger than the bounce it is a share of. The clamp takes the box on the bounce's YCoCg, computes
ReLAX's factor `f` on luma, and moves the slow bounce and the slow fill toward their fast values by
the same `f`, as ReLAX moves its SH by `f`.

**AD4. Acceleration and reset as ReLAX publishes them, then simplify by measurement.** Step A.4
measures each of the three (clamp, acceleration, reset) alone. A part that does not pay is removed
and recorded here.

*Measured* (`RtxBounceTrailTest`, the sky under the spatiotemporal reuse): the clamp alone 14.24
pixels of lag; with the acceleration 13.07; with the reset alone 14.24, and with both 13.07. The
reset never fired: its threshold of 4.5 deviations is wider than any gap the sky's change opens
against samples of one bounce each. **The acceleration is kept, the reset is removed.**

**AD5. The history question is a measurement.** SVGF feeds back the wavelet's first level. ReLAX
keeps the clamped accumulation. The feedback makes a quieter history and a wider trail. Step A.5
measures both. The default is the feedback, as now, until the measurement says otherwise.

**AD6. The math in a header both sides read.** The clamp factor, the acceleration and the reset are
small pure functions in `accumulate.h` (`RTX_SHADER`), as `reconnectionJacobian` is in
`bouncereuse.h`. A host test holds each to hand-computed values.

**AD7. Deterministic.** A fixed-order 5×5 sum in shared memory, no subgroup operation.
`./omw repeat` stays identical.

**AD8. No new frame-rate rule in this part.** The history in seconds (NRD's advice) changes every
history the tree keeps. It is a separate question, recorded in A8.

## A4. Where each part lives

- `components/rtx/shaders/look.h`: `ACCUMULATE_FAST_FRAMES`, `ACCUMULATE_CLAMP_SPREAD` (2.0),
  `ACCUMULATE_ACCELERATION` (3.0, ReLAX's 0.3 × 10), `ACCUMULATE_RESET_SPATIAL` (4.5),
  `ACCUMULATE_RESET_TEMPORAL` (0.5), `ACCUMULATE_RESET_AMOUNT` (0.5), each with the measurement that
  set it.
- `components/rtx/shaders/accumulate.h`: the clamp kernel's bindings and workgroup, and the pure
  functions of AD6.
- `components/rtxvulkan/shaders/trace/denoise/accumulate.comp`: blends the fast history beside the
  slow one and writes both, unclamped.
- `components/rtxvulkan/shaders/trace/denoise/accumulateclamp.comp`: new, AD2.
- `components/rtxvulkan/trace/denoise/accumulatepass.{hpp,cpp}`: records the clamp after the blend,
  under the zone `accumulate` (one zone, or `RtxSourceTreeTest`'s count and `sMaxGpuZones` move).
- `components/rtxvulkan/trace/denoise/denoisehistory.{hpp,cpp}`: the two new pairs in
  `AccumulateImages`, made at `resize`, and turned and discarded with the others.
- `docs/rtx/architecture.md`: the denoiser's paragraph names the clamp.

## A5. Memory and cost

Two pairs of `ACCUMULATE_COLOUR` (16 bytes a texel): 64 bytes a traced pixel, 56 MiB at 1280×720.
The clamp reads two 12×12 squares a workgroup and writes three images: ReLAX's clamp pass is in
the same class. The estimate is under 0.1 ms at 1280×720; step A.6 measures it.

## A6. Steps

Each step ends with `./omw test` and the gates it names.

**Step A.5 — the history: feedback or not (AD5).** *Measured and waiting:* see
`ANTILAG-AND-PAIRS_QUESTIONS.md`, Q1. The feedback stays until the answer.

**Step A.5 — the history: feedback or not (AD5).** The cascade's first level writes the history
(today), against the clamp's output as the history (ReLAX). Measured on `RtxBounceTrailTest`,
`noise --suite=bounce` still, strafed and walked, and `--walk=150`, which is where a history's lag
shows.

**Step A.6 — the fast cap.** A sweep of `ACCUMULATE_FAST_FRAMES` over 4, 6 and 8 against the trail
and the still noise. The value and its measurement go into `look.h`.

**Step A.7 — the cost and the record.** `./omw release bench --suite=bounce` from the background,
a warm-up leg, off and on back to back: the `accumulate` zone's median and p99. `repeat --pairs=10`,
`./omw gate`. The result goes into A9 of this file.

## A7. Acceptance

- **Trail:** the sky's edge lag under the spatiotemporal reuse falls from 16.6 pixels to under 4,
  and its tail from 2.6 to under 1.
- **Noise:** no place of `noise --suite=bounce` noisier by more than 0.02 still, and none noisier
  strafed or walked.
- **Bias:** within 0.1 of today's at every place.
- **Cost:** the accumulator's zone rises by at most 0.1 ms median at 1280×720.
- **Determinism:** `repeat --pairs=10` identical.

## A8. Not in this part

- **The history in seconds** (NRD's advice): at 120 Hz today's 32 frames are 0.27 s, at 60 Hz
  0.53 s. A change for every history the tree keeps.
- **FSR's band on the sun's shadow** (A1): a reactive value from the change of the denoised
  visibility, written by the composite into `CHANNEL_UPSCALE_MASKS`. The composite adds the
  shadowed light linearly (`compose.glsl`), so the colour change is exactly `shadowed × Δvisibility`.
  Worth about 0.5 pixel at the edge. A separate change.
- **The thin sun shadow that the denoiser washes out** (`.notes/ISSUES.md`).

---

# Part B — paired spatial reuse, and cheaper visibility rays

## B1. The problem, as measured

`.notes/RESTIR-GI.md` §10, Step 7: the reuse costs 1.6–2.2 ms median for its three zones, against
§7's limit of 1.0 ms. The resolve is most of it, 1.0–1.7 ms. The resolve's visibility query, four
ways (release, median ms, two legs that agree within 0.02):

| Variant | Guild | Planter | Pier |
|---|---|---|---|
| e0, today: nearest hit, cutouts at the cone's level | 1.17 | 1.47–1.49 | 1.57–1.59 |
| e1, first hit ends the search | 1.15 | 1.46–1.47 | 1.60 |
| e1b, e1 and cutouts at the finest level | 1.08 | 1.34 | 1.48 |
| e2, no rays (a bound, not a design) | 0.32 | 0.32 | 0.30 |

**The rays are three quarters of the resolve**: 0.85 ms of 1.17 at the guild, for up to four rays
a pixel (two neighbours, a shift ray and an MIS ray each) and a final ray where the pixel keeps its
own carried sample.

## B2. What the field does

**Paired spatial reuse** (Lin, Kettunen, Wyman 2026, *ReSTIR PT Enhanced*, §3):

- For pixel A to reuse from B, the MIS needs B's sample shifted to A *and* A's sample shifted to
  B. **When B reuses from A, it needs the same two shifts.** So pixels are linked in pairs, A's link
  is B's link, and each shift is computed once for both.
- **A pre-pass shifts each pixel's path to all its paired neighbours; a second pass resamples.**
- **The pairing is a texture of coordinate deltas** that is its own inverse. It is made by filling an
  even-sized texture (254×254) with consecutive link indices, then permuting it by `n_σ` tiled 2×2
  random shuffles, each other one offset diagonally by one, with
  `n_σ ≈ σ² / 2 + 0.5` (plus terms in `σ⁻¹` to `σ⁻³` for small σ, Eq. 3). The deltas then follow a
  normal distribution of deviation σ. Links that cross the texture's edge are wrapped so the texture
  tiles. The deltas pack into two signed bytes.
- **σ for a disc of radius R**: the same mean distance as a uniform disc when `σ = √(8 / (9π)) R`,
  about `0.532 R`.
- **One texture a neighbour, each of a different size** (254, 230 and 210 for three neighbours), so
  their repeats do not line up.
- **The texture is self-inverting, so it must change every frame.** Each frame flips, mirrors,
  transposes and offsets each texture at random. "Empirically, this completely solves the problem."
- **Result:** the spatial reuse is 1.63 times faster on average (the split into two passes costs
  some of the half), and the FLIP error is lower, because a Gaussian puts more neighbours near the
  centre, where they are more often compatible.

**Visibility rays** (NVIDIA, *Best Practices for Using NVIDIA RTX Ray Tracing*): "terminate rays on
the first hit when possible … as for shadow rays", and "minimize the non-opaque area": an any-hit
test stops the hardware's search. This tree's shadow rays already use the first hit and the finest
cutout level (`passageToward`, `traversal.glsl`, which gives the measurement of JCGT 10(1) 2021
against its own cost).

## B3. Decisions for this tree

**BD1. Visibility rays are any-hit rays at the finest cutout level** (e1b), in a function of
`traversal.glsl` beside `passageToward`: solids only, no face culled (as the bounce's own ray),
`gl_RayFlagsTerminateOnFirstHitEXT`, a cone of width nought. The answer is the same as today's
except where a cutout's finest level and the cone's level disagree. A sample the bounce saw through
a hole at the cone's level can be hidden at the finest level. Step B.1 measures this at a foliage
place.

**BD2. Only the visibility bits cross between the passes.** Of a pair's terms, only visibility
needs a ray. The target at the other point and the Jacobian come from the reservoir and the origin,
which both passes read. So the pre-pass stores **one bit a link** — whether the partner's visible
point sees this pixel's sample — and the resolve computes everything else again:

- A's canonical MIS term for link i: `p̂_B(T(y_A)) |J|`, analytic, times **A's own bit i**.
- A's term for B's sample: `p̂_A(T(y_B))`, analytic, times **B's bit i** (B's link i points back to A).

**BD3. The similarity test becomes symmetric.** The pre-pass at A traces for a link only where A
accepts B, and the resolve at A reads B's bit, which B traced only where B accepted A. Today's depth
test is relative to the centre's distance, `|d_B − d_A| ≤ BOUNCE_DEPTH d_A`, which is not
symmetric. It becomes `|d_B − d_A| ≤ BOUNCE_DEPTH min(d_A, d_B)`. The normal test is symmetric
already. A host test holds `alike(A, B) == alike(B, A)` over a table of pairs.

**BD4. Two pairing textures, 254 and 230 texels a side** (`BOUNCE_NEIGHBOURS` = 2), at
`σ = 0.532 R`, where `R = max(BOUNCE_RADIUS_SHARE × H, BOUNCE_RADIUS_LEAST)` and H is the traced
height. **Made on the host at `BounceReservoirs::resize`**, from a fixed seed, so a run is the same
run twice. The cost is about `n_σ × 254² / 4` block shuffles: at 720 traced rows, σ ≈ 11.5 and
`n_σ` ≈ 67, about a million; at 1440 rows, σ ≈ 23 and `n_σ` ≈ 265. Step B.2 measures it. If it is
more than a few milliseconds, it moves off the frame path into a job, as the kernels' compile is.

**BD5. The frame's transform, one for the whole frame.** From `frame.mFrame` and `SEED_BOUNCE_PAIRS`:
a flip in x, a flip in y, a transpose, and an offset. With `M` the flips and the transpose
(orthogonal) and `o` the offset, pixel `p` reads the delta `d = tex[(M p + o) mod S]`, and its
partner is `p + Mᵀ d`. The partner reads `tex[(M p + o + d) mod S] = −d`, so its partner is `p`
again: the pairing stays its own inverse. A host test holds this over every pixel of a small frame
and every one of the eight transforms.

**BD6. A partner outside the frame is no link**, for both of them. The one outside does not exist,
so nothing reads a bit it did not write.

**BD7. The pre-pass is a ray generation launch, `bouncepairs.rgen`**, between the temporal merge and
the resolve, under the zone `bounce pairs`, a kernel of `VisibilityPass` beside the three of today.
It reads this frame's reservoirs after the temporal merge and the origins. It writes one word a
pixel (a bit a link) into a buffer of `BounceReservoirs` at the reservoirs' stride, bound in the
pushed set (`BIND_BOUNCE_PAIRED`), and the two pairing textures (`BIND_BOUNCE_PAIRING_*`).

**BD8. The resolve traces only its final ray.** The neighbours come from the pairing, not from the
spiral (`neighbourOf` goes), and the shift and MIS rays become reads of the bits. The final ray
stays where it is today: a pixel that keeps its own carried sample asks it.

**BD9. The MIS is unchanged.** Pairing changes which neighbours a pixel takes and when the rays
are traced, not the weights. The neighbours are still chosen independently of the samples, so the
reuse stays unbiased.

## B4. Expected cost

From B1: four rays at the guild cost 0.85 ms, so two cost about 0.43 ms, and e1b takes about
0.09 ms off the rays. The zones after both: validate 0.16 + temporal 0.31 + pairs ≈ 0.40 + resolve
≈ 0.35 = **about 1.2 ms at the guild**, against 1.64 today. **That does not meet §7's 1.0 ms.** The
rest needs one of these, each measured as its own step:

- **One neighbour, not two**: one ray less a pixel, about 0.2 ms. `.notes/RESTIR-GI.md` §10
  measured four neighbours at 0.01 better than two; one is untested.
- **The temporal merge, 0.31 ms for a dispatch that traces nothing**: it reads up to four origins
  (28 bytes each) and a reservoir for each pixel, and reduces in shared memory with six barriers.
  A profile (`nsys`) says where its time goes.
- **The validation, 0.16 ms**: one pixel in eight shades a path end again. Its share is a
  measurement in §10 (D10).

## B5. Steps

**Step B.1 — visibility rays at the finest level (BD1).** The new traversal function, which the
resolve's `bounceSeen` uses. The validation's ray stays a nearest-hit trace, because it compares
the distance of what it meets with the sample's. **Gates:** `repeat --pairs=10` identical;
`shot --views=all --map --upscale=off` against a baseline from before the step moves no picture past
the denoiser's noise, and the foliage places are named in the record. `release bench --suite=bounce`
with a warm-up leg: the resolve's median.

**Step B.2 — the pairing textures (BD4, BD5).** Host code only, in `components/rtx/` (a core fact,
no graphics API): `BouncePairing`, which makes one self-inverting texture of a given size and σ.

- **Host tests:**
  - every texel's partner's partner is itself;
  - every delta's partner is inside the texture after the wrap;
  - the deviation of the deltas is σ to 5%, over σ = 0.8, 4, 11.5 and 23;
  - two seeds give two different textures, and one seed gives the same one twice;
  - BD5's transform keeps the inverse, over all eight transforms and an offset.
- **Measured:** the time to make both textures at 720 and 1440 traced rows, in the test's output.

**Step B.3 — the pre-pass and the bits (BD2, BD3, BD6, BD7).** The kernel, the buffer, the
bindings, the zone. The resolve still traces its own rays, and **a debug comparison** checks that
every bit the pre-pass wrote is the answer the resolve's own ray gives for the same link (a GPU test,
which then goes). **Host test:** the symmetric similarity (BD3).

**Step B.4 — the resolve reads the bits (BD8).** Its shift and MIS rays go.

- **GPU tests:** the existing `RtxBounceReuseTest` suite passes unchanged in its claims:
  - the mean of every mode within 2% of the plain bounce;
  - the spatial half lowers the error below the temporal half;
  - no light through a wall of no thickness;
  - the lamp's bounce gone on the eighth frame.
- **Gates:** `repeat --pairs=10` identical. `noise --suite=bounce` still, strafed and walked: no
  place noisier by more than 0.02 against §10, and the paper's lower error is expected, not assumed.
  `kernels --against` names `bounceresolve` and the new kernel only.

**Step B.5 — the cost, and the next lever.** `release bench --suite=bounce`, warm-up leg, off and
on, back to back, from the background: the four zones' medians and p99. If the guild is over
1.0 ms: one neighbour (B4), measured on noise and cost, then the temporal merge's profile. The
result goes into `.notes/RESTIR-GI.md` §10, Step 7.

**Step B.6 — `docs/rtx/architecture.md`** names the pre-pass in the bounce's paragraph and in the
record order.

## B6. Acceptance

- **Cost:** the reuse's zones together at most 1.0 ms median at the guild at 1280×720 traced, or a
  recorded decision on what moves (§7).
- **Noise:** no place of `noise --suite=bounce` noisier by more than 0.02 still, strafed or walked,
  against `.notes/RESTIR-GI.md` §10.
- **Correctness:** every claim of `RtxBounceReuseTest` holds; `repeat --pairs=10` identical.
- **AMD:** the new kernel compiles under drm-shim.

---

## Order of the two parts

Part A first: it fixes what a player sees, and its acceptance does not depend on Part B. Part B's
noise gates read `noise --suite=bounce`, which Part A changes, so Part B takes its baseline after
Part A lands.

## Sources

- [Lin, Kettunen, Wyman 2026, *ReSTIR PT Enhanced: Algorithmic Advances for Faster and More Robust ReSTIR Path Tracing*](https://research.nvidia.com/labs/rtr/publication/lin2026restirptenhanced/) — paired spatial reuse (§3, Eq. 3, Fig. 8), the σ for a disc, the per-frame transform
- [NVIDIA NRD](https://github.com/NVIDIA-RTX/NRD) — `Include/NRDSettings.h` (ReLAX and SIGMA settings), `Shaders/RELAX_HistoryClamping.cs.hlsl`, `Shaders/RELAX_TemporalAccumulation.cs.hlsl`, `Source/Denoisers/Relax_Diffuse.hpp` (pass order and what each keeps), `Shaders/SIGMA_TemporalStabilization.cs.hlsl`
- [Schied, Peters, Dachsbacher 2018, *Gradient Estimation for Real-Time Adaptive Temporal Filtering* (A-SVGF)](https://cg.ivd.kit.edu/atf.php)
- [NVIDIA, *Best Practices for Using NVIDIA RTX Ray Tracing (Updated)*](https://developer.nvidia.com/blog/best-practices-for-using-nvidia-rtx-ray-tracing-updated/) — first-hit termination for shadow rays, the cost of any-hit tests
- [AMD FidelityFX SDK, shadow denoiser](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK) — `ffx_denoiser_shadows_tileclassification.h`, which the port in `shadowtiles.comp` matches
- [AMD FSR 3.1 documentation](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-upscaler/) — the reactive and transparency-and-composition masks
- [Wyman et al. 2023, *A Gentle Introduction to ReSTIR*, course notes](https://intro-to-restir.cwyman.org/presentations/2023ReSTIR_Course_Notes.pdf) — Algorithm 7, the pairwise MIS this design keeps
