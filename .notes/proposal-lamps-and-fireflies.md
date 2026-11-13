# Proposal: the lamps' noise and the bounce's fireflies

Two faults. The lamps' is in how a lamp is drawn, and is fixed where it is drawn; the fireflies' is a sample the
renderer cannot see coming, from content it does not have, and is fixed where every sample meets the denoiser.

- **The lamps.** The eye's split hit sums every lamp's unshadowed light exactly and multiplies it by one bit: whether
  the ray to the lamp the reservoir held, drawn in proportion to that light, got through (Heitz et al. 2018's ratio
  estimator, `CHANNEL_LAMPED`). Where the lamps that weigh most are stopped — a lamp behind a pillar, one in the next
  room whose reach crosses the wall — the bit is open on few pixels and each carries the whole sum: one pixel in 94
  over the noisy band at the Census and Excise Office, one in 3.6 over the lit wall beside it
  (`docs/research/2026-11-13-lamp-shadow-rays-at-the-census-office.md`). A bit whose mean is 1% has a relative
  deviation of about ten per pixel per frame however it is filtered.
- **The bounce's fireflies.** A glow has no lamp of its own and lights only through the bounce rays that land on it
  (`EMISSIVE_INTENSITY`, `landingLightOf`); a small bright one is found by few of them, each carrying all of it. What
  players report comes from content this tree does not have — a replacer's glow, a mod's emissive map — so the fix is
  the denoiser's, which meets every such sample whatever made it, and a glow stays what it is. A test emitter placed
  in the Balmora Mages Guild makes them: none at a vanilla material's full glow, 52.86 a thousand at ten
  times it, 216.97 at a hundred, where the denoised frame is noisier than its 16 frames averaged. The denoiser's
  firefly hold as built takes none of them, because they arrive in settled histories; held at every history, as
  ReBLUR holds, it takes 94% of them and doubles the frame's bias, since what it holds down is the ball's own light
  (`docs/research/2026-11-13-a-bright-emitter-in-the-mages-guild.md`).

## Decisions: the lamps


- **The held lamp is drawn from last frame's visible lamps, per 8×8 tile**, as Unreal's MegaLights guides its
  samples (Narkowicz and Costa, SIGGRAPH 2025 Advances, "Sample Guiding"): after the trace, the lamps whose rays got
  through in a tile make that tile's list, and the next frame draws most of its rays from the list at the pixel's
  reprojected place. A lamp the room's walls stop is never on it. Refused: ReSTIR's temporal and spatial reuse with
  visibility (Bitterli et al. 2020; Wyman and Panteleev 2021; Tokuyoshi 2024), whose resampling MegaLights found a
  high constant cost and whose repeated samples correlate where the denoiser wants them independent, and which this
  renderer removed from the bounce for its bias (`docs/architecture.md`, "No reuse of the bounce"); weighting each
  lamp by its rays' share that got through, which MegaLights tried and refused because it traces fewer rays into
  penumbras, where the noise is; more rays a pixel, whose noise falls as a root, so one in 94 stays one in 24 at
  four rays.

- **The unshadowed sum stays exact, and the lamps' field carries a fraction, not a bit**: the held lamp's share of
  the sum over the chance it was drawn, `w V / (p S)`, where `w` is its unshadowed weight, `V` whether its ray got
  through, `p` its chance under the steered draw and `S` the exact sum. Its mean is the visible share, as the bit's
  is, and once the draw follows the visible lamps it takes nearly the same value on every pixel instead of nought or
  one: on the band, about `0.0106 / 0.8` on the 80% of pixels that draw from the list and nought on the rest. It
  reaches past one only where a lamp off the list was drawn and got through, up to one over that share (5 at 20%):
  the floor's price, paid where a lamp is coming into view. This is
  Heitz et al.'s estimator in its general form, which takes any distribution; the bit is its case for `p = w / S`.
  Refused: rounding the fraction to a bit at random, whose mean is the fraction and whose noise is the bit's; and
  dividing out one important light per pixel, which MegaLights tried and refused because the choice of light is
  itself noise and shows.

- **Every other lamp keeps a share of the draws: up to 20%, and more only where the list's lamps deliver next to
  nothing themselves** — an empty list, or one whose light is below what a pixel shows — as MegaLights caps its
  hidden-light reservoir and relaxes the cap for dim visible lights. Never by the list's share of the sum: at the
  office it is 1%, which is the case to steer and not one to fall back from. A lamp that comes into view is found within a few frames, and every
  lamp with a weight has a chance above nought, so the estimate is unbiased whatever the list holds: a stale list, a
  wrong one or none costs noise and never bias. A disocclusion or a cut, where the pixel's history is lost, takes
  half its draws from the other lamps (MegaLights' 50%). Refused: weighting the hidden lamps down, which lets a
  strong hidden lamp take most of the draws again — the case this exists for.

- **Two reservoirs, merged**: the list's lamps in one, the rest of the cell's in the other, each with a draw of its
  own so they are independent, the second's weight capped before the merge (MegaLights, "Hidden Lights"). Each
  lamp is weighed once, as `weighLamps` weighs it now, and the exact sum is the two reservoirs' sums.

- **A tile's list is the lamps its pixels' rays reached, sorted by row, at most 8**: a tile of 64 pixels tracing
  one ray each finds few distinct lamps a frame, and a pixel's history converges on fewer than that (MegaLights'
  lists are bounded the same way, by the shaded samples a tile holds). Built from the trace's own answers, so it
  costs no ray; MegaLights measured its build at 0.05–0.1 ms. Kept for two frames, the one being built and the one
  drawn from.

