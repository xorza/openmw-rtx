# GPU pipeline review

Tree at `8f61a220f0`. Line numbers refer to that commit.

Scope: every shader in `components/rtx/shaders` and `components/rtxvulkan/shaders`, the host code that
records and feeds them, the shared GPU structures, and the frame and device layers around them. Six
reviewers read the code in parallel, one area each: the primary trace, the denoiser and upscaler,
media/water/sky/sprites, the scene and texture data, the device/frame/display layers, and the
GLSL↔C++ interface with the pass graph. Nothing was built or run, so every cost below is arithmetic
from formats and extents, or a figure quoted from a code comment. Each batch says how to measure it.

The findings are in **batches**. One batch is one change set that touches one area, so you can do
it in one go. The batches, and the findings inside each, are in order of impact: what is wrong
first, then what is imprecise, then what costs, the biggest cost first.

Labels on each finding: **kind** (bug, latent, perf, simplify, robustness), **severity** (H/M/L)
and **confidence** (H/M/L). The ID is the reviewer's own (TRACE, DENOISE, MEDIA, SCENE, FRAME, XCUT).
When two reviewers found the same thing, the finding has both IDs.

## Summary

| # | Batch | Findings | Why it is here |
|---|---|---|---|
| 1 | Display and presentation | 5 | A bloom weighed by the last exposure, input latency, two extra full-frame copies and submits |
| 2 | Lamp sampling | 3 | O(lamps in cell) per water pixel, up to 3×256, a false invariant, and an unbudgeted darkening walk |
| 3 | Layered ground in the trace | 2 | A stage split that does not exist, and bounce hits that sum the whole stack |
| 4 | Scene record layout for the trace | 3 | Dependent loads and wide rows in the hottest loops. A/B first |
| 5 | Housekeeping | 2 | A pipeline cache only a clean exit writes, and stale comments |

15 findings are open: 0 high, 2 medium, 13 low. No reviewer found a GLSL/C++ layout,
binding or format mismatch. The interface checks (`pushDisagreement`, `bindingDisagreement`,
`storageformat.h`, `mayRoundTowardNought`) hold.

---

## Batch 1 — Display and presentation

All of these items touch `display/`, `gui/` and `present/`.

### FRAME-7: The bloom's Karis weight reads the previous frame's exposure, though nothing prevents recording the exposure first
bug · L · conf H — `display/displaychain.cpp:117-147`, `shaders/display/bloomdown.comp:25-31`

`mBloom.record(..., mExposure.getExposure(), ...)` runs before the exposure is recorded. Both read only
`shown`. Under `FixedExposure`, frame 0 of a reference run weighs the pyramid with what the buffer held
before, so frames 0 and 1 differ on the same input.

**Direction:** record the exposure (fixed or measured) before the pyramid, and delete the lag comment.

### FRAME-12: Nothing paces the CPU to the display, so input-to-photon latency under FIFO is about 3 vblanks plus the ring
perf · L · conf M — `present/swapchain.cpp:133-135`, `present/presenter.cpp:150-262`, `apps/openmw/mwrender/rtx/rtxrenderer.cpp:838-864`

`minImageCount + 1` images, and the only pacing is the ring's wait for frame N−2 and the blocking
acquire. `present_wait` and the present fence (already owned) are not used to hold the next frame's
input sampling.

**Direction:** before the next frame's update, wait for the present of frame N−1 (present-wait or the
present fence), or keep at most one present queued.

### FRAME-6: After the tone curve, every frame makes two more full-frame copies in two more submits
perf · L · conf M — `gui/guidrawer.cpp:46-90`, `present/presenter.cpp:179-254`, `vulkanrenderer.cpp:558-580`

`GuiDrawer::draw` always draws the picture whole into `shown` and submits. `Presenter::present` then
blits `shown` to the swapchain in a second submit, and each command buffer opens with a full head
barrier. At 7680×2160 that is about 265 MB per frame, two extra `vkQueueSubmit2` calls and two drains,
in menus too. Estimated 0.3–0.6 ms at the user's resolution.

