# Redesign: the structural causes behind the two reviews, and the plan to remove them

Inputs: `.notes/review-shaders.md` (cited as **S§n**, its section n) and
`.notes/review-upstream-diff.md` (cited as **U › heading**, its `###` heading). Both list about
200 findings. Most of them come from fewer than a dozen causes. Each cause is a missing contract: a
rule that the code states in one place, or in no place, while several places act on it. This file
names each cause, gives the contract that removes it, and gives the order in which to build the
contracts. A finding that has no structural cause is a point fix, and the last part of this file
lists the point fixes.

Nothing here is measured. Each step in the plan names the measurement that accepts it.

**Status.** Phase 0 step 1, the security of the workflows, is in `938d176850`, and its first run
is its test (section 5, Phase 0). D11, each shared header with the C++ that reads it, is done, and
`RtxSourceTreeTest.aSharedHeaderStandsWithTheCodeThatReadsIt` holds it. D12, the instruments, is
done: `noise` keeps its means in sixteen bits and judges in fractions of a level, the still bar is
held to its history, the scene digest names the colours and every texture field, and the driver
reads every file as UTF-8. **Every `noise` figure from before D12 is not comparable with one after
it.** D1, one arithmetic for every module and flavour, is done: every entry point preserves
NaNs, infinities and signed zeros, debug and release compile one listing, and `rayAt` divides
nothing. Its pictures from before are in `~/rtx-baselines/a70ac65b81`, and from
after in `~/rtx-baselines/d1-after`. The registration GPU test lands with D2 in Phase 3, and `noise --upscale=native` standing is
the leg that shows the defect.

## 1. Decisions

Each decision changes what a later phase builds. All six were taken on 2026-10-05, and each one
says what was decided.

1. **Where a history is registered on the screen** (D2). The two reviews disagree.
   - S§2 says: fetch at `at + 0.5 + motion`, with no jitter, as NRD does.
   - U › "The temporal filters never see the previous frame's jitter" says: fetch at
     `at + jitter − jitterPrev + motion`.
   - The merge left this as an open question.

   **Recommendation: S§2 for every accumulated value, and the previous jitter for every
   single-frame geometry read.** An accumulated mean is the mean of many jittered samples, so its
   centre is the pixel centre. If you fetch it at `jitter − jitterPrev`, you resample it at an
   offset whose variance is twice that of `jitter` alone, every frame. That is more blur than the
   code has now. The last frame's surface channel (normal and distance) is one ray's sample, and
   that ray went through `i + 0.5 + jitterPrev`. A plane test that rebuilds that point needs
   `jitterPrev`, as U says. Section 4 (D2) gives the details. Either way, the frame constants must
   carry the previous jitter, and today they do not.

   **Decided: the hybrid**, as recommended.
2. **How a glossy lobe meets a lamp's model** (D3). **Recommendation: the sun's rule.** Every
   analytic source is reached only by its light sample. Geometry that stands for the source has no
   emission for any ray that a `gather` lobe drew. The other option is MIS between the lobe hit and
   the lamp sample. It needs the lamp sample's pdf at the lobe's hit, and a lamp's model is not the
   lamp's sphere, so the two pdfs measure different shapes.

   **Decided: the sun's rule**, keyed by what the parent evaluated (D3.1).
3. **How minor sky sources reach the shadow bit** (D3). **Recommendation: a share floor.** A source
   whose weight is under a stated share of the total is never drawn. Its light is in the exact
   unshadowed sum and takes the drawn source's bit. The bias is limited to the minor source's
   shadow. The other option is to gate the moons on `!sunUp()`. It makes moonlight appear at the
   frame where the sun's irradiance reaches nought, which is a visible step at dusk.

   **Decided: the share floor**, starting at 1/64 and set by `noise --ab` at dusk. **Then decided
   (2026-10-06): a floor of nought**, kept as `--shadow-floor` for a later A/B. At the dawn deck a
   floor of 1/64 left the noise where it was on every leg and raised the bias by 0.07 to 0.12 of a
   level, and 1/256 by 0.03 (`Shaders::SHADOW_DRAW_FLOOR` has the figures).
4. **Whether to rebuild the bounce's reuse or to retire it** (D4). **Recommendation: make the four
   cheap corrections first** (temporal MIS, far-ground flag, the rate coin in validation, and
   allocation only on demand). Then run the reuse A/B again. Build the random-replay validation
   only if the reuse then shows a noise gain. If it does not, remove the reuse and its kernels.
   `.notes/reuse.md` already records that it does not pay outdoors.

   **Decided: the cheap fixes, then a decision** from the A/B (Phase 5).
5. **Fixes in upstream files.** AGENTS.md allows a bug fix in upstream code only with your
   approval. These point fixes touch upstream files: the launcher's Native save, the settings
   migration, the SDL3 cursor fallback, the window centring, the display mode rounding, the
   controller button labels, the Lua cursor scale, the ping-pong canvas resolve, multiview gamma,
   the local map's render targets, and `rtxsupport.cpp`'s physics entry. They are listed in
   section 6. Approve or decline them as a group or one by one.

   **Decided: all four groups approved**: the settings round trip, the SDL3 regressions, the
   rasterizer's presentation, and the local map with the physics entry. Of the two changes of
   behaviour in the SDL3 port: the controller's positional buttons stay and are recorded in
   AGENTS.md's SDL3 entry, and a Lua cursor is scaled by the shown scale alone, as upstream's pixels
   mean once the frame is scaled into the window.
