# ReSTIR GI for the bounce: research and plan

Status: built on the branch `restir-gi`, which §10 records against the plan. The plan was written
2026-10-03; the cost bench of Step 7 is still to run.

This file says why the bounce is the noise that is left at the Balmora Mages Guild, what the
field does about such noise, and how ReSTIR GI fits into this tree. The last sections give the
steps, the tests and the measurements that decide each step.

---

## 1. The problem, as measured

The place: `balmora-mages-guild` and `probe-guild-planter` in `files/rtx/views.cfg`, at night
(23:13, fog), lit by lamps only.

The last figures, from the session of 2026-10-03, are the frame's noise from `./omw noise`, in
levels of 255. Phase 0 measures them again, because the tree changed after them.

| Place | Still | After `--strafe=150` |
|---|---|---|
| Mages' guild | 0.59 | 1.32 |
| Guild's planter | 0.74 | 1.82 |
| Guild's planter, the bounce removed (before the lamp split) | 0.45 | 1.06 |

**The bounce is most of the noise that is left.** The lamps' direct light goes through the shadow
denoiser now (`CHANNEL_SHADOWED`), and the bounce goes alone through the accumulator and the
wavelet (`CHANNEL_INDIRECT`, `CHANNEL_FILL`).

**What was tried and did not help: a radiance cache** in the style of NVIDIA's SHaRC (a world-space
hash grid of bounce light). It changed nothing: 0.59 / 1.32 → 0.60 / 1.33 at the guild, 0.74 /
1.82 → 0.73 / 1.82 at the planter. A cache removes the noise of the light *at the far end* of the
bounce (one lamp, one shadow ray and one ambient ray at the hit). After the denoiser, that part is
small. The noise that is left is in the *direction* of the bounce: which surface the one ray
meets, bright or dark. The patch is at `~/.cache/omw-refactor/cache/radiance-cache.patch`.

**ReSTIR GI attacks the direction.** Each pixel keeps the bounce sample that was worth most, and
takes better samples from its own past frames and from its neighbours. In a room lit by a few
lamps, most directions meet dark walls and a few meet a lit patch. ReSTIR GI finds the lit patch
once and keeps it, where one random ray a frame finds it rarely.

---

## 2. What ReSTIR GI is

Ouyang et al. 2021. The terms below are the paper's, and the course notes' (Wyman et al. 2023).

- **Visible point** `x_v`: the surface the eye ray found, with its normal `n_v`. In this tree,
  the solid that `shadeSolid` shades.
- **Sample point** `x_s`: where the bounce ray from `x_v` hit, with its normal `n_s`, and the
  radiance `L_o` that leaves `x_s` toward `x_v`. In this tree, `bounceArriving`'s hit and
  `lightAtPathEnd`'s light. A ray that escapes is a sample at infinity: a direction, and the sky's
  light along it.
- **Reservoir**: one kept sample, its unbiased contribution weight `W`, and its confidence `c`
  (often called `M`): how many candidates it stands for.
- **Target function** `p̂`: what the resampling makes the kept samples proportional to. The paper
  tests `p̂ = L_o · f · cos` (eq. 9) and the simpler `p̂ = L_o` (eq. 10). It found eq. 10 more stable
  for spatial reuse, at a higher variance. Both are taken as luminance.
- **The estimate** at a pixel: `f(x_v, ω) · L_o · cos θ · W`, where `ω` points from `x_v` to the
  kept `x_s`. This replaces `L_o · cos θ / pdf(ω)` of the one-ray estimate.
- **The candidate weight**: `w = p̂(x) / p(x)` for a new candidate. A reservoir merged from
  another pixel or frame adds `w = m · p̂(x) · W · |J|`, where `m` is its MIS weight.
- **The Jacobian** of the reconnection shift, for a sample of pixel `q` reused at pixel `r`
  (paper eq. 11):

  `|J| = (|cos φ_r| / |cos φ_q|) · (‖x_v,q − x_s‖² / ‖x_v,r − x_s‖²)`

  where `φ_q` and `φ_r` are the angles between `n_s` and the two segments to `x_s`. A sample at
  infinity has `|J| = 1`. The paper's Figure 7 shows the wrong light that a missing Jacobian gives.
- **Temporal reuse**: merge the pixel's new candidate with last frame's reservoir at the pixel the
  motion vector names. **Spatial reuse**: merge reservoirs of a few neighbours, each shifted to this
  pixel by the Jacobian, and weighted by MIS.

**Why it is not free of bias in a real-time form**, and the parts this plan keeps or accepts:

1. `L_o` is stored for the direction toward the original `x_v`, and reused toward another pixel.
   That is exact only where `x_s` is Lambertian (course §6). Every vanilla surface is. A PBR
   replacer's glossy `x_s` gives a small error. *Accepted, and measured at a PBR place.*
2. A neighbour's sample can be hidden from this pixel. *Fixed by a visibility ray at the final
   shade (§4, D7).*