**Direction:** record the GUI into the present's command buffer. At Native with no letterbox, draw the
picture and interface directly into the swapchain image (`COLOR_ATTACHMENT`), and skip `shown` and the
blit. At minimum, merge the GUI and blit submits.

### FRAME-5: A full-size screenshot goes through the area-resampling path and allocates about 400 MB of doubles at 7680×2160
perf · M · conf H — `rtx/renderer/frameimage.cpp:55-102`, `apps/openmw/mwrender/rtx/rtxrenderer.cpp:548`

The fast path needs `Channels::Rgba`, and `saveScreenshot` asks for `Rgb` at the frame's own size. It
takes the general path: `std::vector<double>(width × height × 3)` = 398 MB, and about 50 M lambda calls
whose weights are all 1.

**Direction:** extend the fast path to any channel set at the identity size (copy rows, drop alpha).

### FRAME-14: The exposure histogram bins every output pixel
perf · L · conf M — `display/exposurepass.cpp:67-117`, `shaders/display/histogram.comp:39-55`

16.6 M loads and up to 256 global atomics per group at 7680×2160. For a trimmed log-luminance mean, a
regular 2×2 subsample is an unbiased estimate (a box-filtered level is not).

**Direction:** bin one pixel per 2×2, with a fixed offset or an offset that turns with the frame.

---

## Batch 2 — Lamp sampling

### TRACE-1: Every split hit walks every lamp in its cell, up to 256, with the full glossy lobe per lamp. The comment says the water legs do not
perf · M · conf H (behaviour), M (cost) — `shaders/lib/shading.glsl:372-375,199-204`, `shaders/lib/water.glsl:107`, `shaders/lib/lights.glsl:131,444-458,566-618`

`weighLamps(..., split ? 0u : frame.mLampCandidates)`, where 0 means every lamp. The comment above
says that "a water leg" takes the fixed count, but `waterRay` shades each leg with
`shadeAtPathEnd(..., true, ...)`, so `split` is true. A shoreline pixel adds the bed. One water pixel
can walk the cell list three times (reflection, refraction, bed), up to 3×256 evaluations, each with
a 40-byte load, `lampAt`, a PCG step and, on a glossy surface, the full GGX `reflectionAt`. Every
other path end pays 8. Canals at night are the worst case.

**Direction:** keep the exact unshadowed sum over all lamps, but with only falloff, cosine and
Fresnel. Draw the reservoir from `LAMP_CANDIDATES` uniform candidates weighed with the full lobe (RIS
stays unbiased). At minimum, correct the comment. Measure on a lamp-dense water view, and check
`./omw noise` for the variance cost.

### TRACE-9: The blue-noise comment says that the R2 pairs are never read together, but the bounce pair and the sun-disc pair are read at the same hit
robustness · L · conf H — `shaders/lib/bluenoise.glsl:25-36`, `rtx/shaders/scene.h:159,178`, `shaders/lib/shading.glsl:253,953`

`STREAM_BOUNCE` and `STREAM_SUN_DISC` both turn by R2, and `shadeSolid` reads both at the same pixel,
so their offset never changes over time. This is harmless today (separate terms, separate filters),
but the stated invariant is false.

**Direction:** give the sun pair its own irrational step (as the lamp pair has), or correct the comment.

### TRACE-8: `darkeningAt` walks every darkening lamp at every gather, with no budget, even when no lamp was held
perf · L · conf H — `shaders/lib/lights.glsl:626-638`, `shaders/lib/shading.glsl:398-402`

Its result only multiplies `lampsArriving`, which is zero when `kept.mWeight == 0`. Negligible in
vanilla. Unbounded with mods that place many negative lights.

**Direction:** skip the walk when `kept.mWeight == 0`. Give the far-hit, pane and leg calls the same
candidate budget as the positive lamps.

---

## Batch 3 — Layered ground in the trace