6. **The ripples' step rate** (S§14). Stepping at the frame's `dt` changes the look against
   upstream's fixed 60 Hz. **Recommendation: step at `dt`**, because the AGENTS.md rule on uniform
   frame times applies. State the change of look in the Accepted diff.

   **Decided: every frame at `dt`**, with the change of look recorded in the Accepted diff.

## 2. What the redesign does not do

- It proposes none of the declined techniques: opacity micromaps, async compute, or Shader
  Execution Reordering.
- It keeps "one path through a shader". A step that adds a per-lane branch names the measurement
  that justifies it.
- It does not change upstream code except where section 6 asks for approval.

And two rules apply to every step:

- **A contract lands with the check that holds it.** The reviews found one gather written five
  times, and the copies had drifted apart. A contract that nothing tests drifts the same way.
  The table at the end of section 4 names each check.
- **A step that changes the picture lands with a temporary switch.** `noise --ab=<switch>` then
  compares both sides in one run, on one card state, which a comparison of two commits cannot do.
  The old side is removed in the same phase, once the A/B accepts the new one. No switch outlives
  its phase, and none becomes a setting.

## 3. The causes

| | Cause | Findings it explains |
|---|---|---|
| D1 | The arithmetic contract stops at contraction and order | Done |
| D2 | No one owns temporal reprojection | S§2, S§5, S§9, U › jitter, U › five kernels |
| D3 | Direct light has no single rule for its sources | S§3 (through), S§4 |
| D4 | The bounce's far end is not a pure function of its sample | S§6, U › validation, U › resolve, U › reservoirs kept |
| D5 | Secondary rays see a scene that the eye does not see | S§11 (coverage) |
| D6 | A filtered signal carries material that the filter blurs | S§8, U › pane and glossy gather |
| D7 | Participating media store a product as two means | S§3 (fog, water), S§13, S§15 (fog ray), S§16 (integrate) |
| D8 | Texture facts are computed per use, with the wrong semantics | S§11, U › mip chain, U › painted-light estimate, U › Sprite light bake |
| D9 | Exposure has two owners, and neither adapts in stops | S§10, U › FSR's reset sentinel |
| D10 | Frame state changes and barriers are decided inside a pass | U › Vulkan ordering (3 groups), S§16 (barriers) |
| D11 | The core's shader folder holds the backend's binding model | Done |
| D12 | The instruments cannot see the defects above | Done |

## 4. The contracts

### D2. Temporal history: one library and one registration rule

**Cause.** Five temporal filters (accumulator, shadow, glossy, pane, bounce reuse) each write their
own copy of the same steps:

- where the history is fetched;
- which taps are this surface;
- how the kept taps are renormalised;
- how much history survives a partial footprint;
- whether a fast mean holds the slow one.

The copies have drifted apart:

- the shadow copy lost its "no history" test;
- the glossy and pane copies have no anti-lag;
- only the dual-motion path has a plane test;
- `samePlane` rebuilds the previous ray with this frame's jitter;
- each copy fetches at this frame's jitter offset.

**Contract.**

1. **Registration.** A history texel `i` that holds a mean over frames stands for the pixel centre
   `i + 0.5`. It is fetched at `at + 0.5 + motion`. A texel that holds one frame's geometry (the
   held surface: normal and distance) stands for the ray through `i + 0.5 + jitterPrev`. It is
   rebuilt through the previous camera, with the previous jitter. The held surface is one frame's
   geometry: `accumulatesurface.comp:37` and `accumulate.comp` copy `CHANNEL_SURFACE` into it each
   frame and average nothing. The bounce reuse's history is neither kind: a reservoir carries its
   own origin offset, so its fetch takes the nearest matching tap and needs no registration.
2. **One gather.** `surfacematch.glsl` returns the four matched bilinear shares (`vec4`, nought
   for a refused tap) and the footprint quality, which is their sum before renormalisation. Each
   kernel weights its own payload with them. A tap is refused when it:
   - is outside the screen;
   - is a different surface by the plane rule;
   - holds no history (a per-filter predicate: `a <= 0`, or `SHADOW_NO_RECEIVER`).
3. **One match rule** (done): `samePlane`, ReLAX's plane distance against `ACCUMULATE_PLANE` of
   the frustum's side over `lerp(0.05, 1, NoV)`, on every filter's taps, the occluder's path and
   the bounce reuse.
4. **History length.** One `historyAlpha(frames)` for every running mean (done). Scaling the frames by
   the footprint's quality was measured and declined: `frames * sqrt(quality)` moved no `noise`
   figure by more than 0.01 on any leg, and the darkness `RtxBounceTrailTest` measures behind a
   moving bar rose from 0.74 to 0.94 columns, since a shortened history called the wavelet's
   history fix in from the still-shadowed floor.
