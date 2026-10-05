# Review: shaders, denoiser, GI and shader plumbing

Whoever addresses an item deletes it. A group with no items left is deleted with it.

Scope: every shader in `components/rtxvulkan/shaders` and `components/rtx/shaders`, the denoiser
(`components/rtxvulkan/trace/denoise`), the bounce's reuse (ReSTIR GI), the media, the display
chain, the upscaler's integration, and the plumbing that builds, pins and binds the shaders. Each
algorithm was compared term by term with its published source: NRD's ReLAX, ReBLUR and SIGMA
shaders, AMD's FidelityFX shadow denoiser and FSR 3.1.4, the GRIS paper, the 2023 ReSTIR course,
RTXDI, Frostbite's volumetrics, Jimenez 2014, and the Vulkan and SPIR-V specifications. The
references are given per item. Nothing here was measured. An item that changes the picture or the
frame time is proved with `noise --ab`, `shot --against` or `bench` before it is kept.

## 2. The temporal filters fetch and match their history differently from the practice they cite

Every temporal pass (accumulator, shadow, glossy, pane, bounce reuse) goes through
`lib/surfacematch.glsl`. The defects below are therefore in every filter at once, and the copies
of the gather make each one harder to fix.

- [ ] `lib/surfacematch.glsl:34-45` (`historyFootprint`). Callers: `accumulate.comp:267, 292`,
  `pane.comp:75`, `specular.comp:128`, `shadowtiles.comp:301`, `bouncetemporal.comp:55`. The fetch
  is at `at + 0.5 + jitter + motion`. The motion is unjittered at both ends (`reproject.glsl:119`),
  so with a still eye and world, the history is fetched bilinearly at this frame's jitter offset. It
  is then stored back at `at`. Each frame resamples the history at a new fractional offset. This
  blurs every temporal mean, and the content follows the jitter's partial sums. The comment's
  reason ("a still image that shakes") is true of the motion vector handed to FSR. It is not true of
  a history that is stored at pixel centres. NRD fetches at `pixelUv + mv`, with no jitter, and its
  matrices are "non jittered!" (`RELAX_TemporalAccumulation.cs.hlsl:413-416`, `NRDSettings.h:92-126`).
  `shot` and `repeat` run with `--upscale=off`, so neither sees this. Target:
  `before = vec2(at) + 0.5 + moved.xy`, and remove the `jitter` parameter from the six callers.
- [ ] `accumulate.comp:159-179, 321-333`, `specular.comp:138-151`, `pane.comp:80-94`. Valid taps are
  renormalised, so a pixel whose only matching tap has 0.05 of the footprint takes that tap's whole
  32-frame mean. NRD scales the history length by the footprint's quality:
  `historyLength *= sqrt(footprintQuality)`, floored at 1 (`RELAX_TemporalAccumulation.cs.hlsl:228,
  575-582`). Target: the same rule, in the shared gather (last item of this group).
- [ ] `surfacematch.glsl:76-84`, `ACCUMULATE_DEPTH = 0.02` (`look.h:1263`). The temporal depth test
  compares point distances with no slope term. On a floor at a grazing angle, one pixel step changes
  the distance by `pixelAngle / tan α`: about 1.9% at 3° and 5.7% at 1° at 1080p. Far ground near the
  horizon therefore loses taps every frame. NRD compares plane distances and widens the threshold by
  `1 / lerp(0.05, 1, NoV)` (`RELAX_TemporalAccumulation.cs.hlsl:114-127`). The accumulator already
  has a plane test, but only on the dual-motion path (`samePlane`, `accumulate.comp:138-152`).
  Target: one plane-distance rule in `surfacematch.glsl` for both paths, scaled by the footprint or
  by NoV.
- [ ] `shadowtiles.comp:324-340`. The shadow history fetch reads `.x` from texels of pixels that
  received nothing (value 0, variance `SHADOW_NO_RECEIVER`, written at `shadowfilter.comp:179` and
  `shadowtiles.comp:260`) and never tests `.y`. Taps across a terminator pull the history toward
  shadow, bounded only by the ±0.5σ clamp, which is loose in a penumbra. The siblings skip such
  taps: `specular.comp:149-152`, `pane.comp:87-90` and `shadowfilter.comp:200`. Target: skip taps
  whose variance is `SHADOW_NO_RECEIVER`, through the shared helper.
- [ ] `accumulate.comp:109-115, 159-179`, `specular.comp:101-107, 138-151`,
  `shadowtiles.comp:170-176, 324-340`, `pane.comp:77-94`. `sameSurface` is written out two times
  and inlined a third time, and the four-tap matched bilinear loop is written four times. The copy in
  the shadow pass lost the "no history" test (item above). `pane.comp:107` writes
  `vec4(normal, scaled)` by hand where `heldSurfaceOf` says the same. `shadowtiles.comp:305-306`
  tests `sameSurface` again at `nearest`, which is one of the four taps the loop tests.
  `accumulate.comp:330-345` repeats `blendedMean`'s `1 / min(frames + 1, ACCUMULATE_FRAMES)`.
  Target: one `gatherSurfaceHistory(footprint, held, history, holdsHistory)` in `surfacematch.glsl`
  that returns the weighted sum, the weight (the footprint quality) and the nearest tap's match, and
  one `historyAlpha(frames)` that `blendedMean` and the accumulator both use.

## 3. Estimators whose expected value is not the integral they claim

Each item adds or loses light in the converged picture. A denoiser cannot remove this.

- [ ] `lib/shading.glsl` (`gather`, the split sky and lamps), against the contract at `gbuffer.h`
  ("`rgb` is exact per pixel"), read raw at `composite.comp`. The sky's and the lamps' sums are exact
  now, but each is multiplied by one ray's `mThrough` — the picked source's, the held lamp's — so
  what a translucent surface let through is a per-ray value in the unfiltered rgb. Target: filter
  `mThrough` with the bit, as SIGMA does with translucency (`SIGMA_FrontEnd_PackTranslucency`), or
  remove "exact" from the contract.
- [ ] `fogscatter.rgen:263-291`, `fogintegrate.comp:99-126`, `lib/froxel.glsl:221-228`. The history
  and the 3×3 tent average the extinction σ and the in-scattered light L separately, and
  `fogThrough` multiplies the means. The integrand is σ·L, and the two are anti-correlated: the sun's
  transport is `exp(-σ·H/μ)` (`fog.glsl:306-309`), and the moons' is `exp(-beam(σ))`
  (`fog.glsl:322-329`). A froxel on a bank's edge, half at 2.1σ̄ and half at 0.4σ̄, at a 30° sun, has
  mean(σ)·mean(L) about 1.4 times the mean of σ·L. Frostbite stores σs·L and σt per froxel so that a
  linear filter of both stays unbiased (Hillaire 2015). Target: store σ·L for the moons and
  σ·transport for the sun, with σ beside them, and blend and tent those. `fogThrough` weighs S by
  `T(1 - e^{-σd}) / σ`, with the limit `T·d` as a select.
