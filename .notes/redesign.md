# Redesign: the structural causes behind the two reviews, and the plan to remove them

Inputs: `.notes/review-shaders.md` (cited as **S§n**, its section n) and
`.notes/review-upstream-diff.md` (cited as **U › heading**, its `###` heading). Both list about
200 findings. Most of them come from fewer than a dozen causes. Each cause is a missing contract: a
rule that the code states in one place, or in no place, while several places act on it. This file
names each cause, gives the contract that removes it, and gives the order in which to build the
contracts. A finding that has no structural cause is a point fix, and the last part of this file
lists the point fixes.

Nothing here is measured. Each step in the plan names the measurement that accepts it.

**Status.** Phase 0 step 1, the security of the workflows, is in `938d176850`. Its first scheduled
run is its test (section 5, Phase 0). Nothing else is started. A first attempt at D11 was reverted,
and what it found is written into D11 below.

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

   **Decided: the share floor**, starting at 1/64 and set by `noise --ab` at dusk.
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
| D1 | The arithmetic contract stops at contraction and order | S§1, U › Floating point (5 groups) |
| D2 | No one owns temporal reprojection | S§2, S§5, S§9, U › jitter, U › five kernels |
| D3 | Direct light has no single rule for its sources | S§3 (lamp, sky), S§4, S§12, U › sky draw, U › two halves, U › lamps that take light |
| D4 | The bounce's far end is not a pure function of its sample | S§6, U › validation, U › resolve, U › reservoirs kept |
| D5 | Secondary rays see a scene that the eye does not see | S§7, U › See-through surfaces, S§11 (coverage) |
| D6 | A filtered signal carries material that the filter blurs | S§8, U › pane and glossy gather |
| D7 | Participating media store a product as two means | S§3 (fog, water), S§13, S§15 (fog ray), S§16 (integrate) |
| D8 | Texture facts are computed per use, with the wrong semantics | S§11, U › mip chain, U › painted-light estimate, U › Sprite light bake |
| D9 | Exposure has two owners, and neither adapts in stops | S§10, U › FSR's reset sentinel |
| D10 | Frame state changes and barriers are decided inside a pass | U › Vulkan ordering (3 groups), S§16 (barriers) |
| D11 | The core's shader folder holds the backend's binding model | U › core's shader folder, S§6 (`pairingsFor`) |
| D12 | The instruments cannot see the defects above | U › Measurements that can mislead, S§2 (`--upscale=off`) |

## 4. The contracts

### D1. One module, one arithmetic

**Cause.** The pinner fixes fusion and order of operations. It does not fix:

- the float environment: NaN, infinity, and signed zero;
- the division `rayAt` makes;
- the double-precision folding of derived constants;
- the glslc level, which differs by flavour;
- implicit-LOD sampling.

Each of these is a way for two compiles of one expression to disagree. The tree then depends on
the exact behaviour in about a dozen guards, such as the `-inf` water sentinel, `isnan` in the
counts and the 9e5 pack, and the NaN-aware comparisons.

**Contract.** A pinned module computes the same bits in every compile, on every flavour, and
from every module that spells the same expression.

**Shape.**

- **Float controls.** The pinner adds `SignedZeroInfNanPreserve 32` to every entry point.
  `requirements.cpp` requires `shaderSignedZeroInfNanPreserveFloat32`. The pinner refuses every
  float-controls mode and `FPRoundingMode` that the source sets itself, and every implicit-LOD
  image operation. The GUI samples at an explicit level.
- **One build level.** glslc runs at one level, `-O -g`, for every flavour.
- **No division that two modules must agree on.** `Camera` carries `2 / width` and `2 / height`,
  divided on the host. `rayAt` is then multiplies and adds only. Look for other divisions that
  several modules repeat: any `precise` expression with an `OpFDiv` is one.