### TRACE-3: `LAYERED` does not remove the layer-stack loop from the surface and water stages
simplify · L · conf H — `shaders/trace/visibilityhit.rchit:15-18`, `shaders/lib/traversal.glsl:1055-1059,1409-1412`

The rchit header says that `LAYERED` folds the loop out of the two stages that no terrain reaches. But
every inline trace in those stages (bounce, shoreline bed, both water legs) goes through `resolve`,
which hard-codes `layered = true`. Only the primary hit's detailed copy is removed.

**Direction:** correct both comments. Measure the stage with `LAYERED` forced true. If nothing
changes, remove `SPEC_LAYERED` and read `mGround` from the material row.

### TRACE-4: A diffuse bounce's far hit on near ground sums the whole layer stack
perf · L · conf L — `shaders/lib/traversal.glsl:1060-1065,1216-1271`, `shaders/lib/ground.glsl:134-158`

A non-detailed hit on a chunk that kept its stack loops over every layer: a 64-byte row, four mask
loads, and a diffuse plus delight fetch per showing layer. The `resolveFor` doc already concludes that
detail at a bounce's far hit is invisible. Composites exist only for chunks past `wantsFlattening`.

**Direction:** bake a composite for near chunks too, at load, through `groundcomposite.comp`. Resolve
non-detailed hits from it. Keep the live stack for the eye, reflections and the bed.

---

## Batch 4 — Scene record layout for the trace

Each item changes what the hottest loops load. The gain is unmeasured, so A/B each item on an
interior and a foliage exterior, and keep only what measures.

### SCENE-6: The candidate loop's rows are read with a 4-byte alignment claim, and the hot material fields are spread across 108 bytes
perf · L · conf M — `shaders/shared/tables.h:14-26`, `shaders/lib/bindings.glsl:296-309`, `rtx/shaders/scene.h:648-675,772-776,1142-1227`, `shaders/lib/traversal.glsl:424-470`

`InstanceTable` and `LightCellTable` claim `TABLE_ALIGN_ROWS` (4), but their rows are 64 and 16 bytes
on a 16-aligned base. `GpuLightCell`'s doc ("both runs in one fetch") does not hold under a 4-byte
claim. `candidateStops` reads `GpuMaterial` fields at offsets 0, 4, 8, 60–76 and 104, which is up to
four sectors per candidate on a cutout.

**Direction:** claim 16 for both tables, and correct the `tables.h` comment. Move the candidate's
fields to the front of `GpuMaterial`, pad it to 112, and claim 16.

### SCENE-7: Every attribute fetch goes through a block-table load that the mesh row could carry already resolved
perf · L · conf L — `shaders/lib/bindings.glsl:236-283`, `shaders/lib/geometry.glsl:33-40`, `rtx/shaders/scene.h:587-620`, `scene/scenebuffers.cpp:216-233`

instance → mesh → `BlockTable` → indices → `BlockTable` → uvs → `TexelTable` → sample. A mesh's runs
never cross a block (`checkFits`), so each run's address is a per-mesh constant. `mMeshTable` is
already one copy per slot, so the posed streams fit as well.

**Direction:** prototype `GpuMesh` with run addresses (about +40 B per row). That removes
`POSED_FIRST_BLOCK`, `mNormalShift` and the `JoinedBlocks` joining. Marginal: an A/B experiment, kept
only on a measured gain.

### SCENE-10 / XCUT-9: Skinning and morphing record one dispatch and one push per deformed mesh
perf · L · conf L — `scene/skinpass.cpp:49-147`

A street of 30 NPCs is 100–200 small dispatches per frame, with tails smaller than a wave and a host push each.

**Direction:** write per-mesh constants into a per-slot table with a vertex-count prefix, and run one
dispatch per kind (a binary search over the prefix, or indirect). Measure the `skin` zone first.

---

## Batch 5 — Housekeeping

### FRAME-11: The pipeline cache is written only by a clean destructor
robustness · L · conf H — `device/pipelinecache.cpp:253-262`, `trace/visibilitypass.hpp:159`