- [ ] `lib/fog.glsl:200-217` (`fogBeamDepth`), `:306-309`, `:322-329`. The sun's self-shadow
  multiplies the point's local coverage along the whole slant column. The slant is
  `H · lift / μ ≥ 2600` units even at the zenith, against `FOG_GRAIN = 1800` a bank, so the line
  always leaves the bank. Clear weather at a 30° sun: inside a bank the code gives
  `exp(-1.6) = 0.20` where the mean coverage gives 0.46, so banks are lit about 2.3 times too dark,
  and gaps too bright. Target: take the coverage the slant crosses, either the mean (coverage 1), or
  one `fogCoverageAt` read at the slant's mean-value point with the slant as its spacing, as
  `fogThroughLeg` (`fog.glsl:771-774`) already does.
- [ ] `lib/underwater.glsl:221-222`, the comment at `:156-162`. The sky half of `waterColumn` is
  `(1 - T²) / 2`, which is the integral for a straight-down ray only. With the arrival depth
  `h0 - t·d.z` at `t`, the integral is `(1 - exp(-σ(1 - d.z)L)) / (1 - d.z)` times the scatter. A
  horizontal view settles at `1 - T`, two times the code's value. A view refracted from above at
  `d.z ≈ -0.66` settles at 0.60, not 0.5. Looking up, the code is darker still. The sun half eight
  lines below already has the general form with `g = 1 - k·d.z`. Target: the sun's closed form with
  `k = 1` for the sky, with its `|g| < 1e-3` select.
- [ ] `lib/underwater.glsl:234-245`. `sunward = frame.mSun.mIrradiance * HG(…)` uses the irradiance
  normal to the beam in air. Under a flat surface, the horizontal irradiance is kept times `(1 - F)`,
  so the irradiance normal to the refracted beam is `E⊥ (1 - F(θi)) cos θi / cos θt`. At a 10°
  elevation the factor is 0.17, so the beam is about six times too bright. At 30° it is 0.62. At noon
  it is 0.98. Surfaces under water (`shading.glsl:201-206`) take the cosine in air, which is correct
  for flux, but no `(1 - F)`. The water and the bed under it therefore disagree at every sun but a
  high one (Mobley, Ocean Optics Web Book, the Fresnel equations). Target: one per-frame host factor
  `(1 - F(θi)) cos θi / cos θt` in the underwater sun irradiance, and `(1 - F)` in
  `lightThroughWater`.
- [ ] `lib/medium.glsl:305-315` (`lit.mReaching = fogThroughLeg(…)`), `spritecomposite.rgen:184-187`.
  The composite is `airBefore + layer.mColour · opacity + …`. `airBefore` is the volume's in-scatter
  to `mCoveredAt`, but `mColour` is dimmed by `fogThroughLeg`, one coverage read at the midpoint.
  Inside a bank the two estimators of one stretch of air disagree. The `fogAlong(…)` call at `:184`
  already computes the volume's transmittance `.w` and discards it. The comment at
  `medium.glsl:283-286` says the geometry is charged by the closed form, which is no longer true.
  Target: for mediums, `fogAlong(…).w` for the reaching term.

## 4. The shadow denoiser's inputs and storage

- [ ] `lib/traversal.glsl:622-628`, the comment at `:578-582`, read at `shading.glsl:218, 283, 289-294`.
  `passageToward` traces with `gl_RayFlagsTerminateOnFirstHitEXT` and reports the committed `t` as
  `mOccluder`. That is the first opaque hit in BVH order, not the nearest. `skyPenumbra` and
  `lampPenumbra` turn it into the penumbra, which sets the shadow filter's reach. A hand two units
  over a table, with a roof 500 units farther on the same sun ray, can return the roof and blur away
  the contact shadow. The comment calls a wider reach safe. NRD's README says that
  `ACCEPT_FIRST_HIT_AND_END_SEARCH` "can't be used … because it can lead to wrong potentially very
  long hit distances from random distant occluders". Target: a literal `nearest` parameter on
  `passageToward`. The split rays, the only ones whose `mOccluder` is read, trace without the flag
  and keep the nearest opaque hit. Every other caller keeps the flag. Measure the cost.
- [ ] `components/rtx/shaders/shadow.h:18-22, 45` (`SHADOW_REPROJECTED STORAGE_RG16F`),
  `shadowtiles.comp:87, 356`, `shadowfilter.comp:78`, `denoisehistory.cpp:61-63`. The moments are
  full floats because "a half store rounds toward nought on this card … so a mean kept in halves falls
  a little at every store". The history is stored in halves, and it is a mean fed back each frame
  through `mix(clamped, current, 0.05)`. At α = 0.05 the loss settles about 10 ulp under the mean,
  about 0.5% in [0.5, 1). In a penumbra, where the clamp is loose, the history stalls up to 1% short
  of 1. That is more than the 0.13–0.2% for which the specular history went to full floats
  (`specular.h`). Target: the history (mean, variance) in `RG32F`. The scratch and visibility images,
  which are not fed back, stay in halves.
- [ ] `shading.glsl:291-294` (`lit.mPenumbra / max(surface.mFootprint, 1e-6)`), `lights.glsl:661-678`,
  `shadowtiles.comp:356` (`reach < 1.0 ? current`). The penumbra is measured normal to the light. On
  a receiver lit at incidence θL it is stretched by `1 / cos θL` along the light's azimuth: three to
  six times at Morrowind's long dawn and dusk. A soft grazing penumbra can read under one pixel and
  take the hard-shadow path, which gives raw bits to the upscaler. SIGMA keeps "at least a 1-pixel
  radius to avoid the hard-shadow early out" (`SIGMA_Blur.cs.hlsl:84-88`). Target: divide the
  penumbra by the light's cosine at the receiver (`picked.mCosine`, the held lamp's cosine), capped.
  Then measure again the pond A/B that justified `reach < 1`.
- [ ] `shading.glsl:142-154` (`sunDraw`, `skyPick`, `lampDraw`, `shadowedPick` from
  `randomSeed(key + lamps)`), `lights.glsl:529`, `random.glsl:255-274`, `scene.h:146-173`. The rays
  whose one bit the shadow denoiser filters aim with hashed white noise, and so does the lamp
  reservoir's pick. Only the bounce, the fog and the water read the blue-noise tile. NRD: "Using
  'blue' noise helps to minimize shadow shimmering and flickering". Heitz & Belcour 2019 show
  screen-space blue error for this one-sample direct-light estimate. Target: tile streams for the
  sun disc pair and the lamp disc pair at the primary hit's `gather(split = true)`. Deeper paths keep
  the hash. Prove it with `noise --ab`.