5. **One running mean with a fast companion.** The bounce's slow mean is clamped to the
   neighbourhood of its fast means in YCoCg, per channel, by a shared library (done). The glossy and
   pane filters' companions were declined (Phase 3 step 5), and the glossy roughness cap waits on a
   question.
6. **Fed-back precision** (done). A history that is read back into its own blend is never stored in
   a format whose store may round toward nought (`mayRoundTowardNought`); `DenoiseHistory`'s table
   names each image's role and checks it at compile time. The shadow history is `RG32F`.

**Host shape.**

- `HistoryConstants` carries the previous jitter, from the frame's sampling
  (`VisibilityConstants::mPreviousJitter`), which `FsrFrame` reads too (done).
- `DenoiseHistory` declares each history once, with its role, from one table (done).

**What goes away.**

- the four gather loops and the three `sameSurface` copies;
- the hand-spelled `heldSurfaceOf`;
- the per-frame resampling blur;
- the ghosting of reflections and windows (stays: their fast companions were declined, Phase 3 step 5);
- the darkening of shadow history at terminators;
- the loss of far-ground taps at grazing angles;
- the half-float drift of the shadow history.

The wavelet items in S§9 are not part of this contract, but they touch the same files, so the
plan does them in the same phase. They are the geometry-weighted short-history variance, the
history fix's own normal power, the per-pixel offset at stride 8, prefiltered variance on both
sides, and the fixed pixel's fast mean.

### D3. Direct light: one source table and one estimator contract

**Cause.** The sun, the moons, and the lamps follow different rules for questions that each have
one answer:


**Contract (the split output, which the shadow denoiser reads).**

1. **Reach, keyed by what the parent evaluated**, is done: `EVALUATED_*`, which `bounceLanding` and
   `bounceEscape` read; the water's legs keep every source's geometry.
2. **The unshadowed sum is exact, and one bit is drawn by contribution**, is done: the split sky is
   every source's sum, the darkening is taken off the exact lamp sum in both modes, and the floor
   (`--shadow-floor`) stands at nought by decision 3.
3. **The penumbra is the nearest occluder's, measured on the receiver, and a stopped ray's through
   is one**, is done: split rays trace to the nearest solid, the width is stretched by the light's
   cosine at the receiver up to `SHADOW_PENUMBRA_STRETCH`. No floor of one pixel: stretched, a reach
   under a pixel is a hard shadow, which the denoiser's hard path was measured for.
4. **A highlight has the source's size**, is done: `reflectionAt` takes the lobe at the disc's
   representative point and scales it by `(α / α′)²` (Karis 2013, eq. 10 and 14), the sun's at its
   seen disc. Evaluating D at `α′` alone, as this item first said, is the method Karis set aside
   because it makes a glossy surface look rough.
5. **The cost is fixed below the primary hit**, is done: a point that composes its light draws
   `LAMP_CANDIDATES`, eight, uniformly from its cell and resamples them; the eye's split hit walks
   every lamp. The guild, the planter, the yurt and the customs office hold no more than eight, so
   no figure moved there.
6. **The draws are blue at the primary hit.** Done for the split hit's shadow rays: its sun pair,
   lamp pair and two picks come from tile streams (`STREAM_SUN_DISC`), and deeper paths keep the
   hash, which D4's replay needs. **Left:** the bounce pair takes a vec2 or cosine STBN mask in place
   of two scalar channels, whose per-frame turn moves the values and leaves the 64-pixel period on
   screen.
7. **One flag answers one question**, is done: `frame.mNoSkyShadows` opens the leg under the water as
   it opens the one over it. The moons in the water column go with D7.3.

**What goes away.**


### D4. The bounce's far end (removed with the reuse)

Decision 4's cheap fixes were made (`bd990fb38b`), and the A/B after them found no gain at any
place: the reuse, its kernels and its reservoirs are removed, and D4.1 to D4.5 with them.
`.notes/reuse.md` has the figures.

### D5. One ray contract for every ray

**Cause.**

- Alpha-tested foliage thins with distance, because the mips lose coverage. The rasterizer corrects
  this by default.

**Contract.**

1. **Leaving a surface** and **meeting a see-through surface** are done: `leaveSurface` steps off
   the triangle by NVIDIA's bound for a hardware traversal, every ray from a surface starts at
   nought, the peel carries on `CONTINUATION_ULPS` past its layer, and a committing ray meets a
   see-through surface as often as it is there (`MEET_BY_CHANCE`).
2. **Coverage at every level.** The mip chain preserves alpha-test coverage at the material's
   reference (D8). The trace then needs no LOD scale in `candidateStops`.

**What goes away.** The thin far foliage.

### D6. The signal contract: every filtered signal is demodulated by its own albedo (done)

**Cause.**

- The diffuse light and the pane's diffuse light are demodulated.
- The glossy lobe is filtered whole, so the bilinear history fetch blurs a PBR replacer's F0 and
  roughness detail.