Every pipeline is made at start, but a crash or a kill later in the session discards the whole compile.

**Direction:** write the cache when `awaitKernels` reports done (the write is already crash-safe).
Write it again at exit only if the blob changed.

### MEDIA-12: Two comments describe code that changed
simplify · L · conf H — `shaders/lib/sprites.glsl:825-837` (`mergedPuffs`), `device/memory/image.cpp:396-397` (`buildMips`)

`mergedPuffs` says that each walk reports an alpha-weighted mean colour, but `spritesAlong` now
composites its nearest four in depth order. `buildMips` says that the fog samples the wave tiles "as a
dispatch", but the fog samples them in ray-tracing launches.

**Direction:** restate both comments.

Comment corrections that belong to a batch above stay with their batch: TRACE-1 and TRACE-9 (batch 2),
TRACE-3 (batch 3).

---

## Checked and sound

The reviewers checked these items and found no fault:

- **History validity.** The first frame, a resize, a cut, an unfiltered frame and a mode change are
  handled through one table (`TemporalTurns`, `DenoiseHistory::discard`, `FramePast::mPastLost`,
  `Upscaler::reset`). Every barrier between accumulator, clamp, shadow, glossy, pane, cascade,
  composite and FSR was traced, including the ping-pong write-after-read cases.
- **FSR port.** The host constants, formats, dispatch sizes, clears, parity and SPD setup match the
  3.1.4 SDK as far as the vendored headers allow a check.
- **GLSL/C++ interface.** All shared structs are scalar layout, and no struct has the one shape where
  scalar GLSL and C++ differ. Push ranges, bindings, descriptor types and specialization IDs are
  checked against the module. No helper is written twice under two names.
- **Frame-slot lifetime and present sync.** Per-slot host writes assert idleness. Graveyard stamps are
  monotonic. The acquire and render semaphores are guarded correctly. Swapchain recreation waits idle.
  sRGB is encoded once.
- **Tone and exposure.** Khronos Neutral with the ramped offset is continuous at the knee. The TPDF
  dither is placed correctly. The exposure bin centres and the trimmed mean are right, and the float
  sums are exact below 2²⁴ pixels.
- **SPIR-V pinning.** Operand classification, single-use fusion, `precise` propagation and float
  widths hold within the stated contract. Since the review, the optimizer is handed the module
  guarded (`Rtx::guardFloatArithmetic`), so it folds none of the arithmetic the pinning holds, and
  the pinning folds the constants itself, exactly, and divides by a constant's reciprocal.
- **Pipelines.** All are compiled at start, with no compile during play. `SET_PASS` is pushed per
  dispatch, with no descriptor pool on the frame path.
- **Acceleration structures.** A full TLAS rebuild with `PREFER_FAST_TRACE` when something moves, a
  deforming-BLAS refit with a rebuild on a rota (the flags match), compaction without waits, and
  `NO_DUPLICATE_ANY_HIT` on every BLAS.
- **Texture kernels.** The BC7 mode-6 layout, the anchor swap and the endpoint quantisation are
  correct. The mip chain is alpha-weighted and sRGB-correct, with correct odd-extent taps. Ground
  composites are capped at 2 per frame.
- **Trace.** The payload pack/unpack (30 words), the rgb9e5 range, the `HitRecord`/SBT order against
  `MaterialKind`, push-descriptor persistence across binds, `tmin ≤ tmax` at the clip, and RIS and
  sky-pick unbiasedness.
- **Fog and waves.** Fog reprojection and history rejection are correct. The FFT's normalisation, centre
  shift, conjugate pairing and Nyquist exclusion are correct. Rows and columns cannot fuse, because a
  512² grid does not fit in shared memory. Since the review, the twiddles come from a table the host
  computes in double (`Rtx::waveTwiddles`), and the time phase is turned by exact quarter turns
  before `sin`/`cos` (`phasorAt`), so the driver's precision applies only inside an eighth of a turn.