- **Derived shared constants are literals.** Each is written as its correctly rounded value, with
  the derivation in a comment. `RtxSharedConstantTest` holds each literal to the derivation
  computed in double. This extends the `portable.h` rule.
- **Finiteness by bits, where a count depends on it.** `countNotFinite` and the 9e5 pack's scrub
  test the exponent field (`(floatBitsToUint(x) & 0x7F800000u) == 0x7F800000u`). An integer test
  cannot be folded under any float environment, so the counts that `check` asserts on stay true
  even if the float controls cost too much and are taken back. The NaN-aware comparisons still need
  the float controls.
- **Boundary guards for data from the world.** These become correct only after the float controls,
  so they are part of this contract:
  - the histogram bin uses `!(l >= black)`;
  - `octahedralDirected` refuses a sum that is not finite;
  - the moon tint never divides by a measured luminance.

**What goes away.** The per-flavour difference in arithmetic, the "may fold" risk in every NaN
guard, and three host-against-device constant mismatches.

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

- `HistoryConstants` gains the previous jitter. Its one source is the frame's sampling, the same
  value `FsrFrame` keeps today in `mPreviousJitter` (`fsrframe.cpp:47`). `FsrFrame` then reads it
  and does not keep its own copy.
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

- **Which technique reaches a source?** The sun disc is light-sample only. A lamp is reached by its
  light sample and again by a lobe ray that hits its model.
- **What is "exact" in the split output?** For the lamps it is the unshadowed sum. For the sky it
  is one picked source over its chance, so each pixel's hue follows the pick.
- **When is a bit "drawn"?** A daylight moon at 1e-3 of the sun's weight makes every sunlit bit
  "drawn", so every sun shadow is filtered at the widest reach.
- **Which occluder gives the penumbra?** The first one traversal finds, not the nearest.
- **Which plane is the penumbra measured in?** The light's, not the receiver's.
- **What does a stopped ray's `mThrough` mean?** It depends on the BVH order.
- **Where does the darkening of negative lamps apply?** After a clamp on a one-sample estimate.
- **Is a highlight the source's size?** No. The lobe is evaluated at the source's centre.

**Contract (the split output, which the shadow denoiser reads).**

1. **Reach, keyed by what the parent evaluated.** A source's geometry (sun disc, moon discs, lamp
   body) emits nothing to a ray whose parent shading point evaluated that source analytically,
   because the parent's light sample already holds that light. It emits whole to every other ray.
   The ray's path carries the answer per kind of source: `bounceLanding` and `reflectedSky` read
   it, and the test of `PATH_SEEN` / `PATH_INDIRECT` goes. Today:
   - a diffuse bounce and a glossy lobe leave a surface whose `gather` evaluated every source, so
     each drops every disc and every lamp body;
   - the water's reflection and refraction legs leave a surface with no analytic lobe, so they
     keep the discs (`water.glsl:131` reads `reflectedSky(…, true)`) and the lamp bodies;
   - a shadow ray has no emission to count.

   Keyed by the parent and not by the ray, the rule stays correct when a surface gains or loses an
   analytic lobe. A flat rule ("no lamp body to any secondary ray") would darken the water's
   picture of a lantern.
2. **The unshadowed sum is exact.** Split, `CHANNEL_SHADOWED.rgb` is the sum over every sky source
   with non-zero weight, plus every lamp, as though every ray got through. `lightThroughWater` is
   inside each source's weight. The darkening of negative lamps is subtracted from this exact sum
   and then clamped, in both modes. The unsplit mode then applies the held lamp's visibility ratio.
3. **One bit, drawn by contribution.** The bit is the visibility of one source, drawn in proportion
   to its share of the sum, as now. **Drawn** means that another source carries at least
   `SHADOW_DRAW_FLOOR` of the sum (decision 3). A source under the floor takes the drawn source's
   bit. The floor must stand above a daylight moon's share, about 1e-3 of the sun. Start at 1/64,
   and choose it with `noise --ab` across dusk (`views.cfg` has a dawn place): the lower floor that
   moves neither the noise nor the bias.