3. Without a visibility ray inside the MIS weights, the weights are approximate. *Fixed by a ray
   to each neighbour inside the weights (D8). The basic form, without the ray, stays as an A/B
   leg.*
4. A reused `L_o` goes stale when the lights or the scene change. *Bounded by an age cap and
   corrected by validation (D10).*

---

## 3. What the field does

| | ReSTIR GI paper | RTXDI SDK defaults | Kajiya (Embark) | Course notes |
|---|---|---|---|---|
| Candidate direction | uniform hemisphere: lower variance than cosine, mostly at grazing light | — | uniform for temporal | — |
| Target | `L_o` (eq. 10) more stable | `L_o · BRDF · cos` | luminance × BRDF | — |
| Confidence cap, temporal | 30 | `maxHistoryLength = 8` | 10 | "5–30, start at 20" |
| Confidence cap, spatial | 500 | — | — | — |
| Age cap | — | `maxReservoirAge = 30` | — | — |
| Neighbours | 3, or 9 where the reservoir is young | `numSamples = 2` | 8, then 5 (two passes) | — |
| Radius | adaptive: 10% of the image, halved on a miss, at least 3 px | 32 px | 12–32 px, then 6–16 px | — |
| Similarity | normals within 25°, depth within 5% | `normalThreshold = 0.6`, `depthThreshold = 0.1` | normal, depth, SSAO | reset `c` only from the G-buffer |
| Bias correction | unbiased (visibility ray) or geometric test | `Basic` (no ray), `Raytraced` optional | explicit bias | pairwise MIS, defensive form |
| Final visibility ray | when not traced in reuse | on | screen-space march | — |
| Validation | re-trace every 6 frames in the candidate pass | — | every 3rd frame, `M` reduced on change | — |
| Boiling filter | — | on, strength 0.2 | — | — |
| Disocclusion | reset, spatial only | fallback to the zero-motion pixel | "very noisy", open | `c = 0` |
| Cost | 4.6 ms reuse at 1080p, RTX 3080 (UE4, 2021) | — | 2.3 ms whole GI, half res, RX 6800 XT | — |

Points that every source makes:

- **Cap the confidence.** Without a cap, temporal reuse keeps one sample for ever, and spatial
  reuse grows `c` without bound. The output then converges to a wrong value (course, *Confidence weights*).
- **The Jacobian is always worth its cost** (paper §4.3: "no reason not to include this factor").
- **ReSTIR output is correlated in space and time.** A denoiser that estimates variance from
  temporal moments, as SVGF does, misreads it (paper §6.1). NVIDIA built ReLAX, an SVGF
  derivative, for RTXDI's ReSTIR signals. This tree's wavelet is SVGF's.
- **Reuse needs a canonical sample**: the pixel's own fresh candidate, from a source pdf that
  covers the whole integrand, so the result can be unbiased (course Def. 3.2.1).

**Licence.** RTXDI is under NVIDIA's RTX SDK licence, which does not fit a GPL program, as SHaRC
did not. The plan takes the published method and the default values, and copies no code.

---

## 4. Decisions for this tree

Each decision says what the tree's rules ask of it.

**D1. Diffuse half only.** The reuse resamples the bounce that goes to `CHANNEL_INDIRECT` and
`CHANNEL_FILL`. The lobe's bounce stays as it is, through the glossy filter. Reconnection on a
glossy visible point fails for low roughness (paper §6.1, course §6.4). Vanilla content has no
lobe, so the guild is all diffuse.

**D2. The candidate is the trace's own bounce, and no ray is added for it.** `bounceLight` already
traces one ray and shades its hit. That ray and its light become the pixel's canonical candidate.
Where `bounceDraw` chose the lobe, the diffuse candidate is empty, with confidence 1 and weight
nought. That is unbiased, because the source pdf of the diffuse candidate already carries the
chance `1 − chance` that the diffuse half was drawn: the expected weight is the integral of `p̂`.

**D3. Demodulated, as the channel is.** The integrand per unit albedo is
`D(ω) · L_o`, with `D(ω) = side(ω) · max(cos, 0) / π · (1 − F(h))`: the sheet's side and its
transmission (`sampledFace`), the face test (`behindTheFace`), and the share the lobe takes. With
the reuse off, `W = 1 / p(ω)` and the estimate is exactly today's bounce. **Step 2 holds that as a
test.**

**D4. The fill goes with the sample.** `L_o` has two parts, the whole and the fill (`Arriving`).
The reservoir keeps both, and selects by the whole. The fill is then the same sample's fill, by the
same weight, so `CHANNEL_FILL` stays the share of `CHANNEL_INDIRECT` it is today.

**D5. Escapes are samples at infinity.** A ray to the sky, and the far ground that `BOUNCE_REACH`
hands the escape, keep a direction and the sky's light. Their Jacobian is one, and their visibility
ray runs to `frame.mReach`. In a room an escape brings nothing, `p̂ = 0`, and it is never kept.