- [ ] Related, `random.glsl:339-348`, `scene.h:183-185`. The bounce pair is two scalar blue-noise
  channels through the polar (Malley) map, which keeps neither the 2D stratification nor the spatial
  spectrum as well as a vector mask. STBN (Wolfe et al. 2022) publishes vec2 and cosine-hemisphere
  masks. The comment says the per-frame Cranley-Patterson turn makes the 64-pixel repeat "a different
  arrangement each time". The turn shifts the values, not the positions, so the period stays on
  screen. Target: a vec2 or cosine STBN mask for the bounce, and a corrected comment.

## 5. The glossy and pane filters keep 32 frames with no anti-lag

- [ ] `specular.comp:139-179`, `pane.comp:77-104` (`blendedMean(…, 1.0)`), `lib/runningmean.glsl:8-9`
  ("No outlier clamp, for the accumulator's reason"). The accumulator's reason now comes with a
  clamp: `accumulateclamp.comp:6-21` holds the slow mean to a fast one, because without it there is
  "a trail ten pixels long". The glossy and pane filters copy the history length without the clamp.
  With a still eye, `reflectionKept` is 1, so a near-mirror floor that reflects a walking NPC ghosts
  for up to 32 frames, and so does a flickering or carried lamp's highlight (lamp specular is not
  split). A pane's lamp light lags on a still window while the wall behind it follows the light frame
  by frame. ReLAX keeps a fast specular history and clamps to it
  (`RELAX_TemporalAccumulation.cs.hlsl:863-866`, `RELAX_HistoryClamping.cs.hlsl:39-154`). ReBLUR caps
  a near-mirror's frames by roughness: `1 - exp2(-200 r²)` frames' share (`NRD.hlsli:588-595`).
  Target: a fast mean beside each running mean, and the slow mean clamped to its neighbourhood by
  `accumulateclamp.comp`'s rule, shared. For the glossy filter, also a cap on frames by roughness.
- [ ] `accumulateclamp.comp:117, 145-169, 200-205`, `accumulate.h:93-103`. The clamp's box is
  luminance only, but the comment calls it ReLAX's "as published". ReLAX builds the box per channel
  in YCoCg and clamps the slow history in YCoCg (`RELAX_HistoryClamping.cs.hlsl`). A change of hue at
  constant luminance is never followed: a red torch replaced by a blue spell, or the sky's tint at
  dusk. Target: YCoCg fast means in shared memory (three floats instead of one) and a per-channel
  clamp. Alternatively, say what was measured to justify luminance alone.
- [ ] Known gap, `specular.comp:17-21`. The glossy filter has no virtual-motion history. A sharp lobe
  resets at every turn of the head, and only the upscaler denoises it. ReLAX blends a virtual-motion
  reprojection from the hit distance with the surface motion (`RELAX_TemporalAccumulation.cs.hlsl:753-934`).
  It needs the lobe ray's hit distance stored. `CHANNEL_SPECULAR.a` holds the roughness. Decide
  whether the gain is worth a channel.

## 6. ReSTIR GI: the reuse departs from GRIS where the gain is

The reuse is off by default because it lost its noise gain (`.notes/reuse.md`). The first three
items are departures from the published method that each cost gain or add bias. Measure each with
`noise --ab=bounce-reuse=temporal,off --suite=bounce` before the reuse is judged again.

- [ ] `trace/bouncetemporal.comp:115-118`, `lib/bouncereservoir.glsl:262-280`. The temporal merge has
  two inputs and weighs them with the defensive pairwise MIS (course Algorithm 7). The defensive form
  gives the canonical sample an extra `c_c / Σc`. With equal targets, `c_c = 1` and `c_h = 20`, it
  gives `m_c = 1/21 + 20/21 · 1/21 ≈ 0.093`, where the generalized balance heuristic gives
  `1/21 ≈ 0.048`. As an exponential mean, the history then holds about 20 samples, not about 41: the
  cap of 20 acts as about 10. GRIS §8 uses the defensive pairwise MIS for spatial reuse and the
  generalized Talbot (balance) MIS, eq. 36, for temporal reuse. RTXDI's GI temporal pass normalizes
  by `p̂_c M_c + p̂_prev M_prev` (`Rtxdi/GI/TemporalResampling.hlsli`). Target: for the two-input
  merge, `m_c = c_c p̂_c(x) / (c_c p̂_c(x) + c_h p̂←h(x))` and the mirror for `m_h`, from the four
  values already computed. Keep the defensive form in the spatial resolve.
- [ ] `trace/bouncevalidate.rgen:80, 84-90`, the rule at `bouncereuse.h:186-192`. The stored
  `mRadiance` is one stochastic estimate of the far end (one lamp draw, one occlusion ray, the
  ambient ray). Validation shades it again with fresh seeds and replaces it only when the new value
  is under half the old one. It keeps `W` and sets the confidence to 1. Resampling keeps samples in
  proportion to `p̂ ∝ L`, so the kept L is biased high against a fresh draw, and a fresh value falls
  under half often in a static scene (a far end between two lamps, one occluded, reads 2L or 0). A
  rule that can only lower the value turns noise into a darkening bias. The next frame's merge then
  takes the confidence-1, near-zero history as "candidates that found no light"
  (`bouncetemporal.comp:~90`), and gives the fresh candidate only about 0.5–0.75 of its weight.
  ReSTIR PT keeps the path's random numbers so that a second shading is deterministic (GRIS §7).
  RTXDI's GI has no radiance validation. Target: shade the far end again with the random numbers it
  was found with. Store the finding frame's stamp in the free high bits of `mState` and the finder's
  pixel key, and seed `bounceLanding` from them. A static scene then gives L exactly, and only a real
  change of light replaces it.
- [ ] `lib/shading.glsl:854` (the `BOUNCE_REACH` far-ground escape), against
  `bouncevalidate.rgen:63-70`, `lib/bouncepairs.glsl:38-45` (`bounceSeen`), `bouncepairs.rgen:49`
  and `bounceresolve.rgen:133`. Ground farther than 8192 units is assumed to escape and traces no ray,
  and `look.h` names this as the bias that makes it worth having. The reservoir does not record that
  the escape was assumed (`BounceOrigin` has no flag). The reuse then traces the longest rays in the
  frame on these pixels: validation's full `trace` to `mReach`, two `mReach` rays in the pairs pass,
  and one in the resolve for every sample older than 0. It also gives far ground a visibility that
  the picture without the reuse never has. This agrees with the outdoor cost (about 1.1 ms) and the
  added outdoor bias by day (+0.06 to 0.10) in `.notes/reuse.md`. Target: carry the receiver's
  far-ground state in `GpuBounceOrigin` (a spare bit in `mSheet`). `bounceSeen` and validation's
  `met` for sky samples answer "seen" with no ray wherever the receiver meets the trace's own
  `BOUNCE_REACH` rule.
