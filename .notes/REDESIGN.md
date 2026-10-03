# Structural redesign: the open work

This plan answers `.notes/REVIEW.md`. It does not repeat the findings. It names the structural
causes behind them, gives the target shape for each, and orders the work. A reference such as
*(REVIEW: Frame-to-frame state)* points at a group heading in `REVIEW.md`. The findings that give a
wrong or missing result for an input the tree can produce are in `.notes/ISSUES.md`.

This file holds only what is still to do. Done work is in the code and the commits, and leaves
this file when it lands. The section numbers are the plan's first ones, which the commits and the
notes cite.

| Workstream | What is left | Phase |
|---|---|---|
| W8 The denoisers share one surface test | step 3 after its probe (steps 1 and 2 measured and declined) | 5 |
| W9 Passes run only over what is new | the last wavelet level into the composite, measured first | 5 |
| W14 The ray tracer shows what the rasterizer shows | the items that wait for an input outside the tree | after its inputs |
| W15 Groundcover stands in the ring | the measurement again with a real mod | 8 |
| W16 A mask's soft texels are layers to the eye | the layer's light from its leaf, a design to make; the first shape failed | 8 |
| W19 The walk visits what can change | the drift between legs, cause still to find | after Phase 5 |
| §16 Smaller workstreams | the upstream diff, the device, layering, tooling | 6 |
| §17 Fixes in place | the local groups; the frame constants | 5, 7 |

**Waiting for you:** the keys' clock reads the session's own `timescale` (`sky.lua` takes it at
`onInit` and `onLoad`), and no window ran it. A run under stub modules gave the expected steps.
Press the clock keys once in a played `view` to confirm.

---

## 0. Principles the redesign keeps

These come from `AGENTS.md` and the owner's posture. Each workstream below applies one or more.

- **P1. One source for each truth, inside the fork.** A value that two owners keep is a value that
  two owners can disagree on. The redesign deletes the second copy. It does not add a check that
  holds two copies equal, unless the two copies are in two languages and a shared function is not
  possible. Across the line to upstream code, P9 decides.
- **P2. A record says what happened.** An owner never infers a fact about the frame from a side
  effect (a zeroed camera, a list that a placement did not clear, a stamp on another object). The
  frame tells each owner what happened, once.
- **P3. Ownership follows use.** An owner hands out the object a caller uses. It does not hand out
  one forward per method. A struct that crosses a layer carries what the reader reads and nothing
  more.
- **P4. Work is incremental.** A table changes by the row and grows by the block. No frame pays for
  a whole table because one row crossed a threshold.
- **P5. A measurement does not measure itself.** The harness does its own work outside the frames
  that it measures.
- **P6. Measure before a shader decision.** Each kernel change has a figure before and after it
  (`./omw release bench`, `./omw kernels`), and each picture change names the pictures it moves
  (`./omw shot --against`).
- **P7. A verdict is tested before it judges.** The harness's comparisons and its measuring window
  decide whether every later change passes. They have their own tests first.
- **P8. The rasterizer is the reference for what is drawn and when.** The ray tracer may light a
  thing differently, which is its purpose. It does not drop, freeze, misplace or misread what the game
  asks for. Every fact the loader states is carried or refused by name. Every setting, script call and
  console command the rasterizer honours is honoured, or the seam says it is not.
- **P9. Upstream code changes only where it must.** A change to an upstream file is a bug fix, or a
  hook the ray tracer cannot work without, and `AGENTS.md`'s Accepted diff names it. A rule the ray
  tracer shares with the game is a copy in fork code that names the upstream rule it follows, held
  to it by a test where one can reach both; upstream code is not moved, renamed or tidied to share
  it.

---

## 7. W6 — Scene tables change by the row

**Done, and its last two rows measured and declined** (2026-10-03). The refit rota and the running
totals are in. What was left would not pay:

- **Block growth for the per-frame tables.** Over `island-crossing` the tables grew 212 times, every
  one before the world stood whole and none in the measured flight; the slowest made its buffer in
  0.97 ms (the top level's storage, 32 MiB) and all of them together took 3.8 ms. Tables on blocks
  would put an address table between the trace and every instance and material row it reads, on
  every ray, to spare a growth a session sees a handful of times, at load.
- **Presence rows in a `SlotTable`.** The placement that moves every medium and additive box moved
  at most 64 of them over the same flight: microseconds a placement.

---

## 9. W8 — The denoisers share one surface test

**Closes:** *(REVIEW: The denoisers repeat each other's per-pixel work)*, the history-loop item in
*(Shared code is written again …)*, the branch items in *(Kernels step around rules …)*.

### What is wrong

The accumulator, the glossy filter and the shadow denoiser's temporal pass each run
`heldSurfaceMatches` on the same four taps with the same inputs. Each pixel pays the surface and
motion loads and the four tests three times. The bilinear history loop is written four times. The
two surface histories re-encode a G-buffer channel into `RGBA16F` at every pixel, and that
re-encoding is the only reason `mDistanceScale` exists.

### Target shape, in three steps

1. **One function** in `lib/surfacematch.glsl` returns the four corner weights (`vec4`: zero
   outside, unmatched or at zero share) from the four loaded surface texels. Each pass dots them
   with its own history fetches. The function takes texels, not an image, because a
   format-qualified image is a type of its own. *Refactor: no picture moves.*
2. **The accumulator writes the match** into an `R8UI` image (four bits). The shadow denoiser's
   temporal pass reads it in place of `heldSurface`. The glossy filter reads it too. The nearest-tap
   test reads its bit from the same mask. *Refactor: no picture moves. Saves two of three tests per
   pixel.*
3. **Decide the surface history by a measurement.** The proposal in REVIEW keeps two of each
   surface channel in the G-buffer (this frame's and last frame's) and binds last frame's as the
   history. That deletes two full-frame writes, two image pairs and `mDistanceScale`.
   `accumulate.h` argues for a format of the pass's own. Both arguments are about precision. Write
   a probe that compares the match rate of both encodings over `one-cell-walk`, and choose by it.

**Steps 1 and 2 were measured and declined** (2026-10-03, the branch `w8-held-taps`): the
accumulator wrote its four bits into an image and the shadow denoiser and the glossy filter read
them, pictures unmoved. `./omw release bench`, three legs each, clock-normalised medians:
`accumulate` 0.205 → 0.210 ms and `shadow` 0.243 → 0.235 ms at `seyda-neen-ship`, 0.205 → 0.211
and 0.286 → 0.277 at the dawn deck; the frame's p99 and worst moved both ways within the legs'
spread. The test the two passes stopped making cost what the accumulator's extra write and the
barrier before them cost; the glossy filter, the third asker, runs on no vanilla frame. Step 1
without step 2 only moves the four loads into each pass. Step 3 stays, behind its probe: what it
would delete is two full-frame writes, which is more than step 2 had to win.

The merge of the glossy filter into the accumulator (one dispatch) is not in this plan:
`DenoisePasses::record` records the shadow denoiser between them. Step 2 takes most of the gain
without changing the order.

The five passes that branch per pixel on "has a surface" either select (as `specular.comp` and
`pane.comp` do) or name the measurement where the branch lands.

### Verification

`./omw noise` (the frame's noise and its bias against a converged reference), `./omw noise
--strafe=150` and `--walk=150`, `./omw shot --against`, `./omw release bench`. Steps 1 and 2 must
not move a denoised picture beyond `sDenoiserNoiseLevels`.

---

## 10. W9 — Passes run only over what is new

**Closes:** *(REVIEW: Passes run over memory or tiles that hold nothing new)*.

| Pass | Change | Expected gain |
|---|---|---|
| the last wavelet level into the composite | measure first | a frame-sized round trip |

Each row has its own commit and its own `bench` figure. A change that shows no gain on a hot card
does not go in.

**Declined on their figures** (2026-10-03, three legs each): `spriteruns.comp` leaving a workgroup
none of whose tiles any sprite reaches — the `sprites` zone stood at 0.047 ms at
`balmora-storm-night` either way; and the mask pass packing a receiver word a tile for the
classification (the branch `w9-receiver-words`) — `shadow` 0.228 → 0.229 ms at `seyda-neen-ship`
and 0.237 → 0.239 ms at `balmora-mages-guild`, the surface read it added to the mask pass costing
what the two it took off the classification saved.

**Measured first and not built**: one dispatch for the skinning and morphing, whose whole `skin`
zone is 0.018 to 0.028 ms at every place benched, and the histogram on the bloom's first halving,
whose `exposure` zone is 0.023 to 0.028 ms — each could win at most its zone, under what a leg's
spread resolves. **Already so, and now held by a test**: a sprite the shelter zeroed has no radius,
which `capsuleSpan` gives an empty arc, so it was in no tile; and the medium and additive walks run
only in tiles a presence of a shown class marks, each crossing's class tested against the ray mask.

---

## 15. W14 — The ray tracer shows what the rasterizer shows

**Closes:** what is left of *(Content the rasterizer draws reaches the ray tracer with no reader)*.

- **A generated particle image** enters the table under a key made from its address, as the
  composites do; the material reader needs the same key.
- **A distant static's texture animation**: a prepared part whose chain carries an AutoPlay
  state-set updater gets one animated material per model, applied once a frame through
  `MaterialResolver::animate`.
- **The cut reads the diffuse alpha alone**, where `objects.frag` tests `alpha × dark.a`. Reading
  the dark map in `candidateStops` cost the dawn deck's trace 3% (2.59 against 2.50 ms,
  clock-normalised, four legs each), behind a material bit or not, because the code sits in every
  shadow ray's candidate loop. Count the content the difference moves before deciding.

Each carries a test with hand-computed values, on the GPU where the fact is a picture, and a
`./omw shot --against` that moves the pictures of its own content and nothing else.

---

## 15a. W15 — Groundcover stands in the ring

**Closes:** the groundcover item of *(Content the rasterizer draws reaches the ray tracer with no
reader)*. The ring reads and stands it (`Rtx::GroundcoverSource`, `MWRender::TracedGroundcover`);
what is left is below.

**The acceptance measurement, again with a real mod.** Taken on 2026-10-03 with a generated
plugin (60 593 plants over 49 cells, five vanilla flora models, about 1 240 plants a cell),
`./omw release bench`, the trace zone's median normalised to the clock, four legs with grass
off against two with it on: `seyda-neen-ship` +13.0%, `seyda-neen-pond` +15.1%,
`seyda-neen-ship-dawn` +8.6% (two legs off); `tlas` +0.05 to 0.07 ms. Under the fifth, so an
instance a plant stays. **The cost is the geometry and not the cut**: the same plants opaque
cost +7.2% and +13.5%. The legs with grass off spread 3.00 to 3.64 ms at the pond, as wide as
the threshold, so a real mod's density on a quiet desktop decides again before the merge is
ruled out; the merge per cell and model, built on the reader thread, is what goes in if it
crosses.

---

## 15b. W16 — A mask's soft texels are layers to the eye

**Closes:** *An alpha-blended surface … is cut at alpha 0.5* (`ISSUES.md`).

### What three trials showed

- **Every soft texel a pane** (2026-10-02): at `seyda-neen-pier` the panes went from 3 to 91, and
  52 of 60 pictures turned to grain. The grain was the light: every sun and sky ray through a pane
  is attenuated by it, so every surface under foliage had a noisy shadow.
- **A hashed alpha test for every ray** (the second trial): the fringe's error against the converged
  blend fell at the pier (8.55 → 2.37) and rose under the canopy at `seyda-neen-pond` (21.57 →
  23.67, noise mean 1.19 → 1.90, p99 10 → 24). A texel met in one frame and missed in the next is
  a different surface to the eye each frame, so the accumulator dropped its history there.
- **A soft texel a layer to the eye alone, shaded as a pane** (2026-10-03, the target shape below
  as first written; the branch `w16-soft-layers` has it): the sun's rays kept the cut, and the
  picture still broke. Under the canopy each fringe texel was a pane shaded by `shadePane` — one
  unfiltered sun ray, one ambient ray that reads the sky blue, into the pane channel, whose filter
  is guided by the nearest layer, which changes from pixel to pixel through foliage — so the leaves
  came out speckled blue with bright contours. `noise --strafe=150`: the pond's frame noise 1.19 →
  1.95 (p99 10 → 29), its bias 1.76 → 3.67; the ship's noise 1.59 → 2.52 (p99 21 → 35), bias
  1.82 → 3.55; the pier unchanged. The pond's trace zone over the noise run's frames 2.61 → 3.48
  ms, +33%.

The first two failed by what the coverage did to rays other than the one the rasterizer blends for,
and by making the eye's surface change between frames. The third kept the light rays whole and
failed by the light it gave the layer itself: a fringe is not a pane, and shading it as one is
what broke it. The rasterizer blends a soft texel into the picture and nothing else: its shadow
casters are alpha tested at a half (`shadowcasting.frag`, "this replaces alpha blending").

### Target shape

**The layer's light is the leaf's, and not shaded again.** What the third trial shows is that a
fringe texel wants the light its own leaf already has, not a path of its own: the same sun, the
same filtered shadow and the same bounce as the solid texels a pixel away. So the soft texel is a
layer to the eye ray, as the trial had it (`MATERIAL_SOFT_MASK`, the `peeling` literal through
`candidateStops`, `cutAt`, and `resolveFor`, all on `w16-soft-layers`), and what it is lit by is
the solid channels' light at the layer's own pixel, demodulated by the leaf's albedo — no shadow
ray, no ambient ray, nothing in the pane channel. Where the pixel's solid surface is not the same
leaf, the nearest pixel that is lends it. That lookup is the design still to make, and it is
measured as the trial was.

### Measurement before it goes in

Release, hot card, back to back: the trace zone at `seyda-neen-pond`, `seyda-neen-pier` and
`seyda-neen-ship`, before and after; `noise --strafe=150` at the same three; the fringe's error
against 1 000-frame references taken under the rasterizer's rule. **Accepted** where the fringe's
error falls at all three, the frame's noise does not rise, and the trace zone grows by less than a
tenth.

---

## 15e. W19 — The walk visits what can change

**The frozen subtrees are in** (2026-10-03): the world walk passes a reference root that changes
nothing on its own, held through every sweep. The walk's median halved — 1.13 to 0.54 ms at
`one-cell-walk`, 1.32 to 0.58 ms at `seyda-neen-ship`, six legs each — and its p99 with it.

**What is left is the drift it was also meant to close**, *A measured run's host rows move as a
whole between runs of one build* (`ISSUES.md`): over the same six legs the walk's medians still
spread 0.41 to 0.73 ms and the frame thread's cache misses 5.4 to 16.9 a thousand instructions,
the spread they had before in proportion. The pointer chase the frozen roots took away was not
its cause, or not the whole of it; what moves a leg as a whole is still to be found.

---

## 16. Smaller workstreams

### 16.1 The upstream diff

1. **Debug-only checks on hot paths.** Each `Crash::notNull` on a per-node or per-frame path becomes
   the debug-only form: `NodeCallback::run`, the light manager, the terrain drawable, the MyGUI
   batch, the physics check that replaced upstream's `assert`, and the terrain's water culling
   view (`quadtreeworld.cpp:447`, once per chunk per cull). The five extra warnings stay on for the
   whole tree, and the rules require this whatever their scope.
2. **The SDL3 port's defects:** `androidmain.cpp` back to upstream's, and upstream's
   `GraphicsWindowSDL2` name back. Both take fork lines out of upstream files.
3. **The docs that say false:** `rtx.rst` and `settings-default.cfg` name a `-DOPENMW_RTX` the
   build does not have.
4. **The harness's hooks in upstream classes.** The engine's two frame hooks are the host interface
   (`OMW::EngineHost`) and go into Accepted diff with that reason. The weather hold
   (`World::holdWeather`) and the script boxes (`WindowManager::scriptMessageBox`) leave upstream's
   classes: the host sets the weather through `World::changeWeather` with its own transition, and
   declines script boxes in its own window-manager setup.
5. **The post-processing package tests the renderer once.** `initPostprocessingPackage` registers
   upstream's usertype where there is a chain and an inert one where there is none, so upstream's
   bindings come back unchanged and the null tests go.

### 16.2 Device requirements

*(REVIEW: The device check and the enabled extensions differ …)*

- A subgroup obstacle in `profileOf`: quad operations in the compute stage.
- `Device` enables an option's extensions only after its feature holds, in table order.
- `rayTracingMaintenance1` leaves the requirements, or names its reader.
- **The push-constant limit is a requirement.** One `sPushConstantBytes = 256` beside
  `sApiVersion`; `profileOf` refuses a device below it by name, and `pushRangeOf` asserts every
  constants block against it (the tone pass pushes 200 bytes, the sprite bin 184).

### 16.5 Layering guards

*(REVIEW: The layering's documents and guards lag the code)*

`RtxSourceTreeTest` reads the folder order from `architecture.md`'s table, so the order has one
statement. Move `shaders/` to the top of that table. The test refuses a quoted include with a `..`
component, and refuses `<apps/...>` under `components/rtx` and `components/rtxvulkan`. Then fix the
quoted `"../"` includes in the fork's own `mwrender` files (never upstream's), the two in test
support, and the `#ifdef _WIN32` in `glrenderer.cpp`. The core's prose names no Vulkan object either
(swapchains, descriptor sets, command buffers, `VkInstance`): it states each cost in its own terms,
and the backend's comment carries the Vulkan reason.

### 16.7 Tooling single sources

*(REVIEW: The driver, CI and CMake restate facts that each other hold)*

The presets are the one source for Qt flavours and dependency roots (`$env{OMW_VCPKG_ROOT}`,
`$env{OMW_QT_ROOT}`). CMake writes the shader and digest tool locations into the cache, and
`kernels` reads them. The digest tool lists `SpecId`s, and the Python SPIR-V reader goes. One
`bench_line` helper serves `repeat` and `profile`. `gate` calls the build verb's body. In CI: one
ccache input on `openmw-deps`, one reusable `package.yml`, one format check (`omw format --check`
in `checks`), `GTEST_FAIL_IF_NO_TEST_SELECTED` in the test preset, `.python-version`. The driver's
usage formats the harness's verbs from `HARNESS_VERBS`. The benchmarks carry a `benchmark` label
that both CI jobs run. One Vulkan headers pin serves all three systems. The release checks for a
successful CI run on its commit.

---

## 17. Fixes in place

These groups need no design. Fix each item where it stands, in the commit that touches the file, or
in one sweep per group.

| REVIEW group | Rule |
|---|---|
| Test-only code ships in the game | `MipChain`'s builder and `OwnedTexture` move to test support beside `SpriteLightBake`; `SpecularAlbedo::at` and the `describeImage` overload go |
| Docs and comments state what the code does not do | correct or delete each one |
| Frame constants are derived on every lane … | the host writes each into `VisibilityConstants` once |
| Kernels step around rules … | `RTX_STORE_COUNTED` for `puffs` and `backdrop`; one path or a named measurement for each branch |
| Parameters and fields carry nothing … | delete the dead fields and parameters; `PathSeeds` for the three seeds |
| Arrival frames allocate or churn … | one refusal per model per cell; report a refusal when the count moves; one owner per path string |
| Canaries that no report reads | the stop report prints the `NodeKinds` overflow and `mWornBeyondKept` |
| Includes that name nothing they use | delete them |
| Smaller defects | each as written |
| Options a verb takes and does nothing with | an `sPictures` verb set owns the picture-only knobs; every contradictory pair is refused at parse time, and `--accumulate` with an upscaler is refused |

**Tests** follow each workstream. The groups *(Tests that cannot fail …)*, *(Behaviour with no
test)* and *(Test fixtures are duplicated …)* close as each owner is rewritten, with these that
stand alone and go first because they cost every run of the suite:

- the blue-noise spectrum test: a separable transform with a twiddle table, about a thousandth of
  the work;
- the unsettled ring test: a `ContentSource` that blocks until released, and exact counts per walk;
- the renderer and `FrameClock` wall-clock bounds: an injected clock (`FrameRateLimiter::limit`
  already takes `now`);
- `RtxMonitorTest`'s quarter-second sleep: count the predicate's calls and release the turn after
  the first;
- `RtxInstanceTest`, 15.9 s under ThreadSanitizer: it moves to `rtx-gpu-tests`, or runs on the
  harness's unvalidated instance;
- the card watch's test: assert on the closed window's readings, not on the time between two calls.

The second review adds tests where behaviour has none: `RenderManager`'s batching over a
`CountingRenderer`; allocation legs for `drawGui`, `traceGuiTexture`, `collectDrawCalls`, the
present, the game side's frame and a second `CellReader::read`; `HeldSubmit`'s opener thread adopted
so its validation errors are reported.

---

## 18. Order of work

Each phase ends green on `./omw gate`.

1. **Phase 5, frame cost:** W9's last row and §17's frame constants. Each with a `./omw release bench` before and after. A change that does
   not improve the worst frame or the p99 does not go in. W8 step 3 follows its own measurement.
2. **Phase 6, the upstream diff:** §16.1, then §16.2, §16.5 and §16.7.
3. **Phase 7, tests and docs:** the test groups in §17, and every doc item, `architecture.md` §1 and
   §13 included.
4. **Phase 8, the open issues:** W15's measurement with a real mod, W16's lit layer behind its measurement.

W14 goes as each input arrives.

---

## 19. Verification for every phase

- Build the touched targets and run the covering test binary with a filter
  (`./omw test <binary> --gtest_filter=...`).
- `./omw test` once before a phase is called done. `rtx-gpu-tests` fails without a device, so a
  green run means a device ran it.
- A change to anything a frame reads: `./omw repeat --pairs=10`.
- A shader change: `./omw kernels --against=<before>`, then `./omw shot --against=<baseline>`. Time a
  suite on its second run, because the first includes the driver's compile.
- A frame-cost change: `./omw release bench`, hot card, back to back, warm-up leg first, started in
  the background. Quote the median, the p99 and the worst frame.
- A denoiser change: `./omw noise`, with `--strafe=150` and `--walk=150`.
- The end of each phase: `./omw gate`, alone, with no build beside it.
- `REVIEW.md`: delete each item as it closes. A workstream is done when its groups are empty.