- **Ripples.** Since the review, a press is as deep as the time it stands for (`kept^n`), the
  pass drops an impulse whose ring cannot reach the window and, past the cap, the impulses farthest
  from the eye, and the field is not stepped while it is
  still and nothing presses it: a dead band under `RIPPLE_STILL` makes "still" exact, and the
  device reports it.
- **The narrow wave cascade's idle lanes (MEDIA-10), measured and dropped.** The 128-point cascade
  runs on 256-lane workgroups, but its two dispatches cost about 1 µs: at `seyda-neen-pier` in
  release, the `waves` median is 0.1489 ms with them and 0.1477 ms without them, 1,200 frames a leg.
  A smaller workgroup cannot save more than that.
  The FFT is correct.

- **Dropped as better left as they are**, each for what it would cost against what it could give:
  - **XCUT-3**, the histogram inside the bloom's first halving: FRAME-7 records the exposure before the
    bloom, so the histogram cannot live inside it; FRAME-14's subsample is the cost fix that stays.
  - **SCENE-12**, octahedral normals and a 16-bit colour: the trace's geometry quantized and the
    picture moved, for an unmeasured gain.
  - **TRACE-5**, the payload's radiances as halves: a second payload layout for every pipeline,
    since a reference needs full floats, for a gain only an AMD card could judge.
  - **DENOISE-8 / XCUT-11**, the transients aliased in an arena: memory is not short — about 1.7 GB
    reserved against a budget of about 13 GB — and aliasing adds lifetime and barrier hazards and an
    arena the tree does not have.
  - **FRAME-16**, statistics captured only on request: the register counts are in use, and the
    cost falls at pipeline creation, at start and never during play.
  - **XCUT-10**, the four probe kernels merged: test-only shaders with nothing wrong in them, and
    merging them only adds specialization plumbing.

- **TRACE-6, withdrawn.** It said that an opaque texel of a see-through caster never stops a
  shadow ray. A see-through surface has no opaque texel: `Material::isTranslucent` needs an opacity
  under 1 or a texture whose alpha never reaches 255 (`reachesSolid`), and a fade under 1 keeps
  `sampledOpacity` under 1 as well. Filtering cannot go above the largest texel.

- **XCUT-1, void by count.** It asked to specialise the fill out where no placed material's ambient
  term differs from its diffuse. The scene report's `ambient apart` line, over every view of
  `views.cfg`: placements with an ambient apart at every place but `arkngthand` and
  `mournhold-arrival` (0 of each), and 7 to 401 elsewhere. No vanilla frame of the suite would take
  the specialisation.

- **TRACE-2, the panes' half void by count.** It asked to gate the four pane channels on a
  see-through layer that can be met. The same line counts see-through placements: none only at
  `wolverine-hall`, `addamasartus`, `ahemmusa-yurt` and `mournhold-arrival`, and 7 to 697 elsewhere,
  so a gate per scene stays open nearly everywhere. The lobe's half is done.

- **XCUT-7, closed.** It asked to store the specular albedo as one rgb9e5 word. Once the lobe's
  channels are gated it saves 4 B/px in a scene that wears a map and nothing in a vanilla one,
  against an integer channel through the digest and `readChannel`. The alpha half was void: no
  three-channel half format takes storage.

- **The gated channels, measured.** The lobe's pair and the puffs' layer gated, against the same
  build with the gates open, on the default suite at 1280×720 traced, release, two legs each after
  a warm-up: trace medians 2.92/2.93 against 2.90/2.91 ms at seyda-neen-ship, 3.53/3.57 against
  3.58/3.56 at dawn, 1.82/1.87 against 1.85/1.84 at balmora-mages-guild — inside a leg's spread.
  16 B/px is about 15 MB a frame there, some 0.03 ms. Every place of the suite has a puff, so the
  composite's skip did not apply.

