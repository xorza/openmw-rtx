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
3. **One match rule.** Plane distance `|n · (P_tap − P)|` against a threshold scaled by the
   footprint and by `1 / lerp(0.05, 1, NoV)`. The same rule applies on the reprojection path and
   on the occluder's path.
4. **History length.** `frames *= sqrt(quality)`, floored at one, before the blend weight. There
   is one `historyAlpha(frames)` for every running mean.
5. **One running mean with a fast companion.** Every running mean (diffuse, glossy, pane) keeps a
   fast mean beside the slow one. The slow mean is clamped to the neighbourhood of the fast means
   in YCoCg, per channel, by `accumulateclamp.comp`'s rule, moved into a shared library. The glossy
   filter also caps its frames by roughness (ReBLUR's `1 − exp2(−200 r²)`).
6. **Fed-back precision.** A history that is read back into its own blend is never stored in a
   format whose store rounds toward nought. `storageformat.h` states this once, and each history
   format names its role: fed back, or scratch. The shadow history becomes `RG32F`.

**Host shape.**

- `HistoryConstants` carries the previous jitter, from the frame's sampling
  (`VisibilityConstants::mPreviousJitter`), which `FsrFrame` reads too (done).
- `DenoiseHistory` declares each history once, with its role, from one table. The table replaces
  the 20 hand-written `Image` and `ImagePair` members, and the format rule in point 6 is checked
  where the table is built.

**What goes away.**

- the four gather loops and the three `sameSurface` copies;
- the hand-spelled `heldSurfaceOf`;
- the per-frame resampling blur;
- the ghosting of reflections and windows;
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


### D4. The bounce's far end: a pure function of its sample

**Cause.** A reservoir's stored radiance is a stochastic shading of the far end, and every later
stage shades it again under other random numbers or other rules:

- Validation re-shades with fresh seeds and with this frame's `INDIRECT_LIGHT_RATE` coin, which is
  the trace's own coin for this pixel. It keeps the new value only when it falls.
- The sky sample stores the receiver's water attenuation.
- The far-ground escape is assumed without a record.
- The resolve writes back an `own` sample whose visibility it did not test.

The temporal merge also uses the defensive pairwise MIS where GRIS uses the balance heuristic. The
reservoirs are allocated at full extent while the reuse is off.

**Contract.**

1. **Replay.** A sample carries the stamp of the frame that found it and the finder's pixel key
   (`mState` has free high bits for the stamp, and the key needs one word). `randomSeedAt(key,
   frame)` beside `randomSeed(key)` gives the same sequence for that stamp. Every stage that shades
   the far end calls one `shadeFarEnd(hit, replay)`. In a static scene it then returns the stored
   radiance exactly. A change in the result is a real change of light, and validation replaces the
   value in both directions. **What replay needs of the rest:** every draw at the far end comes from
   a hashed sequence keyed by the pixel and the frame (D3.6 keeps the tile streams to the primary
   hit), and the `INDIRECT_LIGHT_RATE` coin is one of those draws. A lamp that flickers changes the
   far end's light from frame to frame, and validation then follows it, which is correct.
2. **The receiver's factors are applied, never stored.** Water attenuation at the receiver, the
   far-ground escape rule, and the receiver's plane are applied in `bounceTarget` and in the
   shading of the shift. The sample stores what the far end sends. `GpuBounceOrigin` carries the
   far-ground state, so `bounceSeen` answers without a ray where the trace's own rule did.
3. **MIS by the merge's shape.** A two-input temporal merge uses the generalized balance heuristic.
   The spatial resolve keeps the defensive pairwise form.
4. **Visibility is a state.** A reservoir written to the history carries "visibility established
   this frame". The resolve writes `own` only where it is established. Otherwise it traces `own`, or
   drops `own` if its age is above nought.
5. **Cost follows the mode.** The chain allocates the full-extent reservoirs when a frame first
   asks for a mode other than `Off`. Until then it holds the one-pixel stand-ins that pictures
   already use. The pairs pass traces only where the target at the partner is above nought.
   `GpuBounceOrigin` is padded to 32 bytes. The temporal kernel reads the best tap once. The
   resolve keeps only confidences, `there` values and bits live between its loops.
   `pairingsFor` is in the core (D11, done).

Decision 4 orders this work: points 2 (the far-ground flag only), 3 and 5, together with the
validation without the rate coin, come first. Point 1 comes only if the reuse then shows a gain.

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

### D6. The signal contract: every filtered signal is demodulated by its own albedo

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
- The water's sky term is the closed form for a vertical ray only, and its sun beam has no
  interface factor.
- A cloud layer is dimmed by the closed form, but the haze in front of it comes from the volume.

**Contract.**

1. **The froxel stores what is integrated.** It stores the moons' σ·L, the sun's σ·transport, and
   the dimensionless density `σ / frame.mFogExtinction`. All three are blended and tented linearly.
   `fogThrough` weighs S by `T (1 − e^{−σd}) / σ`, with the limit `T·d` as a select.
2. **A column is charged with the coverage it crosses.** One `slantCoverage(from, direction,
   length)` helper reads the coverage at the slant's mean-value point, with the slant as its
   spacing. `fogThroughLeg` and `fogBeamDepth` both call it.
3. **One closed form for a lit stretch of water.** `waterColumn` integrates each source,
   whether sky or directional, with the general form `exp(−σkh)(1 − e^{−σgL}) / g`. For the sky,
   `k = 1`. Each directional source enters with a host-computed interface factor
   `(1 − F(θi)) cos θi / cos θt`, computed once a frame. Surfaces under water take `(1 − F)` from the
   same table. **Every sky source, the moons with the sun**: the directional term walks
   `skySourceAt` as a surface's `gather` does, so the water in front of a bed the moon lights is lit
   by the same moon. The shaft march stays the sun's alone.
4. **A stretch has one estimator.** Wherever the volume describes a stretch of air, the composite
   reads its transmittance from `fogAlong(…).w`. The closed form serves only a march inside a
   sprite.
5. **The lamps' stretch uses one ray.** The lamp integral is cut at the surface found on the ray it
   is integrated along.

**Performance in the same phase.** The integrate pass reads its tent from shared memory, or the
tent moves to a parallel pass before a pure scan. The ambient ray per froxel is gated by a
frame-uniform flag for whether puffs are present.

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
| D4 | A GPU test: in a static scene, validation leaves every stored radiance as it was, bit for bit. |
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

1. D3.6's rest, STBN for the bounce: a vec2 or cosine-hemisphere mask generated by the tree's own
   tool (void and cluster over space and time) and checked in as data, read for `STREAM_BOUNCE`.
   `noise`, all three legs, against the commit before. **Waiting on `redesign_QUESTIONS.md`**, "A
   spatiotemporal mask for the bounce".

### Phase 3. Temporal history (D2, D6, and the wavelet items)

1. **The gather library is done** (`RTX_HISTORY_SHARES`): one rule for the four filters' taps, and the
   shadow's "no history" test with it. The occluder's path keeps `samePlane` until step 3's one match
   rule. **Owed:** a GPU test of the shadow's "no history" test — a tile with shadowed receivers, lit
   receivers and non-receivers on one plane, under a soft penumbra and a jittered history.
2. **The registration rule, test first.** Write the GPU test of a still, jittered edge, and see it
   fail on step 2's code. Then change the fetch and the `samePlane` rebuild, and see it pass.
   Measure with `noise` on the jittered still leg, and with `--strafe=150 --walk=150`.
3. The history length by quality, and the plane match, each with its own A/B switch.
4. The fed-back precision rule and the history table in `DenoiseHistory`. The shadow history
   becomes `RG32F`.
5. The fast companion and the YCoCg clamp for every running mean, and the glossy roughness cap.
   Measure with `noise --walk` at a place with a mirror floor and a walking actor (add one to
   `views.cfg`), and at a window lit by a lamp. Phase 2's D3.1 has already taken the lamp body out
   of the glossy channel, so the lag measured here is the filter's alone.
6. D6: the specular demodulation, and the pane's lobe out of `CHANNEL_PANE`.
7. The wavelet items from S§9. Then decide the anti-firefly ring (`.notes/todo.txt` item 1):
   remove it, or give it a specialization constant.

### Phase 4. Participating media (D7)

In the order of D7's points. Each step is a `shot --against` at the fog and water places, and the
fog's zones in `bench`. Point 1 changes the stored formats, so take `./omw kernels` before it.

### Phase 5. The bounce's reuse (D4, decision 4)

1. The rate coin out of validation, the far-ground flag, the balance MIS in the temporal merge,
   allocation on demand, the pairs skip, the 32-byte origin, the single read, the live state, and
   `pairingsFor` in the core.
2. Run `noise --ab=bounce-reuse=temporal,off --suite=bounce` (strafe, walk and still) and the
   outdoor `bench` again. Write the result into `.notes/reuse.md`.
3. If the reuse now gains, do D4.1 (replay) and D4.4 (visibility state), and measure again. If it
   does not gain, remove the reuse, its kernels and its reservoirs, and record why.

### Phase 6. Texture facts (D8)

The host and device rules change together, and the host/device tests hold them to each other. Take
`shot --against` at foliage places, at a place with a single-level `_nh` replacer if the test data
has one, and at smoke and fire. Time the arrival of a large replacer texture before and after the
sprite light bake change.

### Phase 7. Exposure and display (D9)

Exposure first, then FSR's input, then the dither, the bloom and the masks. Use `shot --against` and
`noise --ab` for the masks. Run a walk from an interior into daylight and from daylight into an
interior with `film`, to see the adaptation in stops.

### Phase 8. Uniform frame times and unused work

These are the S§14, S§15 and S§16 items, and U › Performance, that no contract above already removed.

- **Uniform frame times.**
  - the ripples at `dt` (decision 6);
  - the per-tile sprite overflow;
  - the frozen-root sweep;
  - the sea synthesised once per water time;
  - the visibility gates' per-frame rerun.
- **Unused work.**
  - the arms' `tmax`;
  - the everywhere-presence word;
  - narrow wavelet levels in `RGBA16F`;
  - the sprite runs' stride rects;
  - the sprite shade's batches in shared memory;
  - the single trace site in `visibility.rgen`;
  - `NO_DUPLICATE_ANY_HIT` only where needed.

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