- The pane's lobe is divided by the diffuse albedo and kept whole across rotations.

**Contract.**

- Every channel a filter averages holds light per unit of the albedo that the composite multiplies
  back:
  - the diffuse channel by the diffuse albedo;
  - the glossy channel by the split-sum specular albedo (`specularAlbedoOf`);
  - the pane's diffuse half by the pane's albedo.
- The pane's lobe leaves `CHANNEL_PANE`. It is either added to the glossy channel's path, kept by
  `reflectionKept`, or composed unfiltered.
- The G-buffer holds no F0 today, so the specular albedo gets a channel of its own (RGB9E5, 4 bytes
  a pixel), or the composite rebuilds it from a stored F0 and the roughness that
  `CHANNEL_SPECULAR.a` holds. Choose the cheaper one when you build it.

The pane redesign also removes the wasted gather that U › "pane and glossy filters gather history
for pixels whose answer they throw away" reports. The pane's history then holds only diffuse light,
and the gather runs through the D2 library.

### D7. Participating media: store the product, and keep one estimator per stretch

**Cause.**

- The froxels average σ and L separately and multiply the means. The integrand is σ·L, and the two
  are anti-correlated.
- σ itself is stored in half floats, under their normal range.
- The sun's self-shadow charges a local coverage along a slant that leaves the bank.

**Contract.**

1. **The froxel stores what is integrated.** It stores the moons' σ·L, the sun's σ·transport, and
   the dimensionless density `σ / frame.mFogExtinction`. All three are blended and tented linearly.
   `fogThrough` weighs S by `T (1 − e^{−σd}) / σ`, with the limit `T·d` as a select.
2. **A column is charged with the coverage it crosses.** One `slantCoverage(from, direction,
   length)` helper reads the coverage at the slant's mean-value point, with the slant as its
   spacing. `fogThroughLeg` and `fogBeamDepth` both call it.

**Performance in the same phase.** The ambient ray per froxel is gated by `mPuffsInFrame` (done).
The tent from shared memory was tried and declined: the integrate pass's `column` zone measured
0.11 ms with a tile a slice and 0.15 ms with a tile of eight slices, against 0.08–0.09 ms with the
nine reads a column, which the cache already serves. A parallel tent pass before the scan was not
tried: it bounds a gain under the pass's whole 0.09 ms, and adds a write and a read of a
64-slice image.

### D8. Texture facts: computed once at arrival, with the encoding's semantics

**Cause.**

- The device and host reductions treat alpha as coverage for every encoding: normal-height maps and
  `_spec` maps too.
- Both reductions drop odd edges.
- Coverage is not preserved.
- The shading map wraps clamped slots and counts the texels inside holes.
- The emitter's mean alpha is read per frame from the centre of an 8×8 level.
- The sprite light bake is cubic in the texture's side.

**Contract.** A slot's arrival computes its facts once, by its encoding and its wrap, with the
same rule on the host and on the device (`MipChain`, `ShadingMap`):

- **The reduction.**
  - `Colour`: alpha-weighted colour, coverage-preserving alpha against the material's alpha
    reference, or 0.5 where one texture serves several references.
  - `Data` and `Normal`: an even box.
  - Every encoding: the three-tap weighted halving on an odd extent (NVIDIA, *Non-Power-of-Two
    Mipmapping*).
  - `MipChainConstants` carries the encoding.
- **The shading map.** Luminance weighted by alpha for every format with alpha. The blur clamps on
  a clamped axis. `ShadingConstants` carries the wrap.
- **The texture's own facts.** The mean alpha (`MeanTexel::mAlpha`) and the texel size go on the
  row the emitter reads (`GpuEmitter`). `GpuEmitterFrame` keeps only the fog.
- **The sprite light bake.** A running product per row and per column, with `across` computed once
  a texel. That is `O(W·H)`, and the bits are the same as now.
- **Comments.** The comments in `mipchain.comp`, `normalspread.comp` and `ground.glsl` say what the
  code does.

### D9. One exposure

**Cause.**

- The display meters the mean bin, which is a plain log average, so the histogram adds nothing.
- The display adapts linearly in a multiplicative quantity, which reverses the intended asymmetry.
- FSR meters its own exposure, from a meter that is not the display's.
- FSR's reset sentinel is cleared to a value its shader takes as a measurement.

**Contract.**

- The display chain owns exposure, in stops:
  - the histogram reduction drops the lowest and highest percentiles of the lit pixels before the
    mean;
  - adaptation is `held · exp2(log2(target / held) · (1 − exp(−dt / τ)))`;
  - the result goes to a 1×1 buffer.
- FSR reads the previous frame's value from that buffer as its exposure input. Its own luma meter
  then does not set the exposure. The sentinel is still cleared to the SDK's `1e8`, so a reset is
  read as a reset.
- The tone pass dithers with triangular noise from the blue-noise tile before the 8-bit store.
- **`noise`'s reference in sixteen bits.** D12 keeps the means a run averages in sixteen bits,
  but the reference is the renderer's own sum of radiance, written through this pass's 8-bit
  target, so its rounding still stands under every bias. A capture of a summed stop takes a
  16-bit target of its own (`writePng` writes either depth), and the frame shown keeps eight.