4. **The penumbra is the nearest occluder's, measured on the receiver.** Split rays trace without
   `TerminateOnFirstHit` and keep the nearest opaque hit. Every other ray keeps the flag. The width
   is divided by the light's cosine at the receiver, capped, and floored at one pixel where the bit
   is a boundary (SIGMA's rule), so the `reach < 1` path takes only hard shadows.
5. **The through of a stopped ray is one.** A ray whose `mOpen` is nought reports `mThrough = 1`.
   Only an open ray's through is filtered with the bit.
6. **A highlight has the source's size.** `reflectionAt` takes the source's angular radius and
   evaluates D at `α′ = saturate(α + sin θ_s / 3)` with Karis's `(α / α′)²` normalisation. The
   weight that picks a source uses the same widened lobe.
7. **The cost is fixed below the primary hit.** The primary split hit keeps the full walk over the
   cell's lamps, because point 2 needs the exact sum, and the walk is bounded by
   `LAMPS_AT_A_POINT`. At bounce hits, pane layers, water legs and the fog, `weighLamps` draws a
   fixed count M of candidates from the cell's run, with the uniform pdf `1/n`, and resamples them
   (RIS). Where `n ≤ M` this is the walk that runs now.
8. **The draws are blue at the primary hit.** The split hit's sun-disc pair, lamp-disc pair and
   pick come from tile streams. Deeper paths keep the hash: D4's replay needs every draw at a far
   end to come from a sequence keyed by the pixel and the frame, and the tile's per-frame turn is
   not one. The bounce pair takes a vec2 or cosine STBN mask in place of two scalar channels.
9. **One flag answers one question.** `frame.mNoSkyShadows` is tested in `skyPassageThrough`, so it
   covers surfaces, the shaft march and the froxels together. The moons in the water column go
   with D7.3, which rewrites `waterColumn` anyway.

**What goes away.**

- the double-counted lamp highlight;
- the per-pixel hue noise at dusk;
- the sun shadows that are blurred all day while a moon is up;
- the penumbrae from distant roofs;
- the order-dependent through;
- the brightening near negative lamps;
- the pinpoint highlights of lamps and moons;
- the lamp-density cost at secondary hits;
- the inconsistent sky-shadow flag.

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
   a hashed sequence keyed by the pixel and the frame (D3.8 keeps the tile streams to the primary
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
   `pairingsFor` moves to the core (D11).

Decision 4 orders this work: points 2 (the far-ground flag only), 3 and 5, together with the
validation without the rate coin, come first. Point 1 comes only if the reuse then shows a gain.

### D5. One ray contract for every ray

**Cause.**

- The eye's ray peels see-through surfaces by opacity. Shadow rays walk past them by opacity. Every
  other committing ray meets them as solid: reflections, refractions, lobes and the bounce.
- Every secondary ray skips its first world unit (`tmin = SHADOW_BIAS = 1`) where the established
  method offsets the origin.
- Alpha-tested foliage thins with distance, because the mips lose coverage. The rasterizer corrects
  this by default.

**Contract.**

1. **Leaving a surface.** One `leaveSurface(surface, direction)` in `traversal.glsl` offsets the
   origin along the geometric normal, on the side the ray leaves by, by the Wächter–Binder ulp
   rule. Every inline query and every `traceRayEXT` from a surface then uses `tmin = 0`. The peel
   continues at `t · (1 + kε)`. `SHADOW_BIAS` remains only as the shortest light distance worth a
   ray.
2. **Meeting a see-through surface.** A committing traversal meets a non-opaque, unmasked candidate
   with a chance equal to its opacity. The draw is one per ray and triangle, built as `cutAt`
   builds its dither. One traversal, and the expected value is the blend. The eye's peel and the
   shadow's walk-past stay as they are.
3. **Coverage at every level.** The mip chain preserves alpha-test coverage at the material's
   reference (D8). The trace then needs no LOD scale in `candidateStops`.

**What goes away.** The leaks at seams, the lost contact shadows, the skipped near layers, the solid
reflections of invisible actors, and the thin far foliage.

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

**Contract.**

1. **State before recording.** One `applyFrameRequest` step runs after `Reconstruction::resolve`
   and before `CommandPool::begin`. It applies everything that can resize or replace a resource:
   the indirect light, the reuse mode, the extent, and the upscaler. `TraceChain::record` asserts
   that the chain already matches. `CommandPool` counts open recordings and asserts in
   `submitWithDeferred` that only the submitted one is open. `GuiTextures::mBatch` is the one named
   exception.
2. **A discard waits for the frame before it.** `sUndefined` is `{UNDEFINED, ALL_COMMANDS, 0}`. The
   comment and its restatements say why.
3. **A pass returns its writes, and the chain places the barrier.** The shadow, glossy and pane
   passes hand their images back untransitioned. `DenoisePasses::record` puts one `Barriers`
   batch after the three. The display chain does the same for its independent passes (D9).
4. **Everything a submit touches is named for it.** `Buffer::clear`, `updateInline` and
   `describeBarrier` call `nameForNext()`.
5. **The device takes what it says it takes.**
   - Options are queried before any extension is appended.
   - `mNeeds` is checked against the taken options.
   - A subgroup row (quad operations in compute) and the largest push block, a `constexpr`, are
     held in `requirements.cpp`.

### D11. Each fact in its own layer

**Cause.** The core's shader folder holds headers that no core C++ reads. Some state descriptor
sets, bindings, push blocks, specialization ids and the shader binding table, which are Vulkan's.
Their stated reason (the compiler is given one directory) is false: glslc is also given
`components/rtxvulkan/shaders`. Also, the backend holds an algorithm, `pairingsFor`, that the
core's tests cannot reach.

**The rule is who reads a header, not what it names.** The light transport's GLSL already lives in
the backend (`shaders/lib/`), so a header that only the backend's C++ and its GLSL read is the
backend's, whatever it states. A header that core C++ reads stays in the core. The first attempt
moved a list, and the list was wrong in two places:

- `storageformat.h` stays: core headers (`scene.h`, `gbuffer.h`, `wave.h`, `shadingmap.h`) include
  it, and its host half names no API.
- `bouncereuse.h` splits: the pairing (`BOUNCE_RADIUS_*`, `BOUNCE_PAIRING_SIZE_*`, `pairingTexel`,
  `pairedStep`) becomes a core header, `bouncepairing.h`, because `pairingsFor` moves to the core and
  reads it. The reservoir's layout and the reuse's constants move.

**Contract.**

- Every header in `components/rtx/shaders` that no file under `components/rtx` includes moves to
  `components/rtxvulkan/shaders/shared/`: 28 headers today (`accumulate`, `atrous`, `bindings`,
  `bloom`, `bouncereuse`, `composite`, `counts`, `exposure`, `fogvolume`, `fsr`, `glare`, `ground`,
  `gui`, `halfstore`, `line`, `mipchain`, `normalspread`, `pane`, `pinning`, `probe`, `ripple`,
  `sets`, `shadow`, `specular`, `spritebin`, `spritelight`, `spriteshade`, `stress`). Their guards
  are renamed with the path.
- The mixed headers split. `visibility.h` gives `SPEC_*`, `HitRecord`, `HIT_*`, `hitRecordOffset`,
  `hitRecordTable` and `MISS_RECORD_*` to `shared/tracerecords.h`. `scene.h` gives `TEXTURE_BIND_*`
  to `shared/sets.h` and `TABLE_ALIGN_*` to `shared/tables.h`.
- `pairingsFor` moves into `components/rtx/frame/bouncepairing` as a free function of the height.

**How the includes work, which the first attempt found:**

- A moved header cannot include a core header as `"camera.h"`: C++ looks beside the moved file and
  then on the include path, which holds the source root only. It includes
  `<components/rtx/shaders/camera.h>`, and glslc gets `-I` for the source root. Its siblings stay
  quoted.
- GLSL includes a moved header as `"shared/accumulate.h"`, under the existing
  `-I components/rtxvulkan/shaders`, as it includes `"lib/…"` today.
- C++ includes `<components/rtxvulkan/shaders/shared/….h>`, and `RtxSourceTreeTest`'s backend order
  gains `"shaders"` first, as the core's has.
- The core tests that include a moved header move to `apps/components_tests/rtxvulkan/shaders/`:
  `accumulate`, `exposure`, `shadow`, `hitrecords`, the reuse half of `bouncereuse`, and the
  glare line of `sharedconstants`. `frame/bouncepairing.cpp` stays and includes `bouncepairing.h`.
  `openmw_tests/mwrender/ripples.cpp` includes the moved `ripple.h` by its new path.
- `AGENTS.md`'s `#pragma once` exception names both shader folders, and `bindings.h`'s stale reason
  goes.

This step changes no kernel. `./omw kernels --against` must name no moved kernel: the digest strips
the debug information, which is the only thing a moved include changes.

### D12. Instruments that can see these defects

**Cause.** The plan is judged by `noise`, `shot` and `repeat`, but:

- `noise` measures against means that it rounds to whole levels. That error is about the size of
  the differences the A/Bs decide on.
- `shot` and `repeat` run with `--upscale=off`, so no jitter reaches the temporal filters, and the
  D2 registration defect is invisible.
- The scene digest skips vertex colours and texture-row fields.
- The still bar ignores the upscaler's extent.
- Windows reads the logs in the wrong encoding.

**Contract.**

- `noise` keeps exact means: 16-bit PNGs, or the sums themselves. It judges in floating point.
- One suite leg runs still and jittered, with the denoiser and with no upscaler (or FSR native).
  Its bias against the reference shows the history blur.
- A GPU test holds a still, jittered, accumulated edge to its unjittered-centre mean. **It lands
  with the D2 fix, in Phase 3, and not here**: it fails today, and the gate stops at the first
  failure. Write it first in Phase 3, see it fail on the old code, then fix.
- The digest covers the colour stream and every `TextureRow` field, bound whole as
  `forEachMaterialField` binds the material.
- The still bar follows `noiseBarFramesAfter`.
- The driver reads every program output as UTF-8 through one helper.

**Exact means move every `noise` figure a little**, by the rounding they lose. The figures in
`.notes/reuse.md` and every baseline taken before this step are then not comparable with later
ones. Take all baselines again after it.

### What holds each contract

Each check lands with its contract, and each is one the gate runs.

| | The check |
|---|---|
| D1 | A SPIR-V test: every pinned module declares `SignedZeroInfNanPreserve`, and the pinner refuses each float-controls mode and implicit LOD in a source. `./omw kernels` gives one listing in every flavour. |
| D2 | `RtxSourceTreeTest`: `historyShare` and `historyTap` appear in `surfacematch.glsl` alone. A GPU test: a still, jittered edge accumulates to its unjittered-centre mean. |
| D3 | GPU tests: a mirror beside a lamp reflects the lamp's analytic lobe and no glow of its model; under a daylight moon, no sun-lit pixel is marked drawn; the split and unsplit modes agree in the mean. |
| D4 | A GPU test: in a static scene, validation leaves every stored radiance as it was, bit for bit. |
| D5 | GPU tests: a floor point half a unit from a wall gets no light from behind the wall; a pane of opacity one half is met by half the secondary rays, in the mean. |
| D6 | A host test: the composite's remodulation inverts the trace's demodulation for every channel. |
| D7 | Host tests: the froxel's blend of `σ·L` and `σ` equals the mean of `σ·L`; `waterColumn`'s closed form equals a numerical integral at several directions. |
| D8 | The host/device tests that exist, plus: an odd extent's halving reads every texel of the level above, and preserves its sum. |
| D9 | A host test: adaptation closes a gap at the same rate in stops either way, after the asymmetry the constants state. |
| D10 | `CommandPool`'s count of open recordings, asserted at each submit. Synchronization validation clean on one `shot` run. |
| D11 | `RtxSourceTreeTest`: a header in `components/rtx/shaders` that no file under `components/rtx` includes is a failure, and so is a backend header that core includes. |
| D12 | The harness's own tests. |

## 5. Implementation plan

Each phase ends with `./omw gate`. Each step builds the targets it touched and runs the covering
test binary with a filter, as AGENTS.md says. Changes to the picture are taken one at a time, so
that each `shot --against` and each `noise` run attributes its movement to one cause.

**After Phase 0, before Phase 1.** Take the baselines outside `/tmp`, once D12's exact means are in:

- `./omw shot --views=all --map --upscale=off --out=~/rtx-baselines/<commit>`
- `./omw kernels > ~/rtx-baselines/<commit>/kernels.txt`
- the `noise` suites (D12's new leg included) and a `release bench`, on a quiet desktop, as
  AGENTS.md describes.

Take new baselines at the end of each phase.

### Phase 0. Foundations that move no picture (D11, D12, D10, the security item)

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
2. **D11.** Move the headers and split the mixed ones. Verify that `./omw kernels --against` names
   nothing, and that `RtxSourceTreeTest` passes.
3. **D12.** Exact `noise` means, the jittered still leg, digest coverage, the still bar rule, and
   UTF-8 reads. Verify with the harness tests and with one `noise` run per side, to confirm that the
   figures move only by the rounding they lost. The registration GPU test waits for Phase 3.
4. **D10 points 1, 2, 4 and 5.** These are ordering and contract fixes. Verify with
   `./omw test` and with synchronization validation on one `shot` run and one `bench` run.
   `./omw repeat --pairs=10` must agree.

### Phase 1. Arithmetic (D1)

1. Float controls, the pinner's refusals, and explicit LOD in the GUI.
2. One glslc level for every flavour.
3. The `rayAt` reciprocal, and any other division that two modules repeat.
4. Shared constants as literals, with the test.
5. Boundary guards: the histogram, `octahedralDirected` and the moon tint.

**Verify.**

- `./omw kernels --against` names the moved kernels. Expect most of them to move.
- `./omw repeat --pairs=10` agrees.
- `shot --against` shows no picture change beyond the half-ulp class. A larger move means that a
  guard was being folded, so read it.
- `release bench` A/B with the previous commit, back to back, medians and p99. This step costs
  frame time if it costs anything, so state the figure in the commit.
- `./omw kernels` in the debug flavour and in the release flavour give one listing.
- **The GPU tests now run against the modules the game ships**, for the first time: until step 2
  they ran on `-O0` modules, which fuse nothing. A test that fails then has found a real
  difference between the host model and the shipped arithmetic. Read it before changing it.

### Phase 2. Where light goes (D5, then D3)

Order matters. D5 changes what every secondary ray meets, and D3 is measured on top of it.

1. D5.1, `leaveSurface` and `tmin = 0`. Then `shot --against`. Expect moves at seams, contact
   shadows and near layers, and check the bias against the reference in `noise`.
2. D5.2, see-through commits by opacity. Measure with `shot` at a place with glass and an invisible
   or fading actor. Add one to `views.cfg` if none exists.
3. D3.1, the reach rule: lamp bodies, discs, and water legs.
4. D3.2 and D3.3, the exact sky sum and the drawn floor. Measure with `noise --ab` at dusk, with a
   place in daylight while a moon is up and a place under two moons. The shadow filter's reach
   should fall by day.
5. D3.4 and D3.5, the nearest occluder, the receiver's penumbra, and the stopped through. Measure
   the cost of the split rays without `TerminateOnFirstHit` with `bench`, and the pond A/B again.
6. D3.6, the highlight size. Then D3.7, M candidates below the primary hit, with
   `noise --ab=<M>` and `bench` at a lamp-dense interior.
7. D3.8, blue streams for split draws, and STBN for the bounce. `noise --ab`, all three legs.
8. D3.9, the sky-shadow flag. The moons under water go with D7.3 in Phase 4.

### Phase 3. Temporal history (D2, D6, and the wavelet items)

1. The previous jitter in `HistoryConstants`, from one source. `FsrFrame` reads it.
2. **The gather library first, as a pure refactor.** Fold the four loops and the three
   `sameSurface` copies into it, with today's rules unchanged, the shadow's missing "no history"
   test excepted. `shot --against` must show no change outside the shadow's terminators, and
   `repeat` must agree. Every later step of this phase is then one change in one place.
3. **The registration rule, test first.** Write the GPU test of a still, jittered edge, and see it
   fail on step 2's code. Then change the fetch and the `samePlane` rebuild, and see it pass.
   Measure with `noise` on the jittered still leg, and with `--strafe=150 --walk=150`.
4. The history length by quality, and the plane match, each with its own A/B switch.
5. The fed-back precision rule and the history table in `DenoiseHistory`. The shadow history
   becomes `RG32F`.
6. The fast companion and the YCoCg clamp for every running mean, and the glossy roughness cap.
   Measure with `noise --walk` at a place with a mirror floor and a walking actor (add one to
   `views.cfg`), and at a window lit by a lamp. Phase 2's D3.1 has already taken the lamp body out
   of the glossy channel, so the lag measured here is the filter's alone.
7. D6: the specular demodulation, and the pane's lobe out of `CHANNEL_PANE`.
8. The wavelet items from S§9. Then decide the anti-firefly ring (`.notes/todo.txt` item 1):
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
| S§1 | D1 (Phase 1) |
| S§2 | D2 (Phase 3), D12 (Phase 0) |
| S§3: lamp, sky, through | D3 (Phase 2) |
| S§3: fog, water, layer | D7 (Phase 4) |
| S§4 | D3 (Phase 2), D2 point 6 (Phase 3) |
| S§5 | D2 point 5 (Phase 3) |
| S§6 | D4 (Phase 5), D11 (Phase 0) |
| S§7 | D5 (Phase 2) |
| S§8 | D6 (Phase 3) |
| S§9 | Phase 3, step 7 |
| S§10 | D9 (Phase 7) |
| S§11 | D8 (Phase 6), D5 point 3 |
| S§12 | D3 points 6 and 7 (Phase 2), the bounded VNDF in Phase 2 step 6 |
| S§13 | D7 (Phase 4), the moons under water in D7.3 |
| S§14 to S§16 | Phase 8, D7 (integrate, ambient ray), D10 (barriers) |
| S§17 | Section 6 |
| U › Pictures that are quietly wrong | D3, D4, D2, D5, D8, and section 6 (material key, groundcover, night sky, twin fold, hooks, canvas) |
| U › Floating point the build does not control | D1 |
| U › Vulkan ordering and submission | D10 |
| U › Settings and the SDL3 port | Section 6, decision 5 |
| U › Crash reports, CI and the release | Phase 0 step 1 (security), section 6 |
| U › Measurements that can mislead | D12 (Phase 0), section 6 (gate, hashes, driver) |
| U › Performance | D4 point 5, D8 (sprite light), D6 (pane gather), Phase 8 |
| U › Design: data, ownership and dependencies | D11, section 6 |
| U › Duplication, dead code and stale narration | D2 (the five kernels), section 6 |
| U › Conventions, U › Fork hunks | Section 6 |

When a phase closes a finding, delete the finding from its review file, by the rule each review
file states.