- [ ] `trace/bounceresolve.rgen:133, 142`. When a neighbour is chosen, `own` goes back to the
  history with its visibility never tested. The comment at `:134-137` promises "a sample behind a
  door that closed is let go of at once", which holds only when `own` was the sample shown. The next
  temporal merge, which tests no visibility, gives the hidden sample its full weight again. Target:
  decide the history's fate on `own`'s visibility, traced only where `own` is older than 0 and was
  not shown. Alternatively, drop an untested `own` that is older than 0.
- [ ] `lib/shading.glsl:744-745` (`skySample(drawn.mTowards, sky * daylight)`). The water column's
  attenuation at the receiver (`daylightReaching(position)`) is stored in the sky sample's radiance.
  A shift to another receiver (temporal, or a partner across a shoreline that `bounceAlike` accepts)
  keeps the finder's attenuation. The attenuation belongs to the receiver's integrand. Target: store
  the sky's radiance unattenuated, and apply `daylightReaching(frame.mOrigin + origin.mOffset)` in
  `bounceTarget` and in the resolve's shading.
- [ ] `trace/bouncepairs.rgen:42-50`. A ray is traced for every link whose own sample `holdsBounce`,
  whatever its target at the partner. Both readers multiply the bit by that target
  (`bounceresolve.rgen:~92, 111-112`), so the ray changes nothing where the target is 0: escaped
  samples in rooms, samples behind the partner's plane or under its horizon, zero-radiance far ends.
  Target: compute `bounceTarget(near, own.mSample, reachOf(…))` in `bouncepairs.rgen` and trace only
  where it is above 0. The arithmetic is pinned, so the readers see the same zero.
- [ ] `trace/bouncetemporal.comp:60-77`. The 32-byte `bounceHistory[at]` is loaded and unpacked in the
  tap loop each time a better tap matches, up to four times. Target: keep the best tap's index and
  origin in the loop, and read the reservoir once after it.
- [ ] `trace/bouncetemporal.comp:88`. `min(before.mConfidence, BOUNCE_CONFIDENCE_CAP)` is dead: every
  writer of `bounceHistory` stores a capped confidence (`finishMerge`, a candidate of 1, or
  validation's 1 or 0). Target: remove it.
- [ ] `components/rtxvulkan/shaders/shared/bouncereuse.h` (`GpuBounceOrigin`). The record is 32 bytes
  now, the point's rounding filling it, so none crosses a sector; 24, by folding the reflectance and
  the three flags into one word and the rounding with them, is not measured. The reads are
  scattered: four temporal taps, two partners in the pairs pass, two in the resolve. Target: measure
  32 against 24 with the reuse on.
- [ ] `trace/bounceresolve.rgen:65-67, 74-123`. The first loop keeps two whole reservoirs and two
  whole origins live (about 65 floats) until the second loop, which needs only the confidences, the
  `there` values and the partners' bits. Target: keep those, and read each partner again in the
  second loop from the cache.

## 8. The specular channels are not demodulated

- [ ] `lib/shading.glsl:1002` (`seen.mSpecular = lit.mSpecular + bounced.mSpecular`), `lib/compose.glsl:23`,
  `lib/gloss.glsl:93-95` (`gloss.mAlbedo`, the split-sum albedo, weighs only the bounce's draw). The
  glossy filter averages the lobe's light whole, so the F0 and roughness detail of a PBR replacer's
  maps is blurred into the 32-frame mean by the bilinear fetch. The diffuse half and the pane are
  demodulated. NRD's input contract: radiance "should not include material information (use
  material de-modulation)" (`NRD.hlsli:37`), and for specular that is the pre-integrated albedo
  (Karis 2013, here `specularAlbedoOf`). Target: write `lobe / max(gloss.mAlbedo, ε)` to
  `CHANNEL_SPECULAR` and multiply back in `composedLight`.
- [ ] `visibility.rgen:125` (`stack.mDrawn += reaching * (layer.mBounced + layer.mSpecular)`),
  `:466-471` (divided by `paneAlbedoSum`), `pane.comp:96`, the comment at `pane.comp:22-23`. A glossy
  layer's highlight turns with the view, but the pane history keeps it whole across any rotation, so
  highlights on PBR glass smear when the eye turns. The lobe is also divided by the diffuse albedo,
  against `shading.glsl:659-661`'s own rule. Target: the layer's lobe on its own path, kept by
  `reflectionKept` and demodulated by the specular albedo, or left out of `CHANNEL_PANE`.

## 9. The wavelet's details differ from ReLAX and SVGF

- [ ] `accumulateclamp.comp:154-172, 221`. `momentMean`, the short-history variance, is a flat 5×5
  box over every surface pixel. Short histories stand at disocclusions, so the box mixes the
  occluder's moments with the uncovered surface's. That raises the variance and loosens the
  brightness test where it matters most. ReLAX weights the spatial estimate's taps by normal and
  material (`RELAX_AtrousSmem.cs.hlsl`). SVGF uses a bilateral filter by depth and normal (Schied
  2017, §4.2). Target: weight each tap by `facingWeight` and `coplanarWeight` (the surface is already
  loaded for `gFast`) and normalize by the weight sum.
- [ ] `atrous.comp:230, 237-241` with `surfacematch.glsl:114-117`. The history fix uses the wavelet's
  normal power, 128, on taps 7 to 14 pixels away. On the shading normal (`gbuffer.h:94`), 10° gives
  0.14 and 15° gives 0.012, so on a normal-mapped or curved surface the fix finds almost no
  neighbour. NRD's history fix uses a power of 8 (`RELAX_HistoryFix.cs.hlsl:20-23`,
  `NRDSettings.h:398`). Target: `ACCUMULATE_FIX_NORMAL_POWER = 8`, selected by `fixing`.