- **The pixel reads the list of a tile picked at random among the four nearest its reprojected place, by their
  bilinear weights** (MegaLights' "stochastic bilinear visible light list lookup"): a tile's edge would otherwise
  show as a seam where the draw changes from one list to the next.

- **A lamp is named by its row in the light table, and a table laid out again empties the lists**, as a new world
  resets the histories. A row that names another lamp after a change only steers the draw; the floor above keeps
  the estimate unbiased.

- **Only the eye's split hit is steered**, the one whose lamps' light the shadow denoiser filters as a field. A
  path's far end, a pane and a water leg draw their candidates as now: their light is filtered with the bounce's or
  the layer's, and their lamps are rarely the frame's noise.

- **The shadow denoiser's lamp field takes the fraction**: its mask's open bit becomes "a fraction above nought",
  which is what the tiles classify by, and its temporal blend and its spatial filter take the fraction where they
  took the bit (the bit already reaches them as a mean once its history is blended). The reach stays the whole reach
  wherever the draw was among several lamps, as `seenSolidOf` gives it now.

## Decisions: the fireflies

- **Each sample of the bounce is held under forty times the frame's white, and what it carried past is dropped**
  (`ACCUMULATE_FIREFLY_CEILING`), as every shipping real-time path tracer holds a rare bright sample: Unreal's Lumen
  at a multiple of the exposure (`r.Lumen.ScreenProbeGather.MaxRayIntensity`, 40 by default), NVIDIA's Omniverse
  per sample before the exposure (`fireflyFilter:maxUnexposedIntensityPerSample`), Unreal's path tracer
  (`Max Path Intensity`) and Blender's Cycles (`Clamp Indirect`). Refused, each tried at the guild and judged by eye
  still blotched on every frame: ReBLUR's ring on every history, which doubled the frame's bias; the ring's excess
  spread back over the surface, energy-conserving (Zirr, Hanika and Dachsbacher 2018), at every reach tried, since a
  few rays a frame find such a glow; and the excess dropped by the ring. Refused by the user: a lamp of its own for
  every glow (next-event estimation on emissive geometry, as Quake II RTX and RTXDI do), which fixes the glows a lamp
  is made for and not the samples a mod's content makes.

- **The white is the exposure the frame before was shown at**, as Lumen's pre-exposure and Omniverse's scaling hold
  it: one ceiling at every brightness, as Blender's is, cuts a bright day's bounce or none of a dim room's. The frame
  before's, because this frame's exposure is measured from the picture the trace makes.

- **The bias is the price, so the hold stays under the A/B switch until the suites judge it**: a surface lit mostly by
  a glow the bounce finds rarely loses most of that light for good — 0.212 of its mean kept under the lantern sheet of
  `theRingLeavesASettledMeanItsLight`, shown at one. Whether the ring of short histories stays beside it under the one
  switch is decided with the measurement.

## Costs


- The lists: `ceil(width / 8) × ceil(height / 8)` tiles of 9 words, two frames of them: 4.1 MB at a 2560×1440
  trace, 1.0 MB at 1280×720.
- The fraction: a float a pixel where the lamps' field holds a sign now; where it lives (the field's alpha beside
  the penumbra, or an image of its own) is the implementation's to settle on the formats the run keeps radiance in.
- One pass after the trace to build the lists, and the second reservoir's weighing in `weighLamps`, which weighs
  the same lamps it weighs now.

- The ceiling: the exposure's one float read by every accumulator thread, and a division a pixel.

## Plan

Each batch leaves the build, the gate and `./omw noise` on its places green. The first is what the other two are
judged by; the other two stand apart.

### 1. The places and the measures

- [ ] `census-office-wall` in `files/rtx/views.cfg`, and a `noise` suite that visits it beside a place where the
  player's content fireflies: the places this proposal is judged by.
- [ ] `noise` reports each place's worst tiles beside the frame's figures: the office scored "as clean" or near it
  with its noise in a seventh of the frame.

### 2. The fireflies

- [ ] `./omw noise --versus=antifirefly=1` on the `bounce` suite and the firefly place, still, strafed and cut: the
  bias the ceiling adds against the fireflies it takes, then on by default or not, and the ring kept beside it or not.
- [ ] The glossy filter's samples (`CHANNEL_SPECULAR`) under the same ceiling, if a lobe that finds a glow fireflies
  where the bounce no longer does.
- [ ] `./omw repeat --pairs=10`, `./omw release bench`.

### 3. The lamps' draw

- [ ] The visible-lamp lists: a pass after the trace, 8×8 tiles, at most 8 rows, two frames kept, emptied when the
  light table is laid out again.
- [ ] `weighLamps` for the eye's split hit: the list's lamps and the rest in two reservoirs, the rest capped at 20%
  of the draws and at 50% where the pixel's history was lost, the list read at a tile picked by the bilinear weights
  of the reprojected place.
- [ ] `CHANNEL_LAMPED` carries the fraction `w V / (p S)`; the shadow denoiser's lamp field filters it, its mask's
  open bit a fraction above nought.
- [ ] Tests: the lists a tile builds, the fraction's mean equal to the bit's on a fixed scene, the floor's chance
  above nought for every weighed lamp.
- [ ] `./omw noise` on the suite before and after, `./omw repeat --pairs=10`, `./omw release bench`.

## Supersedes

- `docs/architecture.md`, "No reuse of direct light": it stands for the light, which is still traced fresh each frame.
  What last frame decides is only which lamp this frame's ray goes to.
- The accumulator's "No outlier clamp on the sample": it stands for a ceiling set by the pixel's own spread, and the
  exposure's ceiling is the display's.