- **The denoiser's pass graph, measured** (DENOISE-2/XCUT-2, DENOISE-3/XCUT-4, DENOISE-5, DENOISE-7,
  DENOISE-9; default suite, release, two legs each, medians). The families recorded stage by stage,
  six drains where thirteen were: the stages before the wavelet 0.650–0.679 to 0.639–0.648 ms on the
  ship, 0.750 to 0.732–0.738 at dawn, level in the guild — the drains were short. The shadow filter
  over both fields in one dispatch a level: 0.231 to 0.219 ms at dawn, where both run, level
  elsewhere; the mask and the tiles left a dispatch a field, for a gain of the same size against
  doubling the tiles' state. The composite in the last wavelet level: the last level and the
  composite 0.172 to 0.116–0.125 ms on the ship, 0.175 to 0.126 at dawn, 0.204 to 0.152 in the guild.
  The held surfaces replaced by the frame before's surface channels: the zones level, 16 B/px of
  memory and of writes fewer, each texel rebuilt through its own eye. Every step but the last the
  same to the bit at every view; the last moved by a level of 255, and by up to 25 on 0.01% of
  `mournhold-arrival`'s pixels, isolated in its dark.

- **The sprites and the fog, measured** (MEDIA-4, MEDIA-5, MEDIA-8, MEDIA-3, MEDIA-1; release, two
  legs each after a warm-up, at vivec, seyda-neen-ship and ald-ruhn under `--weather` Ashstorm and
  Snow, medians/p99 in ms). The self-shadow walk staged a workgroup of the sorted run at a time into
  shared memory: `shade` under snow 1.016/2.243 to 0.638/0.672 at Vivec and 1.019/2.148 to
  0.642/1.903 on the ship, under ash 0.129 to 0.092 and 0.070 to 0.050. A second copy of the grid,
  one barrier a step, measured slower (0.81 under snow) and was dropped. The fog history in halves
  rounded at random: `air` 0.210 to 0.203, 0.242 to 0.234, 0.200 to 0.193; `column` 3–9% lower; the
  noise and the bias at balmora-fog-night, balmora-storm-night and seyda-neen-pier the same to the
  verb's two decimals; half the memory, some 118 MB at a 3840×2160 traced frame, and no device need
  of a filtered 32-bit float. The unbinned walk's skip and the fill's stop draw the same list and the
  same picture; the list sized from the floor only until a report lands. Every picture of the suite
  within a level of 255.

- **DENOISE-10, measured and left.** The driver does not merge a history tap loaded for the test and
  again for the sum (each binary 1 to 2% smaller loaded once), and every way of loading it once cost
  more: a `vec4` a corner kept from the test was laid in shared memory (768 bytes a workgroup in the
  pane filter), and a gather unrolled a corner a block spilled the accumulator to local memory. The
  shadow tiles' `vec2` a corner moved the composed frame only because the driver folded the
  kernel's `exp` weights apart in the two modules; with the weights the build's
  (`shadowLocalWeight`) it draws the same to the bit, 128 bytes smaller and a register fewer, and
  is not timed. The clamp's two loads stand in two loops over squares of different sizes, and the
  narrow wavelet's centre and middle tap are one coordinate once the loop unrolls.

- **FRAME-2 / SCENE-14, withdrawn by measurement.** It said that the graveyard frees all of a
  crossing's burials in one collect, so one frame absorbs the sweep. A release profile of the
  streaming suite, 6.59 s over 19 crossings: `Graveyard::freeThrough` is 0.02% of it, about 1.3 ms in
  all, under 0.1 ms a crossing. The worst frames of the same suite are the game's own `update`, 12–24
  ms on the CPU.

- **SCENE-5, measured and left.** It said that all of an arrival's BLAS builds land on the arrival
  frame. The streaming suite's `Blas` zone, on the 367 of 598 frames that build: median 0.24 ms, p99
  0.58 ms, worst 1.23 ms — a twentieth of the game's own worst `update`. A triangle budget would hold
  a mesh out of the trace for frames (pop-in) to move a millisecond that no frame shows.