- The bloom chain:
  - takes a Karis average on its first halving only (a specialization constant);
  - places taps on corners on odd levels, `uv = (2 · pixel + 1) · texel`;
  - records the histogram with the first halving, and the reduction and the glare between the
    coarse levels.
- The reactive and transparency masks are clamped to 0.9 at the store.

### D10. Frame state changes before recording, and barriers between dependency levels

**Cause.**

- `TraceChain::record` can resize the reservoirs, which submits and waits inside the frame's open
  recording.
- `Use::sUndefined` has a `NONE` first scope, so no discard is ordered after the frame before it.
- Each denoiser pass ends with its own barrier, so three independent passes run one after the
  other.
- `Buffer`'s fill, update and barrier do not name the buffer for the next submit.
- An optional extension is enabled even where its feature was declined.

**Contract.** Points 1, 2, 4 and 5 are done: the indirect light is set before a frame's recording
opens and `CommandPool` asserts no submit beside an open one; a discard's first scope is every
stage; every buffer hand-out names the buffer; and a device option is taken whole, its needs met to a
fixed point (`dropUnmetNeeds`), with quad subgroups and Vulkan 1.4's push constants required. What is
left:

- **A pass returns its writes, and the chain places the barrier.** The shadow, glossy and pane
  passes hand their images back untransitioned. `DenoisePasses::record` puts one `Barriers` batch
  after the three. The display chain does the same for its independent passes (D9). Phase 8.
### What holds each contract

Each check lands with its contract, and each is one the gate runs.

| | The check |
|---|---|
| D2 | `RtxSourceTreeTest`: `historyShare` and `historyTap` appear in `surfacematch.glsl` alone. A GPU test: a still, jittered edge accumulates to its unjittered-centre mean. |
| D3 | GPU tests: a mirror beside a lamp reflects the lamp's analytic lobe and no glow of its model (done); the split sky is every source's sum, and a source under a floor draws no bit (done); a lamp that takes light away takes it off the exact sum where one lamp is drawn (done). |
| D5 | GPU tests: a floor point half a unit from a wall gets no light from behind the wall; a pane of opacity one half is met by half the secondary rays, in the mean. |
| D6 | A host test: the composite's remodulation inverts the trace's demodulation for every channel. |
| D7 | Host tests: the froxel's blend of `σ·L` and `σ` equals the mean of `σ·L`; `waterColumn`'s closed form equals a numerical integral at several directions. |
| D8 | The host/device tests that exist, plus: an odd extent's halving reads every texel of the level above, and preserves its sum. |
| D9 | A host test: adaptation closes a gap at the same rate in stops either way, after the asymmetry the constants state. |
| D10 | `CommandPool`'s open recordings, asserted at each submit (done). Synchronization validation clean on one `shot` run. |

## 5. Implementation plan

Each phase ends with `./omw gate`. Each step builds the targets it touched and runs the covering
test binary with a filter, as AGENTS.md says. Changes to the picture are taken one at a time, so
that each `shot --against` and each `noise` run attributes its movement to one cause.

**Before each phase.** Take the baselines outside `/tmp`:

- `./omw shot --views=all --map --upscale=off --out=~/rtx-baselines/<commit>`
- `./omw kernels > ~/rtx-baselines/<commit>/kernels.txt`
- the `noise` suites (the `--upscale=native` still leg included) and a `release bench`, on a quiet desktop, as
  AGENTS.md describes.

Take new baselines at the end of each phase.

### Phase 0. Foundations that move no picture (the security item)

1. **Security first, independent of the rest. Done in `938d176850`**, unproven until a merge runs:
   - the agent job has no write credential;
   - the result is a bundle that a second job pushes;
   - git is narrowed to the subcommands the job needs, with no `-c`;
   - hooks go to `/dev/null`.

   The release workflow runs CI on the tagged commit, without caches. **Read the first merge
   run for:** no `refused` line in "What the agent reported" (a refused `Write` of
   `UPSTREAM-MERGE.md` means the `Edit(./**)` rule does not cover it); the sandbox started (a
   missing bubblewrap stops the agent at once, by `failIfUnavailable`); a pull request opened by the
   Claude GitHub App, with CI running on it; and auto-merge turned on or the merge held, as before.

### Phase 2. Where light goes (D5, then D3)

Order matters. D5 changes what every secondary ray meets, and D3 is measured on top of it.