**D6. Offsets, not world positions.** The tree never subtracts two six-figure positions on the
device (`reproject.glsl`). A reservoir stores `x_s − x_v`. A pixel rebuilds its `x_v` from the eye
and the distance along its ray (`positionAlong`), and a temporal sample is carried to this frame by
`frame.mCameraMotion`, as the motion vectors are.

**D7. A visibility ray at the final shade, always.** Morrowind builds rooms of sheets with no
thickness. A neighbour's lit patch on the far side of a wall is the first leak this would show.
One ray a pixel, from `x_v` (lifted as a shadow ray is) to the kept `x_s`. A hidden sample
contributes nothing, and the temporal reservoir it came from is cleared.

**D8. MIS: pairwise, with confidences, and a visibility ray inside the weights.** The generalized
pairwise MIS of the course (eq. 7.5, the defensive form 7.6), with the canonical sample as this
pixel's own. The weights read `p̂` at each neighbour's visible point, from the neighbour's origin
record (§5.2), times whether that point sees the sample: one ray a neighbour, about two more rays a
pixel. *Decided (Q2):* the basic form without the ray (RTXDI's default) has less total error in
three of the paper's four scenes, but its error is a fixed shift near every visibility change,
which no number of frames averages away. The basic form is built as an A/B leg, so Step 4 shows
what the ray buys.

**D9. Uniform frames.** Every pixel runs the same number of neighbours, with a select and not a
skip where a neighbour fails its test (`AGENTS.md`: one path through a shader). The paper's
adaptive count (9 or 3) and adaptive radius are per-pixel divergence and a cost that moves with
the history: not taken.

**D10. Validation: a fixed share of pixels every frame, never a frame every N.** The paper and
Kajiya re-trace all samples every 3rd or 6th frame. That is a batch behind a threshold, which this
tree does not take. A fixed share (for example one pixel in eight, by an interleaved pattern that
turns each frame) re-traces and re-shades its kept sample each frame. A sample whose light moved
past a tolerance is cleared. Morrowind's lamps flicker and pulse (`LightController`), so a stale
`L_o` is a real case here, not a corner. The age cap bounds what validation misses.

**D11. Deterministic, as the frame is.** Every draw comes from a `SEED_` of its own in
`random.glsl`, off `pixelKey`. A permutation of the temporal fetch uses one number for the whole
frame, as RTXDI does. The boiling filter's group mean is a fixed-order reduction in shared memory,
and not `subgroupAdd`, whose order is the driver's (`AGENTS.md`: a shader's float arithmetic is
the build's). `./omw repeat` must stay identical with the reuse on.

**D12. A reconstruction rule, not a branch in the game.** Whether the bounce is reused is a fact of
how the frame is put back together: `Rtx::Reconstruction::resolve` decides it, with the denoiser,
the jitter and the noise source. A picture inside the interface keeps no history, so it never
reuses. A frame that reuses runs the composite, because the trace cannot compose a bounce it does
not know yet (`VisibilityConstants::mComposed`). **The reuse does not need the denoiser** (Q4):
it keeps its own history, so `./omw repeat`, which runs with the denoiser off, checks it.

**D13. No upstream file changes.** Everything is in `components/rtx`, `components/rtxvulkan` and
the harness. The game side does not change.

**D14. AMD and Mesa.** Every new kernel uses only what the tree already requires: ray queries in a
ray generation shader, as `spriteshelter.rgen` does, and shared memory in a compute shader. No wave
intrinsic, no SER (declined in `AGENTS.md`), no second queue (declined). Mesa's drm-shim compiles
every new kernel.

---

## 5. Structure

### 5.1 The frame, in record order

```
visibility.rgen + visibilityhit.rchit   the trace, as today; it also writes this frame's
                                        candidate reservoir, the origin record and the
                                        path's transmittance (§5.2)
bouncetemporal.comp                     NEW: merge last frame's reservoir into the candidate,
                                        in place; age, confidence cap, boiling filter
bounceresolve.rgen                      NEW: spatial reuse over this frame's reservoirs,
                                        the final visibility ray, the final shade into
                                        CHANNEL_INDIRECT and CHANNEL_FILL; validation share
accumulate, shadow, specular, pane,     the denoiser, as today
wavelet, composite
```

The temporal pass is compute, because it traces nothing and the boiling filter needs shared
memory. The resolve is a ray generation launch, because it traces, as the fog scatter and the
sprite shelter do. **The reservoirs that become history are the temporal pass's output**
(the paper's Algorithm 4: a spatial result never feeds back). Step 5 measures the feedback that
RTXDI and Kajiya use for faster convergence after a disocclusion, as an option.

### 5.2 Data, per traced pixel

All at the trace's extent, which is the render extent under an upscaler.

| What | Bytes | Kept | Written by | Read by |
|---|---|---|---|---|
| Reservoir: `x_s − x_v` (3 × f32), `n_s` (octahedral, 2 × 16), `L_o` whole (RGB9E5), the fill's share (3 × unorm8) and the age (8 bits), `W` (f32), confidence and flags (32 bits) | 32 | pair: this frame's and last frame's | the trace (candidate), the temporal pass (in place), the resolve (clear on a hidden sample, validation) | temporal, resolve |
| Origin record: distance along the pixel's ray (f32, the arms in its sign, as `CHANNEL_SURFACE`), shading normal and geometric normal (2 × octahedral), transmission and the lobe's `F0` (4 × unorm8) | 16 | pair | the trace | temporal (MIS and the surface test), resolve (`p̂` and `D` at each neighbour) |
| The path's transmittance in front of `x_v` (RGB10A2) | 4 | this frame | the trace | resolve |

That is 100 bytes a traced pixel, beside the about 470 bytes that the G-buffer (170) and the
denoiser's histories (300) keep now: 21% more. The paper needed 475 MB at 1080p.

| Output, upscale | Traced | The reuse adds | Today's per-pixel images |
|---|---|---|---|
| 1680×1050, `balanced` | 988×618 | 58 MiB | about 275 MiB |
| 1920×1080, `quality` | 1280×720 | 88 MiB | about 415 MiB |
| 2560×1440 `native`, or 3840×2160 `quality` | 2560×1440 | 350 MiB | about 1.65 GiB |

*Decided (Q3):* the full traced grid, as essential frame memory, which is never refused. On a full
card the content (textures, structures) comes down first, by the budget rule it already has.
`upscale` is the player's control for memory and cost. A coarser grid loses detail at thin edges
and on normal maps (paper §6.1) and needs an upsample pass.

**Why an origin record of its own, and not `CHANNEL_SURFACE` or the accumulator's surface
history:** at a waterline pixel, both hold the water's distance and a mixed normal, and the reuse
needs the bed's. A sheet's transmission and the lobe's `F0` are in neither. *Decided (Q4):* the
temporal pass runs the tree's one function, `heldSurfaceMatches`, on the origin pair. Reading the
accumulator's history instead would tie the reuse to the denoiser, and `repeat` could then not
check it. **Relation to W8 step 3** (two of each surface channel in the G-buffer): it stays where
it is in `REDESIGN.md`, and if it lands, the origin pair is the candidate to fold into it.

**Validation, if it replays the candidate's own draws** (D10), needs the candidate's 32-bit draw
key in the reservoir. The reservoir is then 36 bytes, padded to 48. Step 6 decides between a
replay and a tolerance test by a measurement.

### 5.3 Where each part lives

The folders keep the order `RtxSourceTreeTest` holds.

**The core, `components/rtx/`:**

- `frame/reconstruction.hpp`: `BounceReuse { Off, Temporal, Spatiotemporal }` in
  `ReconstructionRequest`, its names table beside the others, and the rule in `resolve` (D12).
  `forPicture` resolves it to `Off`.
- `shaders/bouncereuse.h`: what C++ and GLSL both read: the reservoir and origin layouts with their
  `static_assert`ed sizes, the binding numbers, the push constants, and the tuning constants (the
  confidence cap, the age cap, the neighbour count and radius, the similarity thresholds, the
  validation share), each with the measurement that set it, as `look.h` does.

**The backend, `components/rtxvulkan/`:**

- `trace/reuse/bouncereservoirs.{hpp,cpp}`: `BounceReservoirs`, the chain's history: the two
  reservoir buffers, the two origin images, the transmittance image, their set, `resize`, `turn`,
  `reset` and `discard`, on the pattern of `DenoiseHistory`. `TraceChain` owns one, beside
  `mDenoise`. A cut resets it where the denoiser's history is reset.
- `trace/reuse/bouncetemporalpass.{hpp,cpp}`: the temporal pass's pipeline and its record.
- `VisibilityPass`: one more `Kernel`, `BounceResolve`, compiled with the others on the launch
  compile's threads, with `sharedSets`.
- `TraceChain::record`: the two passes between the trace and `DenoisePasses::record`, under zones
  of their own (`bounce temporal`, `bounce resolve`) so `bench` reports them.
- `GBuffer` and the trace's set: the trace binds the reservoir buffer, the origin image and the
  transmittance image of this frame. `CHANNEL_INDIRECT` and `CHANNEL_FILL` keep their meaning:
  under reuse the resolve writes them, and the trace does not.

**The shaders:**

- `lib/bouncereservoir.glsl`: pack and unpack, the reservoir update and merge with confidence,
  `D(ω)`, `p̂`, the Jacobian, the neighbour test, the pairwise MIS weights. The lamp reservoir in
  `lights.glsl` is streaming RIS inside one pixel, with a different sample type. It stays as it is:
  a shared update of three lines is not worth a lamp picture that moves.
- `lib/shading.glsl`: `bounceLight` hands back its sample as well (direction, distance, `n_s`, the
  whole and the fill, the source pdf). `Answer` carries it to the launch. **About four more words on the
  payload**, which `payload.glsl` counts and the pinned layout tests.
- `trace/visibility.rgen`: writes the candidate reservoir, the origin record and the transmittance
  where the reuse runs, and the indirect channels where it does not.
- `trace/bouncetemporal.comp`, `trace/bounceresolve.rgen`: new.
- `lib/random.glsl`: the new `SEED_` values.

**The harness, `apps/rtxtool/`:** `--bounce-reuse=off|temporal|spatiotemporal`, for the A/B
legs. `scene` and `bench` print the two zones.

**No player setting.** *Decided (Q1):* the renderer always reuses where the frame is denoised, and
only the harness has `--bounce-reuse`. The cost is per traced pixel, so `upscale` already sets it.
Look at this again only if Step 7 shows a cost a small card cannot pay.

---

## 6. Steps

Each step ends with `./omw test` and the measurements it names. A step that does not pay what it
says is reverted and recorded in this file, as W8 and the radiance cache are.

### Step 0 — Baseline and the places

- Add a view of a flickering lamp in a room and a view of a lit exterior by day to
  `files/rtx/views.cfg`. The guild has steady lamps, and an exterior has the sky as the source.
  Add a PBR replacer place if one is installed, for the Lambertian bias (§2, item 1).
- `./omw noise --views=balmora-mages-guild,probe-guild-planter,<flicker>,<day>` still, with
  `--strafe=150` and with `--walk=150`. Keep the outputs outside `/tmp`.
- `./omw shot --views=all --map --upscale=off --out=<baseline>`.
- `./omw release bench` at the same places, a warm-up leg first, from the background.
- `./omw kernels > before.txt`.

**Done, 2026-10-03.** The places: `ahemmusa-yurt` (two lanterns that pulse slowly, two candles) and
the suite `[bounce]`: the guild, the planter, the yurt, the pier and the pond at noon. The guild's
own lamps flicker too: 20 of its 30 flicker slowly and one pulses. No PBR replacer is installed, so
no place has a glossy `x_s`. The baseline is in `~/.cache/omw-restir/base/` (release `noise`, debug
`shot --views=all --map --upscale=off`, `kernels`). The bench waits for a quiet card.

`./omw release noise --suite=bounce`, the frame's noise and its bias, in levels of 255:

| Place | Still | `--strafe=150` | `--walk=150` |
|---|---|---|---|
| balmora-mages-guild | 0.79, 1.67 | 1.57, 1.98 | 1.82, 2.42 |
| probe-guild-planter | 0.96, 2.33 | 2.17, 2.29 | 1.79, 3.43 |
| ahemmusa-yurt | 0.76, 1.78 | 2.47, 2.62 | 2.96, 3.12 |
| seyda-neen-pier | 0.50, 1.71 | 1.18, 1.44 | 1.18, 2.07 |
| seyda-neen-pond | 0.47, 1.53 | 1.19, 1.75 | 1.14, 1.63 |

These are higher than §1's figures from the session before: §1 was measured under another
build of the denoiser. This table is the one the steps are held to.

### Step 1 — The plumbing, with the reuse off

- `BounceReuse` in the core, the rule in `resolve`, the harness option, the names table.
- `bouncereuse.h`, `BounceReservoirs`, the temporal pass and the resolve kernel compiled but not
  recorded.
- **Tests (components-tests):** the rule: a picture is `Off`, a request without history is `Off`,
  reuse turns the composite on. The header's layout sizes. **Tests (rtx-gpu-tests):** the chain
  makes, resizes and resets the reservoirs.
- **Gate:** `shot --against <baseline>` moves no picture. `kernels --against` names only the new
  kernels.

### Step 2 — The candidate, and the resolve that only reads it

- `bounceLight` hands back its sample. The trace writes the reservoir with confidence 1 and
  `W = 1 / p(ω)`, the origin record and the transmittance.
- The resolve, with no temporal and no spatial reuse, shades the pixel's own candidate:
  `D(ω) · L_o · W · transmittance`.
- **Test:** with `--bounce-reuse=temporal` and the confidence cap at 1, `CHANNEL_INDIRECT` and
  `CHANNEL_FILL` equal today's to the rounding of the packed radiance, at every pixel of a test
  scene with a wall, a sheet lit from behind, a glossy surface and an escape. This is D3, and the
  test that holds every later step to the same integrand.
- **Gate:** `noise` at Step 0's places: the same noise and bias as the baseline, within the
  verb's own spread.

### Step 3 — Temporal reuse

- Reproject by `CHANNEL_MOTION`. Test the surface against the previous origin record by the
  tree's one surface test (`heldSurfaceMatches`, or W8's if it lands first). Shift the sample by
  the Jacobian (D6 for the offset). Merge with confidences and pairwise MIS (D8). Cap the
  confidence and the age. Fall back to nothing on a disocclusion: `c = 0`, the course's rule.
- Start values: confidence cap 20 (course), age cap 30 (RTXDI). Then a sweep of the cap over
  8, 12, 20 and 30.
- A/B the candidate's source pdf with the reuse on: cosine (today, blue noise) against uniform
  (the paper's and Kajiya's finding). The reuse-off frame keeps the cosine.
- A/B the target: `p̂ = L_o` against `p̂ = L_o · D(ω)`.
- **Tests (rtx-gpu-tests):**
  - **Unbiased in a still scene:** the mean of N frames with temporal reuse equals the mean
    without it, within a bound derived from the variance of the frames (the test states the
    math). The scene: a lit patch on one wall of a closed box, most of whose hemisphere is dark.
  - **Less variance:** the per-pixel variance over the same frames falls by a ratio the test
    states, and the cap is shown to matter: cap 1 → variance A, cap 20 → variance B, `A ≠ B`.
  - **Jacobian:** a hand-computed `|J|` for a set geometry, one for the identity shift, and one
    for a sample at infinity.
  - **Disocclusion:** a pixel whose surface changed keeps only its candidate.
- **Gate:** `noise` still and `--walk=150` at the guild and the planter fall. The bias does not
  rise past the threshold in §7. `repeat --pairs=10` is identical.

### Step 4 — Spatial reuse and the final visibility ray

- The resolve reads a fixed number of neighbours (start: 2, RTXDI's default) in a disc (start:
  32 px), by a low-discrepancy pattern turned per pixel and per frame. Each neighbour passes the
  similarity test (start: RTXDI's 0.6 and 10%) or weighs nought by a select. Shift by the
  Jacobian. Reject a shift whose Jacobian is far from one, by a bound that the measurement sets,
  because a reconnection of a very different length adds variance (course §6.4). Merge with
  pairwise MIS, the pixel's own temporal reservoir as the canonical sample.
- The final visibility ray (D7). A hidden sample is nought, and its temporal reservoir is cleared.
- The boiling filter in the temporal pass (start: strength 0.2).
- The MIS ray (D8) by default, and the basic form as the A/B leg.
- Sweeps: neighbours 1, 2, 4. Radius 16, 32, 48 px. Boiling filter on and off.
- **Tests (rtx-gpu-tests):**
  - **No leak through a sheet:** a wall with no thickness between a lit half and a dark half of
    a room. No pixel of the dark half takes light from a lit-side neighbour's sample.
  - **Unbiased in a still scene**, as in step 3, with the spatial reuse on and the ray inside the
    weights. The basic leg's offset from the plain bounce is measured beside it, and the test
    states which way it leans.
  - **Sky samples:** an exterior pixel under a roof edge takes sky light only where its own
    visibility ray reaches the sky.
- **Gate:** as in step 3, with `--strafe=150` as well, which is where the spatial reuse is
  meant to help.

### Step 5 — The denoiser after the reuse

- **The history length.** With reuse, the accumulator averages a signal that is already a
  temporal average. Sweep `ACCUMULATE_FRAMES` (32 today) down to 16 and 8 under reuse, against
  the noise and the walk's lag.
- **The variance the wavelet stops at.** The moments of a correlated signal understate its
  variance (paper §6.1). Measure the wavelet's luminance stop as it is, and with a spatial
  variance estimate in the first frames of a history (SVGF's own rule for a young history).
- **Spatial feedback.** The option that the spatial result becomes next frame's history (RTXDI,
  Kajiya). Faster after a disocclusion, more correlated. Measured with `--strafe=150`.
- **Gate:** the best of each against `noise` still, strafe and walk, and `shot --against`.

### Step 6 — Validation and the dynamic cases

- The validation share (D10): replay against a tolerance test, a share of one in four, eight
  and sixteen. Measure the lag at the flickering-lamp view: frames until the bounce follows a
  lamp, by `film` over a route and `noise`'s bias there.
- A door that opens and an actor that walks through the guild: `bench` with the moving camera,
  `film` for what only motion shows.
- **Tests (rtx-gpu-tests):** a lamp that turns off. The bounce from it falls to nought within the
  number of frames the share and the age cap state.

### Step 7 — Cost, and the default

- `./omw release bench` at Step 0's places, back to back, from the background: medians, p99 and
  the worst frame, and the two new zones.
- `kernels --against`, `repeat --pairs=10`, `./omw gate`.
- The default for a played session: on, with no setting (Q1).
- `docs/rtx/architecture.md`: §8 *The denoiser* and §10 *A frame* name the two passes. The
  headers hold the rest.

---

## 7. Acceptance

The figures are a proposal, for you to change.

- **Noise:** at the guild and the planter, the still and the strafed noise fall by a third or
  more against Step 0. No place of the `noise` suite gets noisier.
- **Bias:** `noise`'s bias against the converged reference rises by no more than 0.3 levels at
  any place of the suite, and `shot --against` shows no light through a wall.
- **Cost:** the two zones together at most 1.0 ms median at the guild, at the run's default
  extent (1280×720 traced), on the RTX 4090 Laptop. The estimate, from the bench of 2026-10-02
  (the trace 1.34 ms, the accumulator 0.24 ms, about 0.2 ms a ray a pixel), is 0.4–0.8 ms for the
  reuse and about 0.4 ms for the MIS rays (Q2): 0.8–1.2 ms, at the limit. Step 7 says whether
  the limit or the neighbour count moves. The p99 and the worst frame do not move beyond the legs'
  spread.
- **Determinism:** `repeat --pairs=10` identical with the reuse on.
- **AMD:** every new kernel compiles under drm-shim.

---

## 8. Decisions

Asked and answered on 2026-10-03.

| | Question | Decided | Where |
|---|---|---|---|
| Q1 | A player setting? | No. Always on where the frame is denoised. The harness has `--bounce-reuse`. | §5.3 |
| Q2 | A visibility ray inside the MIS weights? | Yes, by default. The basic form is an A/B leg. | D8, step 4 |
| Q3 | The grid? | The full traced grid, as essential memory. | §5.2 |
| Q4 | The surface match against W8? | The reuse's own origin pair, by `heldSurfaceMatches`. W8 step 3 stays as it is. | §5.2, D12 |

---

## 9. Not in this plan

- **The lobe's bounce** (D1). ReSTIR PT's hybrid shift handles a glossy visible point
  (course §6). A later plan, after a PBR place shows a need.
- **More than one bounce.** The paper's multi-bounce tiles (25% of the screen). The second hit
  today ends in `pathEnd`, and the reuse keeps that.
- **Decorrelation by MCMC mutations** (Sawhney et al. 2024), if Step 5 leaves blotches the
  denoiser cannot take.
- **ReSTIR DI for the lamps.** The lamps' noise goes through the shadow denoiser now and is the
  smaller part.

---

## 10. What was built, and where it left the plan

On the branch `restir-gi`. Each point says what changed against §4 and §5, and why.

**The shape** (§5.1, §5.3):
- **No set of its own.** The reservoirs bind in the trace's pushed set (`BIND_BOUNCE_*` in
  `bindings.h`): the push already turns every frame, and a fifth shared set would pass Vulkan's
  guaranteed four.
- **No `trace/reuse/` folder.** The owner is one class, `trace/bouncereservoirs.{hpp,cpp}`, and
  the three passes are kernels of `VisibilityPass`, beside the fog's and the sprites': they read
  what every launch reads (the frame block, the structure, the tables).
- **Three passes, not two**: the validation (a launch, it traces and shades), the temporal merge (a
  dispatch, it traces nothing) and the resolve (a launch).
- **The hit shader writes the candidate**, the shader that shaded the point; the launch writes a
  reservoir of nothing where none did (`Answer::mBounceKept`). The payload gained one flag bit and
  no word.
- **Reservoirs: one working buffer and one history**, not a pair (§5.2). The trace and the temporal
  pass fill this frame's; the resolve writes the history the next frame merges. The origins stay a
  pair.
- **The origin holds an offset from the eye** (28 bytes), not a distance: last frame's point is read
  against this frame's eye through `mCameraMotion`, where a distance would need last frame's ray.
  So a pixel keeps 32 + 32 + 2 × 28 + 4 = 124 bytes, not 100.

**The method** (§4):
- **The history is the pixel's own temporal reservoir**, not the spatial merge's result. Fed back,
  as RTXDI and Kajiya do, the still guild was 0.62 against 0.66 now; but validation could not keep
  up with it: a sample it found dark came back the frame after from a neighbour not yet asked, and
  the bounce of a lamp that went out lasted to the age cap, 30 frames, where it now lasts 8.
- **Last frame's reservoir is the nearest matching tap.** Drawn among the four taps by bilinear
  share, a walk kept more history, but a still eye under the upscaler's jitter took its neighbours'
  reservoirs at random, and the still guild rose from 0.68 to 0.74.
- **The neighbours' disc is 3% of the traced height**, not 32 pixels: at a quarter of the frame
  the spatial half raised the error. **Two neighbours**: four gained 0.01 for four more rays.
  **RTXDI's facing test, 0.6**, not the accumulator's 0.9: 0.01 to 0.02 less noise.
- **Each neighbour's shift traces its own visibility ray**, as the paper's unbiased variant does,
  beside the MIS ray to the canonical sample (D8). A sample taken from a neighbour then needs no
  final ray, and neither does the pixel's own fresh candidate.
- **A shift to the other side of the sample's surface is refused** (`reconnectionJacobian`): a
  sample on a wall of no thickness is the same point as the dark face behind it.
- **The boiling filter lets go of carried samples only**, past 41 times the workgroup's mean
  (RTXDI's strength 0.2). On fresh candidates too, as RTXDI does, the still guild fell to 0.55, but
  where the eye moves most pixels hold a candidate alone, and the yurt's bias rose by 0.9 walked.
- **Validation (D10): one pixel of each 4 × 2 block a frame, each in turn**, in a launch ahead of the
  temporal pass. It traces last frame's kept sample again from the point that kept it and shades it
  as the trace did. A sample the point no longer meets is let go; one whose light fell by more than
  half takes the light it has now and stands for one candidate. **A tolerance and not a replay**:
  the far end's light is one draw of a lamp and one occlusion ray, and a replay would need the
  draw's frame and pixel in the reservoir and the frame threaded through every draw of the shading.
  A fall of 4 was as noisy as 2; with no light rule at all, the yurt was 0.02 less noisy.
- **The temporal MIS traces no ray** (D8 is the spatial merge's): last frame's geometry is gone.
- **A hidden kept sample leaves the history empty** (D7), written by the resolve.
- **Not built:** the uniform-hemisphere and target A/Bs (Step 3), the sweep of the confidence cap,
  and the denoiser's history length and variance (Step 5). Each needs a knob that the frame does
  not have, and the gains measured so far came from the reuse's own structure.

**What the tests hold** (`rtxvulkan/trace/visibility/reuse.cpp`, `rtx/shaders/bouncereuse.cpp`):
- `own` equals the plain bounce to what a reservoir stores, and differs from it bit for bit.
- Over a sunlit corner, every mode averages to the plain bounce within 2%, and one frame's error
  falls with the temporal half and again with the spatial one.
- With the eye walking 2 units a frame, the temporal half keeps the error under two thirds of none.
- No light reaches the dark side of a wall of no thickness; without the side test, 96 pixels.
- A rare bright sample is not carried: 18 pixel-frames past the limit, against 24 the plain bounce
  draws and 37 without the filter.
- A lamp's bounce is gone on the eighth frame after the lamp goes, in both reuses.
- A pixel whose surface went shows its own candidate, bit for bit; without the surface test, 319
  pixels take the old surface's history.
- The Jacobian, by hand: a set geometry, the identity, the far side, edge-on and the limit.

**Where the denoised frame stands** (frame noise and bias, `noise`, levels of 255):

| Place | Still: base → now | Strafed: base → now | Walked: base → now |
|---|---|---|---|
| Guild | 0.79 → 0.65 | 1.57 → 1.37 | 1.82 → 1.50 |
| Planter | 0.96 → 0.70 | 2.17 → 1.83 | 1.79 → 1.48 |
| Yurt | 0.76 → 0.74 | 2.47 → 2.26 | 2.96 → 2.66 |
| Pier | 0.50 → 0.49 | 1.18 → 1.18 | 1.18 → 1.18 |
| Pond | 0.47 → 0.46 | 1.19 → 1.19 | 1.14 → 1.14 |

The bias stays within 0.09 of the base at every place (`~/.cache/omw-restir/final/`). §7's noise target, a third off the still
and the strafed frames at the guild and the planter, is met at the planter's still frame only: the
raw bounce loses three quarters of its error, the denoised frame much less, because a reservoir
keeps a sample for many frames, so the accumulator's 32 frames average fewer independent samples
than before.

**Step 7** (2026-10-04): `repeat --pairs=10` with the spatiotemporal reuse is identical over every
pair; `shot --views=all --map --upscale=off --bounce-reuse=off` against Step 0 moves no picture
past the denoiser's noise; `kernels --against` names the three new kernels and every kernel that
reads `bindings.glsl`, which declares the reuse's buffers; the gate is clean; CI passes on the
branch. The gate found the GPU timer's cap of 24 zones dropping the queue hold's zone once the
reuse added three: the cap is 40, and a source-tree test counts the zones against it. **The cost
bench is not run**: the card was in use for the whole session, and §7's cost limit is not yet
checked.

## Sources

- [Ouyang et al. 2021, *ReSTIR GI: Path Resampling for Real-Time Path Tracing* (NVIDIA)](https://research.nvidia.com/publication/2021-06_restir-gi-path-resampling-real-time-path-tracing) — the method, eq. 9–11, Algorithms 2–4, §5's parameters
- [Wyman et al. 2023, *A Gentle Introduction to ReSTIR: Path Reuse in Real-Time*, course notes](https://intro-to-restir.cwyman.org/presentations/2023ReSTIR_Course_Notes.pdf) — confidence weights and their cap, canonical samples, shift mappings, pairwise MIS
- [NVIDIA RTXDI, *ReSTIR GI* integration guide](https://github.com/NVIDIAGameWorks/RTXDI/blob/main/doc/RestirGI.md) and [RTXDI-Library](https://github.com/NVIDIA-RTX/RTXDI-Library) — the pass structure and the default parameters (`Source/ReSTIRGI.cpp`)
- [Embark Studios, kajiya, *GI overview*](https://github.com/EmbarkStudios/kajiya/blob/main/docs/gi-overview.md) — a shipped ReSTIR GI: caps, neighbours, validation, its known artifacts
- [*Understanding the Math Behind ReSTIR GI*](https://agraphicsguynotes.com/posts/understanding_the_math_behind_restir_gi/) — the Jacobian and the weight update, worked through
- [NVIDIA NRD, README](https://github.com/NVIDIA-RTX/NRD/blob/master/README.md) — ReLAX was made for RTXDI's ReSTIR signals
- [Sawhney et al. 2024, *Decorrelating ReSTIR Samplers via MCMC Mutations*](https://research.nvidia.com/labs/rtr/publication/sawhney2024decorrelating/ReSTIRMCMC.pdf) — the correlation problem, and one answer to it