- **FRAME-1, withdrawn by measurement.** It said that the harness's counting kernel, one atomic per
  missed primary ray into host memory, makes `bench` slower than the game. Three release legs back to
  back, counting on, off and on again, at seyda-neen-ship, seyda-neen-ship-dawn and balmora (84–88%
  of the primary rays hit): the trace median with counting off fell between the two legs with it,
  2.638 / 2.682 / 2.698, 3.183 / 3.225 / 3.230 and 1.590 / 1.610 / 1.609 ms. The atomics go to one
  address, which the compiler joins per subgroup; the digest's slow atomics went to many.

- **FRAME-4, not reproduced.** It said that a timeout from acquire or the present fence, treated as
  device loss, ends a game whose window the compositor stopped showing. Minimized through KWin
  (Plasma 6, Wayland, NVIDIA) for about 45 s with a FIFO swapchain, the game kept presenting at
  about 31 frames a second and nothing waited near its 10 s patience. A window on another virtual
  desktop, and other compositors, were not tried.

- **FRAME-8, measured safe.** It said that host-written memory could run out of a 256 MiB BAR heap
  on an RTX 20 card. At the largest distant land setting (`landCells` 10, distant statics on), the
  busiest exteriors (Vivec, Ald-ruhn, Sadrith Mora) and a streamed flight held 77.8 MiB live and
  104 MiB reserved. Running out would still be a `DeviceError` with a message, not a silent fault.

## Not reached

- `rtx/preprocess/shape/*`, `rtx/image/{alphaimage,colour,colourblock,texels,formatcensus}` in depth,
  `rtx/scene/{materialtable,placementtable,scenedesc}` in depth, `mirror/*` beyond the pose path.
- `environment/{atmospheremesh,cloudmesh,moonfaces,skymesh,skysheet,wavespectrum}.cpp`.
- The bodies of `spirvpin.cpp` (beyond its classification), `spirvinterface.cpp`, `spirvdigest.cpp`.
- `lib/water.glsl` and `lib/medium.glsl` were skimmed. The fixed-point share accumulation in
  `medium.glsl` is not proved free of overflow for unlit sheets.
- The vendored FidelityFX host source (`ffx_fsr3upscaler.cpp`) is not in the tree, so the reset clears
  were checked against the port's comments only.

## Frame pass table

World frame, denoised and upscaled, in record order. **T** = traced extent, **O** = output extent,
**C** = fog columns (T/12 per axis), **G** = 8×8 groups. `│` = a compute→compute drain after the pass.