1. **D3.6's rest, STBN for the bounce, was tried and declined.** A vector mask, 64 × 64 × 32, made by
   the paper's swaps (Wolfe et al. 2022: `sigma_i` 1.9, `sigma_s` 1, the `|dV|^(2/3)` value term,
   256 sweeps), each slice kept a stratified set, read a slice a frame and moved by an R2 step each
   period. Against the R2-turned tile at `a1506a2e8d`, no `noise` figure of the native still, strafe
   or walk leg moved by more than 0.01, at any place; the bench did not move. Its slices are less
   even over the screen than the tile's pair (mean neighbour distance 0.598 against 0.613, 0.521 for
   white noise), for a little more over time (0.666 against R2's 0.64), and the history fix's test
   left 0.60 of a fresh strip's noise where the tile leaves under half. The work and the mask are in
   `~/.cache/omw-redesign/ab-stbn/`.

### Phase 3. Temporal history (D2, D6, and the wavelet items)

1. **The gather library is done** (`RTX_HISTORY_SHARES`): one rule for the four filters' taps, and the
   shadow's "no history" test with it. **Owed:** a GPU test of the shadow's "no history" test — a tile with shadowed receivers, lit
   receivers and non-receivers on one plane, under a soft penumbra and a jittered history.
2. **The registration rule is done**: means are fetched at `at + 0.5 + motion`, the held surface's plane
   test rebuilds through the previous jitter, and the reuse keeps its tap at the surface's own point.
   **Owed:** the GPU test of a still, jittered edge accumulating to its unjittered-centre mean, which
   needs an edge in the accumulator's own history, isolated from the wavelet.
5. **The YCoCg clamp is done** (`lib/historyclamp.glsl`, the accumulator's). **The fast companion for
   the glossy and pane filters was tried and declined**: a 2-frame fast mean beside each, held by the
   accumulator's clamp in one kernel, took the still eye's filtered means off the mean of their frames
   — the pane test's blue 14% dark, the glossy test's off by up to 0.16% — and a 6-frame one left the
   pane's blue 8.5% dark. The lamp a pixel draws is one of four by luminance, so the blue lamp comes
   rarely and bright: the slow mean holds it, the box of fast means seldom does, and the clamp takes
   it out. Neither filter has a spatial pass before the clamp, which ReLAX's has. The patch is in
   `~/.cache/omw-redesign/ab-meanclamp/`. **The glossy roughness cap is done** as decided in
   `redesign_QUESTIONS.md`: ReBLUR's responsive accumulation under a perceptual roughness of a
   quarter, at least three frames (`SPECULAR_RESPONSIVE_ROUGHNESS`); no place's picture moved.
7. **The wavelet items from S§9 are done**: the history fix's normal power, the widest level's tap
   offset, the anti-firefly ring's specialization constant, the short history's geometry-weighted
   variance, and the fixed pixel's fast mean. Prefiltering each tap's variance was tried and
   declined (`ATROUS_LUMINANCE_SIGMA` has the figures).

### Phase 4. Participating media (D7)

In the order of D7's points. Each step is a `shot --against` at the fog and water places, and the
fog's zones in `bench`. **Point 1 is done**: the froxel stores the density as a share of the
weather's extinction, the light and the sun's transport times it, and `fogThrough` integrates them
by `mediumKept`. The pictures before it are in `~/.cache/omw-redesign/shots-before-d7`. **Point 2 is
done** (`slantCoverage`). **Owed:** a GPU test of a point at a bank's edge, lit as the clear air its
slant leaves through, which needs a way for a test to state a coverage field: the field is
procedural, and a test can only make it even, where the slant's coverage and the point's agree.
**Point 3 is done**, with one change to the contract: the interface factor is computed in the shader
from the source's direction (`waterCrossingOf`), not on the host. A stored factor is a second
statement of the direction, and a writer that sets only the direction leaves it stale.
**Point 4 is done**: a shell and an additive mesh are dimmed by `fogAlong(…).w`, and the sprites'
march keeps the closed form per crossing.
**Point 5 is done**: `fogdepth.rgen` traces the block's middle ray as well, and the lamps' stretch
ends where it stops.

### Phase 5. The bounce's reuse (D4, decision 4)

**Done: removed.** After the cheap fixes the A/B moved no place by more than 0.02 in noise or bias,
and the glow-lit chamber, the one place the reuse was kept for, had 3.15 of bias with it against
1.63 without (`.notes/reuse.md`).

### Phase 6. Texture facts (D8)

**Done**: the odd-extent halving on both sides and in the normal spread
(`rtx/shaders/halving.h`), data's and a normal map's even box, the shading map weighed by alpha in
every format and clamped on a clamped axis, the emitter's mean alpha over the coarsest level, the
bake as four running products, and `delitTexel`'s doc. Two changes to the contract: the mean alpha
is taken in `spriteemitters.rgen` over every texel of the coarsest level (at most 64 reads an
emitter) and not on `GpuEmitter`, because only the device knows which image stands in a slot and
at which level; and `mTexels` stays on `GpuEmitterFrame` for the same reason. A 1024-square bake
with its upload takes 3.3 to 5.3 ms. **The coverage is done too**, as decided in
`redesign_QUESTIONS.md`: the rasterizer's `1 + 0.25 · lod` in `candidateStops`. Only Mournhold's
arrival moved, by its alpha-tested plants; vanilla foliage blends. The dawn deck alone: trace
3.58/3.66 ms against 3.59/3.69.

### Phase 7. Exposure and display (D9)

**Done**: the meter's mean over the lit pixels between their tenth and ninetieth shares
(`EXPOSURE_LOW_SHARE`, Unreal's defaults), the adaptation in stops, FSR's reset sentinel at the
SDK's `1e8`, and the masks held at 0.9 (`UPSCALE_MASK_CEILING`). **Tried and declined: FSR reading
the display's exposure.** Handed last frame's through a texel, its still frames stood further from
the converged reference — the guild 1.20 → 1.58 of bias, the pond 1.51 → 1.71, the pier 1.32 →
1.39, the noise the same — so FSR meters its own (`upscaler.cpp` says so).

The tone pass dithers its eight-bit store, triangular off the blue-noise tile's thirteenth channel
(`STREAM_DITHER`), which a world frame takes and a picture inside the interface does not; the test
harness turns it off (`RenderProfile::mDither`), since its tests read the curve's bytes. A summed
frame's curve runs a second time into a sixteen-bit picture, undithered (`PresentTarget::requireDeep`,
`Renderer::readDeepPixels`), and `noise` writes its reference from it (`Actions::mDeepCapture`); the
bar stays a picture as shown, as the frame it is held against is.

The frame's halving is a Karis average weighed by the exposure the frame before ended on
(`BLOOM_SPEC_KARIS`), and every level stands on its source's corners, the tone pass's read of the
finest one included.

**Declined without a build: the histogram with the first halving, and the reduction and the glare
between the coarse levels.** What the overlap can save is at most the exposure and glare zones, 0.02
and 0.00 ms in the gate's bench and 0.00 and 0.00 at the ship at dawn, under the noise of a
`release bench` median, which Phase 8 keeps a change by; and it would tie the exposure's barriers to
the pyramid's.

**The walk, filmed** (the customs office, a cut to the ship's deck at noon with a turn on it, and a
cut back; `~/.cache/omw-redesign/film-adapt`): each side is exposed as it should be, the room at a
mean of 68 of 255 and the deck at 103. A cut shows no adaptation in a film, which warms up 128
frames after the world stands whole; the turn on the deck moves the mean 103 → 109 → 102 over three
seconds with no overshoot and no step.

### Phase 8. Uniform frame times and unused work

These are the S§14, S§15 and S§16 items, and U › Performance, that no contract above already removed.

**Done**: the ripples step every frame by the time the water's clock moved, in time-corrected Verlet
steps no longer than the springs stand (1.298 sixtieths) and at most four a frame; the change of look
is in AGENTS.md's Accepted diff. A sprite list without room for every run bins the tiles whose runs
fit, a prefix, and only the tiles past it walk every sprite (`SPRITE_TILE_UNBINNED`); no bench
measures it, since the frame it shortens is the rare one a storm's first frames make.
A frozen root that moves no longer costs a sweep: a dropped hold leaves the counts short until the
walk reaches the entry again, and a root whose face changed stays thawed until a later walk finds it
standing still, so a turning door is walked each frame and frozen on none.
The sea is synthesised once per water time (`WavePass::holds`): the pictures of a cell crossing and
the frame share one synthesis.
A visibility gate watches `GameHour` by the hour it stands in, so a script that reads the hour runs
as the hour turns and not on every frame.
**Measured and declined: `NO_DUPLICATE_ANY_HIT` only where needed.** Its upper bound, the bit off
on every geometry, against the bit everywhere, at the dawn deck over eight runs alternated: medians
7.53 against 7.56 ms and the trace zone 3.48 against 3.49, within the runs' own spread; the mages'
guild 5.34–5.37 on both sides (`~/.cache/omw-redesign/ab-nodup`). The bit costs this card nothing it
measures, so the split by material and its rebuild are not built.
**Not measurable yet: the arms' `tmax`.** No bench place draws the arms (a probe of
`mArmsInFrame` read nought at the deck and at the guild: the harness's body readies nothing), so the
arms' ray is never traced in a measured run and no A/B can keep the change. It waits for a place
that stands the player with a weapon drawn; the finding stays in `review-shaders.md`.
**Declined by its bound: the everywhere-presence word.** The atomic ORs it would save are a part of
the sprite bin's whole zone, 0.03 ms at the dawn deck, under a bench median's noise.

**Kept: the wavelet's narrow levels in `RGBA16F`** (`ATROUS_NARROW`). At the guild over six runs a
side, alternated: the frame median 5.46 → 5.36 ms, the p99 7.20 → 7.11, the filter zone 0.774 →
0.752, lower in every run; `noise` at the guild and the dawn deck the same to the hundredth, and the
frame's mean darker by 0.004 and 0.006 of a level, the halves' rounding toward nought
(`~/.cache/omw-redesign/ab-narrow`).
**Measured and declined: the sprite runs' stride rects.** Each stride's union widened by the pass
over sprites and tested by each workgroup before it loads the stride's rectangles. At the night
storm over six runs a side: the sprite zone's median 0.049 → 0.055 ms, the four atomics a sprite
costing more than the strides passed save, and its p99 1.08 → 1.09; the p99 is not the runs' walk
(`.notes/ISSUES.md`; `~/.cache/omw-redesign/ab-strides`).

**Declined by its bound: the sprite shade's batches in shared memory.** The shade zone is 0.011 ms
at the night storm and 0.04 at the overcast deck, under a bench median's noise.

**Measured and declined: one trace site in `visibility.rgen`.** The four sites folded into one loop
over the eye and the layer draw the same pictures at every place, and are slower: the trace zone's
median 3.68 → 3.82 ms at the dawn deck and 1.75 → 1.77 at the guild over six runs a side
(`~/.cache/omw-redesign/ab-site`).

**Stopped here on the user's word** (2026-10-06): Phase 8 is done; Phase 9 and section 6 are not
started.

- **Uniform frame times.**
- **Unused work.**

Each one is a `release bench` A/B, and is kept only if its median or p99 improves. A per-lane
branch is kept only with its measurement written beside it.

### Phase 9. The rest of the tree

The point fixes in section 6, in any order, apart from the ones that wait for decision 5.

## 6. Point fixes outside the contracts

These are local defects. Each one is fixed where it stands.

**Fork code, no approval needed.**

- U › A material is keyed on the nearest state set (both items). This is a correctness bug in the
  walk, and the highest priority in this list.
- U › A reused ESM::Cell carries the last cell's groundcover. A one-line clear before
  `initCell`, with high benefit.
- U › The night sky reader, both items.
- U › The twin fold. Count the differing pairs first.
- U › Hooks that answer for a renderer that is no longer current: `TracedTerrain::enable` and the
  overlay's pending paint.
- U › Crashes the catcher never reports: the Windows WER module, the macOS hang handler, and the
  AppImage mount (verify the mount first).
- U › Session packages accumulate.
- U › Platform abstractions: `shellWord`, and the test-only code in the library.
- U › CI hygiene, and the release workflow's name sanitising.
- U › The gate's verdict, the hashes reader, and the driver's refusals.
- U › A frozen walk no longer counts what the reuse check measures.
- U › Sibling tables and readers, one rule stated twice, the content cache's dead key machinery, the
  walk's frozen recording on a throw, and the lifetime refusal counter.
- U › Smaller items in the shared headers and the pipeline. The push-block item goes with D10.
- U › Duplication, dead code and stale narration, the docs' claims, and the stale harness
  narration. Do these when you edit those files, by the comment rule.
- U › Conventions: includes and file layout, and `noexcept` on the seam overrides.
- S§17: `lightThrough`, the FSR dead code, the stress clock, the headers' stated gates, the
  descriptor reflection table, and the k-buffer composite for sprites. The k-buffer is the largest
  item in this list, so treat it as its own measured step.
- U › Fork hunks that the Accepted diff does not cover. Each one is a decision for the Accepted
  diff, not code.

**Upstream files, waiting for decision 5.**

- U › The frame resolution does not survive a round trip: the launcher's Native save, and the
  migration marker.
- U › SDL3 port regressions: the cursor fallback, the window centring, the mode rounding, the
  button labels, and the Lua cursor scale.
- U › The rasterizer's no-technique resolve, and gamma under multiview.
- U › Rasterizer local-map tiles no longer release their render targets.
- U › The settings declaration names a reason that is not true (`rtxsupport.cpp`, fork code, but
  it decides an upstream setting).
- U › Smaller defects in covered hunks: the `-DOPENMW_RTX` comment, typed RTX settings, and the
  visibility gates.

## 7. Where each finding goes

| Review group | Goes to |
|---|---|
| S§2 | D2 (Phase 3) |
| S§3: sky, through | D3 (Phase 2) |
| S§3: fog, water, layer | D7 (Phase 4) |
| S§4 | D3 (Phase 2), D2 point 6 (Phase 3) |
| S§5 | D2 point 5 (Phase 3) |
| S§6 | D4 (Phase 5) |
| S§8 | D6 (Phase 3) |
| S§9 | Phase 3, step 7 |
| S§10 | D9 (Phase 7) |
| S§11 | D8 (Phase 6), D5 point 2 |
| S§13 | D7 (Phase 4), the moons under water in D7.3 |
| S§14 to S§16 | Phase 8, D7 (integrate, ambient ray), D10 (barriers) |
| S§17 | Section 6 |
| U › Pictures that are quietly wrong | D3, D4, D2, D5, D8, and section 6 (material key, groundcover, night sky, twin fold, hooks, canvas) |
| U › Vulkan ordering and submission | D10 |
| U › Settings and the SDL3 port | Section 6, decision 5 |
| U › Crash reports, CI and the release | Phase 0 step 1 (security), section 6 |
| U › Measurements that can mislead | Section 6 (gate, hashes, driver) |
| U › Performance | D4 point 5, D8 (sprite light), D6 (pane gather), Phase 8 |
| U › Design: data, ownership and dependencies | Section 6 |
| U › Duplication, dead code and stale narration | D2 (the five kernels), section 6 |
| U › Conventions, U › Fork hunks | Section 6 |

When a phase closes a finding, delete the finding from its review file, by the rule each review
file states.