- [ ] `atrous.comp:203, 235-236`. The wide level weighs a prefiltered centre variance
  (`varianceAround`, SVGF's 3×3) against each tap's raw variance (`light.a`). The prefilter exists
  because "an edge stopped by a noisy spread is a blotch", and the tap's raw value is the noisy
  estimate. Target: prefilter both, or say why the tap's raw variance is acceptable.
- [ ] `atrous.comp:214-217`, step 8 at the last level (`atrouspass.cpp:360`). A fixed 3×3 lattice at
  stride 8 is the classic à-trous grid artefact. ReLAX offsets the taps by a per-pixel hash where the
  step is over 4 (`RELAX_Atrous.cs.hlsl:137-141`). Target: a deterministic hash of the pixel and the
  frame, so `repeat` holds.
- [ ] `atrous.comp:192-194, 237-239`. In ReLAX the history fix is a pass before the clamp, and every
  à-trous level then filters the fixed pixel (`Relax_Diffuse.hpp`). Here the fix replaces the first
  level's B3 filter, so a fixed pixel is filtered by one level fewer than its neighbours. ReLAX's clamp
  also copies the fast history into the slow one for histories under the fix's frames; here the fast
  mean of a fixed pixel stays the raw sample, and the next frame's box around it is as wide as the
  noise. Target: keep the fused pass, which saves bandwidth, and either weight the B3 by the fix
  kernel or say why one level fewer is acceptable.
- [ ] `look.h:1396-1401`, `docs/rtx/architecture.md:303`, `atrous.comp:46-49`, `atrous.h:23`. The
  docs call the cascade "ReLAX's shape: a 5×5 first level, then three 3×3". ReLAX's first level
  (`RELAX_AtrousSmem`) is a 3×3 Gaussian; its 5×5 is only the spatial-variance branch, and it runs
  5 levels by default (`NRDSettings.h:430`). `look.h:1401` says "the accumulator averages sixteen
  frames" and `atrous.h:23` "up to sixteen"; `ACCUMULATE_FRAMES` is 32 (`look.h:1142`). Target: "SVGF's
  B3 first level, then ReLAX's 3×3 levels", and the figures for 32 frames.

## 10. The display chain does not adapt and meter as its comments say

- [ ] `display/exposure.comp:98-104` (`exposure = mix(held, target, 1 - exp(-dt / tau))`), `look.h:123, 126`.
  The exposure is a multiplier, and a linear mix of a multiplier closes a gap in stops at a rate that
  depends on the direction. For a change of 100 times: into daylight (100 → 1, τ = 0.5 s, the "quick"
  side), the picture is still 5.2 stops over at 0.5 s and 2.6 at 1.5 s, where a mix of the logarithms
  gives 2.4 and 0.3. Into a cave (1 → 100, τ = 1.5 s, the "slow" side) it is only 1.8 stops under at
  0.5 s. The dark adaptation that is meant to be slow converges faster than the light adaptation that
  is meant to be quick. UE and HDRP adapt in stops (EV). Target:
  `exposure = held * exp2(log2(target / held) * (1 - exp(-dt / tau)))`.
- [ ] `components/rtx/shaders/exposure.h:37-41`, `display/exposure.comp:39-75`. The header says the
  histogram "keeps them apart and lets the reduction decide what to expose for", but the reduction is
  the mean bin, which is exactly the log average of the lit pixels. A few flames in a dark room move
  it as they move a log average. Histogram metering clips percentiles before it averages (UE's Low
  and High Percent, 10% and 90%). Target: a prefix sum over the 256 bins that drops the low and high
  percentiles of the lit population, in the same one workgroup. Alternatively, a plain log-sum
  reduction and a corrected comment.
- [ ] `display/tone.comp:172` into `STORAGE_RGBA8` (`tone.h:38`). Nothing dithers before the 8-bit
  store. Sky gradients and dark rooms (exposure up to 200, then a gamma lift) band. `tone.h:131-135`
  already worries about the gamma lift's banding. Practice is ±1 LSB triangular noise in display
  values before the store (Gjøl, "Banding in Games", GDC 2016). Target: triangular noise from the
  renderer's blue-noise tile after `displayGamma`, so `repeat` stays exact.
- [ ] `lib/bloom.glsl:26-50` (`bloomHalved`), `bloompass.cpp:108-114`. The first halving has no Karis
  average. Jimenez 2014, which the file cites, weights each of the five boxes by `1 / (1 + luma)` on
  the first halving only, so that one bright sample does not bloom into a flickering disc. With no
  threshold, a path-traced firefly becomes a pulsing disc up to 64 pixels wide. Target: a
  specialization constant on `bloomdown.comp` for the frame-to-level-0 dispatch only.
- [ ] `bloompass.cpp:44-58` (`width /= 2`), `bloomdown.comp:35` (`uv = (pixel + 0.5) / levelSize`),
  `bloom.glsl:12-14` ("every tap sits on a texel corner"). At 1080p the chain is
  540 → 270 → 135 → 67 → 33 → 16. Under an odd source, a tap lands up to half a texel off a corner,
  and the 13-tap weights are no longer the designed ones, so the veil shimmers as a highlight moves.
  Target: `uv = (2.0 * vec2(pixel) + 1.0) * mTexel` in source texels, and optionally `(n + 1) / 2`
  levels.
- [ ] `upscale/upscaler.cpp:159, 187, 196, 210` (`*_BIND_EXPOSURE` → FSR's own luma meter),
  `fsrcallbacks.glsl:521-534`. FSR rectifies, locks and measures instability on
  `colour * Exposure()` from a meter that is not the display's (no partial adaptation, no hour bias,
  no 0.005–200 clamp). The upscaler judges contrast against an exposure that is not the one shown,
  most at night. The FSR 3.1.4 guide's exposure input is "the exposure value computed for the current
  frame". Target: bind the display's exposure from the previous frame, one truth for exposure.
- [ ] `visibility.rgen:520-531` (`paneMisMoved`, `waterMisMoved`), `reproject.glsl:213-216`. The
  reactive and transparency masks reach 1. The FSR 3.1.4 guide: "it is unlikely that a reactive value
  of close to 1 will ever produce good results … we recommend clamping the maximum reactive value to
  around 0.9". Target: `min(x, 0.9)` at the store. Prove it with `noise --ab`.

## 11. Texture preparation reads a texture differently from the rest of the tree

- [ ] `trace/spriteemitters.rgen:40-47`, read at `sprites.glsl:697-698`, the contract at
  `spriteshade.h:22-26`. The "mean alpha" of a layer is `textureLod(tex, vec2(0.5), coarsest).a`. That
  is the mean only where the coarsest level is 1×1. Morrowind's chains end at 8×8
  (`components/rtx/image/mipchain.hpp:33-37`), where the tap reads the four centre texels: about the
  peak of a radial puff, not its mean. `mLayerThrough = log2(1 - centreAlpha)` is then several times
  too negative, and vanilla smoke and fire shadow themselves far too darkly. The same content also
  shades differently by how its file was packed. Target: the mean alpha is a fact of the texture, so
  take it once at load: `MeanTexel::mAlpha` (`texels.hpp:65-67`, `ImageFactCache::meanOf`) on
  `GpuEmitter`, or a sum in a pass that reads every texel on arrival. `GpuEmitterFrame` then keeps
  only the fog. `mTexels` is a texture constant too.
- [ ] `texture/mipchain.comp:87` (`painted / 4.0`), `lib/traversal.glsl:~389` (`candidateStops` tests
  the cone level's alpha against `cutAt`). A box mean of alpha lowers alpha-test coverage at every
  coarser level, so foliage thins and disappears with distance (Castaño, "Computing Alpha Mipmaps").
  The upstream rasterizer corrects this by default (`settings-default.cfg:527`,
  `files/shaders/lib/material/alpha.glsl:21-33`), so the trace's far foliage is thinner than the
  rasterizer's. Target: coverage-preserving alpha at load, a per-level scale against the material's
  reference, or the same LOD scale in `candidateStops` for masked materials that are not soft-edged.
- [ ] `texture/mipchain.comp:66-77`, `texture/normalspread.comp:56-64`, extents from
  `device/memory/image.hpp:173`. The comment says the last texel "serves twice along an odd edge".
  With floor halving, `2x + 1 ≤ w - 1` for every odd `w > 1`, so the `min(…, last)` never applies, and
  the last row and column are dropped (for `w = 5`, texel 4 is never read). Each level also shifts by
  up to `1/w` toward the far edge. Target: the three-tap weighted halving on odd extents (NVIDIA,
  "Non-Power-of-Two Mipmap Creation"), and corrected comments in both shaders and the host `MipChain`.
- [ ] `texture/shadingmap.comp:92-100`, `texture/texture.cpp:787-791`, `shadingmap.h:36-41`. The blur
  always wraps ("because Morrowind's textures tile"). Slots are keyed per file and wrap, and a clamped
  slot is a UV-unwrapped image that does not tile: its border cells take the opposite edge's light, and
  the delighting divides real albedo by it. Target: the slot's `TextureWrap` in `ShadingConstants`,
  and a clamp on clamped axes, in the shader and in the host `ShadingMap`.
- [ ] `texture/shadingsum.comp:67-70`, `shadingmap.h:78-81`. Only BC1 excludes holes. Under BC2, BC3
  and RGBA cutouts, the colour under alpha 0 is usually black, but it enters the cell mean whole.
  Cells around leaf holes read dark, and `delitTexel` brightens the leaves up to its ×2 ceiling.
  `mipchain.comp:63-69` rejects the even mean for this reason. Target: weight the luminance by alpha
  for every format with alpha. BC1's test is the special case.
- [ ] `lib/ground.glsl:58-62` (`delitTexel`'s doc). A 32×32 grid and three box passes give a blur of
  about 11 texels on a 256² texture. A low-pass ratio is the Retinex smoothness assumption: it removes
  low-frequency light only, and flattens low-frequency albedo. The comment promises to remove
  "occlusion in the corners, a highlight along a rim", which are a few texels wide and pass through.
  Target: the comment says what the estimate removes and where it fails.
- [ ] `lib/sprites.glsl:381` (`rate = 0.5 * max(texels.x / width, texels.y * inverseAxis) / sprite.mRadius`).
  A streak turns about its own axis only, so the footprint along the axis grows by `1 / sin θ`, with
  `sin θ = swing / |axis|`. Rain seen from above or below is read too sharp along its length, which is
  the shimmer the mip chain was built to remove (ray cones: Akenine-Möller et al., JCGT 2021). Target:
  `texels.y * inverseAxis / max(swing * inverseAxis, ε)`. `swing` is known at `:348`.

## 12. Light sampling for glossy surfaces and for many lamps

- [ ] `lights.glsl:446-458` (`surfaceCandidate` → `reflectionAt(gloss, side, towards)` at the centre),
  `gloss.glsl:123-135`, `shading.glsl:87-94, 208-210`. Visibility is sampled across the source's
  cone, but the GGX lobe is evaluated at the source's centre. A lamp 4 to 16 units wide at 100 units
  subtends 2° to 9°, and a lobe under roughness 0.3 is narrower than that. A smooth surface shows a
  highlight the size of the lobe, with the disc's energy in a few pixels, which reads as sparkle under
  bloom. On Masser the ratio is about 100. Karis 2013, eq. 10 and 14: `α′ = saturate(α + r / (3d))`
  with the normalisation `(α / α′)²`. Target: `reflectionAt` takes the source's sine
  (`lamp.mSourceRadius / distance`, `SkySource::mLimb` for the visible disc), and evaluates D at `α′`.
  `atan(mLimb)` per sky source is computed once a frame.
- [ ] `lights.glsl:563-600` (`weighLamps`), `LAMPS_AT_A_POINT = 256` (`:134`). Weighted reservoir
  sampling walks every lamp in the cell, at every shading point (primary, bounce, each pane layer,
  both water legs), and on a glossy surface each candidate pays a full GGX evaluation. The cost grows
  with lamp density, against the rule that frame times are uniform. Practice draws a fixed count of
  candidates from a power-proportional distribution and resamples them (RIS: Bitterli et al. 2020,
  RTXDI), or uses a light tree (Conty & Kulla 2018). Target: a per-cell power alias table built on the
  host once a frame, and M candidates drawn from it with `W = Σ(p̂/p) / (M p̂)`, with a small M at
  `PATH_INDIRECT` and in panes. Measure first.
- [ ] `gloss.glsl:163-169`, `shading.glsl:928`. The spherical-cap VNDF sampler (Dupuy & Benyoub 2023,
  correct) draws reflections under the horizon, which are given weight 0. At grazing views on rough
  lobes that wastes samples. Eto & Tokuyoshi 2023, "Bounded VNDF Sampling for Smith-GGX Reflections",
  shrinks the cap so those are never drawn, for about one `sqrt`. Target: the bounded cap in
  `visibleNormal`, with the weight from its pdf.

## 13. Media and water: smaller defects

- [ ] `components/rtx/shaders/fogvolume.h:22`, `trace/fogvolume.cpp:46-49`, `fogscatter.rgen:264,
  284-285, 291`. σ in world units is about 1.5e-4 in clear weather and goes down to 1e-6 under the
  height falloff. Half floats are subnormal under 6.1e-5, with steps of 2^-24 (about 2% at 3e-6). The
  0.9 history rounds back to halves each frame and cannot come nearer to its target than about 5 ulp,
  a large relative error there. Target: store the dimensionless density `σ / frame.mFogExtinction`
  (about 0 to 2.8) and multiply back on read.
- [ ] `fogscatter.rgen:121-135, 190-191, 294`. The lamps are integrated along the middle ray
  (`fogColumnRayAt(column, 0.5)`), but over `[behind, clear]`, where `clear` comes from
  `fogColumnDepth` on the jittered ray (`fogdepth.rgen:40`). In a column across a silhouette, `clear`
  changes between the surface and `ahead` from frame to frame, and the result has no history. The
  lamp light flickers at silhouettes. Target: cut the lamp stretch at a surface found on the ray the
  lamps are integrated along, or give the lamps the jittered ray and a history.
- [ ] `lib/underwater.glsl:226-227` (`if (!sunUp()) return WaterColumn(transmittance, sky)`). At night
  `shading.glsl:184-206` lights a submerged bed by Masser, but the water in front of it gets no moon
  beam. The air has the moons (`fog.glsl:219-366`). Target: the column's directional term takes the
  source from `skySourceAt`, as surfaces do, with the moon's closed form and no shaft march.
- [ ] `lib/starfield.glsl:52-53`, `components/rtx/environment/nightsky.cpp:297`.
  `uv.x = (atan(y, x) - mTurn) / mTile`, with `mTile` an arbitrary real. Unless `2π / mTile` is a whole
  number, `u` jumps by a fraction of a tile at azimuth ±π, which cuts or doubles stars on that
  meridian. Target: the host rounds `mTile` to `2π / round(2π / mTile)`, or confirms the mesh's unwrap
  is a whole number of tiles.
- [ ] `lib/sea.glsl:364-371`. The comment says the caustics fade "as the inverse square root of the
  depth", but `WATER_CAUSTIC_FADE` is 1.0, and `look.h` says 1 was chosen for the look. Target: the
  comment states the exponent the constant holds.

## 14. Work behind a threshold, against uniform frame times

- [ ] `trace/ripplepass.cpp:119-206` (`tickOf`, the return at `tick <= mSteppedTick`). The ripples step
  at a fixed 60 Hz, as upstream. At 120 fps every second frame pays a 1024² step, a 1024² compose and
  two mip chains, and the frames between pay nothing. Under 60 fps the wake slows. Target: step every
  frame at the frame's `dt`, with the coupling scaled by `(60 dt)²` (stable while
  `a (60 dt)² ≤ 0.5`, the CFL limit of the five-point Laplacian), with substeps only past that. State
  the change of look against upstream.
- [ ] `scene/spritestarts.comp:79-88`, `components/rtx/frame/spritelistsize.hpp:27-30`,
  `trace/spritebin.cpp:60-62` (the report lags two frames). A storm that more than doubles in two
  frames, or entering rain, gives a frame where every pixel walks every sprite: the worst frame in the
  run. The runs are laid out in prefix order, so the tiles whose end fits are binned correctly in that
  frame. Target: tiles past the capacity get a sentinel start that means "not binned", and only those
  tiles walk every sprite.

## 15. Work a frame pays for and does not use

- [ ] `accumulateclamp.comp:102, 111-119, 178-189, 195-196`, `accumulate.h:54-62`. The anti-firefly
  ring is off by default, but it is always computed and multiplied by `mAntiFirefly = 0`: the 16×16
  tile (12×12 would do), the extra barrier stage and the 81-tap loop. `look.h` measured the cost
  (median 0.17 → 0.22 ms, p95 0.58 → 0.69 at the guild, "the switch on or off alike"). Target: an
  `ACCUMULATE_ANTI_FIREFLY` specialization constant and two pipelines, as `ATROUS_WIDE` does. Off,
  the pass loads the 12×12 tile and has no ring loop. If `.notes/todo.txt` item 1 removes the ring,
  remove it whole.
- [ ] `fogscatter.rgen:237-244, 279, 288`. `ambientReaching` is one ray a froxel (2.07M at 1080p and
  64 slices). The file says the air does not read it; only `puffLight` does. The comment against a
  puff flag (`:227-229`) argues about the sun and moon rays, not this one. Target: a frame-uniform
  flag (sprites or mediums present), with the history dropped on the first frame it turns on. Measure.
- [ ] `visibility.rgen:173-177, 206-207, 259-260`. With `mArmsInFrame`, every pixel traces
  `MASK_FIRST_PERSON` with `tmax = frame.mFar`. An instance mask culls instances, not the top-level
  nodes along the ray (software traversal on RDNA), so a miss costs a full-length traversal, not "a
  mask a handful of instances carry". Target: `tmax` at the farthest first-person instance's bound,
  computed once a frame on the host, or a small top-level structure for the arms.
- [ ] `scene/spriterects.comp:263-280`. A `PRESENCE_EVERYWHERE` row (the cloud shell) does an atomic
  OR into every tile every frame, about 1000 a row at 4K, to say one global fact. Target: one
  frame-wide presence word that `presenceAt` and `puffsCoverNothing` OR with the tile's.
- [ ] `atrous.h:20-28` (`ATROUS_CHANNEL STORAGE_RGBA32F`), `atrouspass.cpp:319-371`,
  `denoisehistory.cpp:331, 336-337`. The levels after the first read 9 taps × 32 bytes and write 32
  bytes a pixel in full floats. `atrous.h` says those levels "are shown and never summed, and would
  keep halves on their own". The fill's alpha is never read. Target: a ping-pong pair in `RGBA16F` for
  the narrow levels. Level 1 reads the 32F history and writes 16F. Measure with `bench`.

## 16. Memory traffic and latency on the frame path

- [ ] `fogintegrate.comp:92-127, 160-202`. One thread a column runs 64 slices × 9 neighbours × 2
  fetches, about 1150 fetches a thread, from 32k threads at 1080p, which a 4090 cannot hide. Adjacent
  threads fetch eight in nine of the same texels. Target: each slice's 10×10 tile in shared memory
  once a workgroup, or the tent as a parallel pass before a pure scan. Measure the "column" zone.
- [ ] `scene/spriteruns.comp:94-131`, `spritebin.h:50-58`. Every tile walks every sprite rect, with a
  barrier each 32. Each 8-tile workgroup reads the whole rect table again: about 32 MB of L2 traffic
  at 5k sprites at a balanced 1440p. Target, minimal: `spriterects` also writes the union rect of each
  32-sprite stride, and a tile tests it first and skips the stride (sprites of one emitter are
  consecutive). Full: an order-keeping scatter (Laine & Karras 2011, or a stable radix sort of tile
  and index keys as in Kerbl et al. 2023).
- [ ] `scene/spriteshade.comp:262-265`. Each step of the serial loop reads `run.at[first + step]`
  from a coherent buffer, then `sprites.at[…]` that depends on it, then two barriers: about 1000
  cycles a sprite on one SM, so about 0.5 ms for a 1000-puff emitter. Target: after the sort, the 1024
  lanes load the next 1024 sorted sprites into shared memory (16 KB), and the loop steps through them
  from there, batch after batch.
- [ ] `lib/payload.glsl:152-183`, `visibility.rgen:197-340`. The payload is 25 words, and up to five
  `traceRayEXT` sites keep two `PaneStack`s (19 floats each), `NearestPane`, the ray and the cone live
  across them. NVIDIA: "Keep the ray payload small" and "Minimize live state across ray-trace calls".
  Target: one loop over (eye, layer) with one trace site. Measure first.
- [ ] `shadowpass.cpp:132`, `specularpass.cpp:55`, `panepass.cpp:52` (`Image::transition` flushes at
  once, `image.cpp:199-204`), order at `denoisepasses.cpp:100-115`. The shadow, glossy and pane passes
  are independent, but each ends with its own compute-to-compute barrier, so each waits for the
  previous pass's tail. Target: hand the images back untransitioned and put all three in one
  `Barriers` in `DenoisePasses::record`, as `blends` is. The timer zones then overlap.
- [ ] `display/displaychain.cpp:113-156`, `bloompass.cpp:102-139`. The histogram, the one-workgroup
  reduction and the glare are independent of the bloom, but each is recorded after it, alone between
  barriers, and the coarse bloom levels (≤ 60×33) leave the card nearly idle per barrier. Target:
  record the histogram with the first halving, put the reduction and the glare between the coarse
  levels, and optionally fold the levels under one workgroup's reach into one dispatch (SPD-style).
- [ ] `shadowfilter.comp:107-114, 185`. Each level reads `CHANNEL_SURFACE` again and rebuilds the
  positions of its whole square (100, 144 and 256 texels for 64 lanes), and reads the centre's
  distance it already loaded. Target: positions and normals once a frame.
- [ ] `upscale/fsrcallbacks.glsl:55` (`FFX_HALF 0`), `:73-76`. AMD's build uses FP16 and forced
  wave64 on RDNA. The port uses full floats, and lets the RDNA 2 driver choose the wave size. Target:
  request a 64-lane subgroup through `VkPipelineShaderStageRequiredSubgroupSizeCreateInfo` where the
  device allows 32 and 64. FP16 needs the pinner to accept 16-bit operations.

## 17. Branches, asymmetries and dead code

- [ ] `shadowtiles.comp:291` (`if (receiver)` around the loads), `shadowfilter.comp:180, 200`. Per-lane
  branches with no measurement named, where `specular.comp:177-179` and `pane.comp:104-106` select.
  The tile-uniform early outs (`shadowtiles.comp:252`, `shadowfilter.comp:153, 162`) are correct.
  Target: selects, or the measurement named where the branch stands.
- [ ] `shadowpass.hpp:24-25`, `specularpass.hpp:19-20`, `panepass.hpp:18-19`, `accumulatepass.hpp:19`.
  Each says it runs "where the wavelet does". `denoisepasses.cpp:57-61, 117-123` runs them whenever the
  denoisers run, also where the wavelet does not, and the accumulator runs `recordSurface` every
  frame. Target: each header states its real gate.
- [ ] `lib/traversal.glsl:652-655`. `lightThrough` is never called. Comments name it as the rule
  (`traversal.glsl:109, 586`, `lights.glsl:61`). Target: remove it, and point the comments at
  `lightPassage`.
- [ ] `upscale/fsrcallbacks.glsl:536-549` (`r_lanczos_lut`, `SampleLanczos2Weight`), `:73-76`
  (`FFX_PREFER_WAVE64`, read by nothing). The reproject and upsample passes use Lanczos types 0 and 2,
  which read no table, and no wrapper binds `FSR3UPSCALER_BIND_SRV_LANCZOS_LUT`. The `return 0.f` stub
  is a silent trap if a type changes. Target: remove both.
- [ ] `tonepass.cpp:24-33` against `tone.comp:33-65`, `upscaler.cpp:122-223` against the `fsr*.comp`
  wrappers, `exposurepass.cpp:247-254`. Binding numbers and formats are shared, but each descriptor's
  type and count are written twice, in the C++ layout and in GLSL, and only a validated run finds a
  difference. Target: the `openmw-rtx-spirv` tool, which already parses every module, writes each
  module's (set, binding, type, count, format) table, and a test or a debug assert holds the layout to
  it.
- [ ] `lib/sprites.glsl:453-466, 645-709`, `mergedPuffs` at `:729-743`. The order-independent
  composite weights by alpha only (Bavoil & Myers). A bright near wisp (α 0.5) in front of a dark far
  puff (α 0.9) composes to about 0.34 of its colour, not 0.5: the farther, denser layer wins. The
  stated reason, that sorting tens of sprites a pixel is not affordable, is not what a k-buffer costs.
  Target: a k = 2–4 register buffer by `mSeen` in `spritesAlong`, inserted by selects, with the tail
  merged (MLAB, Salvi & Vaidyanathan 2014; Multi-Layer Alpha Tracing, Brüll & Grosch 2020). At
  minimum, a WBOIT depth weight (McGuire & Bavoil 2013).
- [ ] `trace/stresspass.cpp:23-26`, `trace/stress.comp:12-15, 36-41`. The stress loop assumes that
  the shader clock ticks at the timestamp period. `VK_KHR_shader_clock` defines no unit and no relation
  to `timestampPeriod`. The two agree on NVIDIA and RADV by coincidence. Target: calibrate once at
  start (a loop of known count between two timestamp queries), or assert the ratio on the first run.

## Checked against the sources and correct

So that no one checks these again: the GGX D, the height-correlated Smith term, the VNDF sampler and
its `G2/G1` weight; the Kulla-Conty compensation and its table; Hanika's terminator; the ray-cone
LOD; the lamp RIS and the ratio estimator's split; the ReSTIR GI Jacobian, GRIS weights, M and age
caps, boiling filter and pairings' involution; ReLAX's anti-lag, boost and history-fix stride; SVGF's
variance propagation with squared weights; the RGB9E5 packing; the FidelityFX shadow denoiser's
temporal and filter passes; the split-sum table; the Jendersie & d'Eon phase function; the froxel
integration's slice midpoints; the Tessendorf waves and the ripple integrator's CFL; the sun disc's
radiance and the Kasten-Young air mass; the sprite scans, sorts and barriers on wave32 and wave64;
the Toksvig and vMF chain of `normalspread`; skinning and the BLAS refit and rebuild policy; FSR's
jitter, phase count, motion vectors, depth parameters and history pairs; Khronos PBR Neutral and the
sRGB encode; and the pinner's orders of operations.