| # | Pass (module) | Dispatch shape | Reads | Writes | Barrier |
|---|---|---|---|---|---|
| — | skin / morph (`skin.comp`, `morph.comp`), at placement | one dispatch per deformed mesh, 64-lane groups | bind streams, influences, bones/weights | pose blocks (slot) | │ |
| — | TLAS pack (`toplevelpack.comp`) + BLAS refit / TLAS build, at placement | rows/256 | instance rows, starts | packed instances, AS | │ |
| 1 | glare clear | fill | — | sun-glare counts | — |
| 1 | ripple step ×1–4 (`ripplestep.comp`), clock moved and field moving or pressed | 1024²/16² | field[before], impulses | field[after], moving word (last step) | │ each |
| 2 | ripple compose (`ripplecompose.comp`) + blit mips, as the step | 1024²/16² | field | ripple surface, curvature (+mips) | │ |
| 3 | waves: `waverows.comp` ×2 │ `wavecolumns.comp` ×2 + blit mips (sea, clock moved) | one group per row/column | amplitudes, turn rates, twiddles | wave surface, curvature (+mips) | │ │ |
| 4 | frame block update (`vkCmdUpdateBuffer`, 1632 B) | — | — | `VisibilityConstants` | │ |
| 5 | sprite shelter (`spriteshelter.rgen`), when shelter > 0 | sprites × 1 | TLAS, frame, sprites | sprite copy | │ |
| 7 | sprite emitters (`spriteemitters.rgen`) | emitters × 1 | frame, emitters, fog | emitter frames | │ |
| 8 | sprite shade (`spriteshade.comp`) | one group per emitter per light | sprites, emitters | sprite light/order | │ |
| 9 | sprite bin: clear │ `spriterects` │ `spritestarts` │ `spriteruns` | various | sprites, emitters, presences, camera | rects, presence words, starts, runs, report | │ │ │ │ |
| 10 | channels begin (UNDEFINED → write, ×19) | — | — | — | │ |
| 11 | fog depth (`fogdepth.rgen`) | C.x × C.y | TLAS, frame | column depth, column moons | │ |
| 12 | fog scatter (`fogscatter.rgen`) | C.x × C.y × 64 | column depth/moons, fog history, field, lights | scatter, sunward, lamps (3D) | │ |
| 13 | fog integrate (`fogintegrate.comp`) | C/8² | scatter, sunward, lamps | air, slices, seeing (3D) | │ |
| 14 | trace (`visibility.rgen` + rchit×3, rahit, rmiss×2) | T.x × T.y | TLAS, frame, tables, textures, waves, ripples, fog, sprite list | 19 channels, counts, glare | │ |
| 15 | temporal stage: accumulate, shadow mask a field, glossy (when mapped), pane | T/G each | indirect, fill, motion, surface, the frame before's surface, histories, shadowed, lamped, specular, pane | moments, blends, fast blends, masks, penumbra tiles, means | │ |
| 16 | clamp stage: accumulate clamp, shadow tiles a field, glossy and pane clamps (`historyclamp.comp`) | T/G each | blends, fast blends, moments, masks, shadow histories | blends (in place), fast means, shadow moments and scratch | │ |
| 17 | shadow filter L0 │ L1 │ L2, both fields in one dispatch a level | T/G | scratch, history, moments, surface | history, scratch, visibility | │ each |
| 18 | wavelet ×4 (`atrous.comp`, level 0 `ATROUS_WIDE`; the last `atrouscompose.comp` where nothing sums) | T/G each | blends, surface, moments; the last level also the shadows, specular, pane and albedos | ping-pong narrow, level 0 histories; direct, composed | │ each |
| 19 | composite (`composite.comp`), only where nothing filtered or a sum is kept | T/G | direct, indirect, fill, 4 albedos, shadows, specular, pane | direct, sum | │ |
| 20 | digest (`digest.comp`), harness read-back only | T/16², two runs of 16 float and 8 word slots | 19 channels, 20 denoiser images | digest lanes | │ |
| 21 | FSR: clears │ inputs │ luma pyramid │ change pyramid │ change │ reactivity │ instability │ accumulate | T/8², SPD, (T/2)/8², O/8² | colour, surface, motion, masks, histories | FSR transients, history, output (O) | │ each |
| 22 | puffs composite (`spritecomposite.rgen`) | T.x × T.y | surface, backdrop, puffs, sprite list, fog | shown (in place) | │ |
| 23 | bloom down ×≤6 │ bloom up ×≤5 | (O/2ᵏ)/8² | shown, exposure (last frame) | bloom levels | │ each |
| 24 | histogram │ reduce (`histogram.comp`, `exposure.comp`) | O/16², 1 group | shown | bins → exposure | │ │ |
| 25 | sun glare ease (`sunglare.comp`) | 1 group | glare counts | glare share | │ |
| 26 | tone (`tone.comp`) | O/8² | shown, bloom, exposure, glare, backdrop, surface, lift, stars, blue noise, sprite list | target (rgba8) | │ |
| 27 | debug lines, when debug is on | draw | surface | target | │ |
| 28 | read-back / stress hold, harness only | copy / 1 group | target / counts | host buffer | — |
| 29 | GUI (`gui.vert`/`gui.frag`), its own submit | draws | GUI textures, picture | shown | │ |
| 30 | present: clear when letterboxed, then blit, its own submit | transfer | shown | swapchain image | — |
