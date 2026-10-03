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
| W6 Scene tables change by the row | block growth, running totals, the refit, the presence rows | 5 |
| W8 The denoisers share one surface test | steps 1 and 2; step 3 after its probe | 5 |
| W9 Passes run only over what is new | every row, each with its bench | 5 |
| W14 The ray tracer shows what the rasterizer shows | the items that wait for an input outside the tree | after its inputs |
| §16 Smaller workstreams | the upstream diff, the device, layering, Vulkan, tooling | 5 (§16.6), 6 |
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

**Closes:** *(REVIEW: Scene tables redo work for rows that did not change)* and *(Work is batched
behind a threshold …)*.

### What is wrong

| Where | Defect |
|---|---|
| `GrowableBuffer::outgrow`, `SlotTable::sync` | the frame that crosses a power of two remakes the table and writes every row |
| `SceneAcceleration::prepareRefit` | a posed body is rebuilt only every 64 placements (`sRebuildEvery`), so frames alternate between a rebuild and none |
| `readPlacedStats` | the texture and structure figures are loops, so they are left out of the per-placement stats and go stale; `extendScene` pays a 4096-slot walk on the frame path |
| `SceneBuffers::place` | every medium and additive box is transformed on every placement, standing rows included |

### Target shape

- **Per-frame device tables grow by the block.** `SlotTable`, the skin tables and the sprite bin
  move onto `BlockedBuffer`'s shape: fixed blocks behind one address table. Growth costs one block
  and owes only its new rows. `SlotTable::mRows` reserves by the block too. `GrowableBuffer` stays
  only for start-up and resize paths, and `outgrow` goes.
- **The refit rebuilds the oldest posed mesh on every placement that poses anything.** No threshold.
  The worst frame stays one rebuild, and every frame costs the same.
- **Running totals.** `TextureArray` keeps count, bytes and reduced count, updated in `stand` and
  `drop`. `StructureStorage` keeps its two totals as rooms are taken and given back. One
  `readStats` runs at every placement, and `readPlacedStats` goes.
- **Presence rows in a `SlotTable`**, driven by the same `changed` list as the instance table.

### Verification

`./omw repeat --pairs=10`. `./omw release bench` on a hot card, back to back, with a warm-up leg:
`one-cell-walk` and a crowd view for the movers, `island-crossing` for growth. Report the median,
the p99 and the worst frame before and after. The worst frame must not rise. The frame thread's
cache misses a thousand instructions (the report's `host thread` line) must stay within a tenth of
each other across six legs: the walk's spread between legs follows them, and a table read as
arrays is what stops it depending on the heap's layout.

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
| ocean transform (`waveform`, `waveline` ×6, `wavecompose`) | two dispatches and one barrier: the row pass computes the spectrum of its row for all three pairs in shared memory and transforms; the column pass transforms and stores into the tiles | about 36 MiB less traffic a frame, four queue drains fewer; the 128 cascade stops running 256 threads with 64 working |
| `spriteruns.comp` | count non-empty tiles of the workgroup into shared memory before the first barrier; return uniformly at zero | scales with occupied tiles, not all tiles |
| `shadowfilter.comp` | `APRON = 1 << SHADOW_LEVEL` | 61% and 44% fewer loads at levels 0 and 1 |
| `shadowtiles.comp` | the mask pass writes a receiver word per tile; the classification reads words | two full-frame reads off the cleared tiles |
| skinning and morphing | measure first: one dispatch over a table of rows in place of one per mesh | hundreds of tiny dispatches become one, if the command processor shows the cost |
| histogram on the bloom's first halving; last wavelet level into the composite | measure first | up to two frame-sized round trips |
| `spriterects.comp` | a sprite the shelter zeroed gets `noTiles()` | sheltered rain costs nothing after the shelter launch |
| the medium and additive walks | run only where the view casts against their classes (`describeView`); additive placements keep their class bit | a map tile stops paying three traversals a pixel (and stops drawing absent actors' spell sheets) |
| the upscaler's clears | one `Barriers` into `TRANSFER_DST` for all of them, one back merged into `between()` | two barrier commands a frame instead of up to eleven |
| `barrierBeforeBuild` | deleted; its comment moves to `recordRefit`'s `barrierAfterBuild` | one drain fewer per moving frame |

Each row has its own commit and its own `bench` figure. A change that shows no gain on a hot card
does not go in.

---

## 15. W14 — The ray tracer shows what the rasterizer shows

**Closes:** what is left of *(Content the rasterizer draws reaches the ray tracer with no reader)*.

- **Groundcover is one merged mesh per cell**, as the rasterizer's chunks merge it: the ring's
  reader builds it from `GroundcoverStore` under `rendering distance` and `density`, with a copy of
  the density rule that names `Groundcover`'s `DensityCalculator`, which stays where upstream keeps
  it (P9). One instance per cell keeps the top-level structure's row count what it is today, where
  an instance per plant would add about a hundred thousand rows. Stomp is a later deformer. **The
  content to check it against is made by the tree**: a test plugin written with `ESM::ESMWriter`,
  which declares vanilla `flora_*` statics as groundcover in two cells near Seyda Neen at known
  positions, loaded through `groundcover=` as a player's `openmw.cfg` does. The reader is checked
  against it under both renderers (`shot` of one view, and the count of placed plants against
  `GroundcoverStore`'s at the same density), and against a real mod where one is installed.
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

### 16.6 Vulkan lifetimes and barriers

*(REVIEW: Vulkan objects are waited on or rebuilt by a side effect …)*

Staging blocks retire through `Retiring<std::size_t>` like every other retired object. The
presenter compares against the extent it was last asked for. The two GUI `finish` calls before a
present-mode change go, or state the reason that remains. A trace pipeline reads each named module
once per build and chains the words (`maintenance5`), and the GUI pass's four pipelines load their
two modules once.

The present target states its resting use once (`PresentTarget::sResting`), and every user
transitions from it and back; the five literals go. `Barriers` asserts in debug that nothing is
pending when it goes out of scope.

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
| Where an image was left … ; Barriers that order nothing … | §16.6 |
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

1. **Phase 5, frame cost:** W6, W8 steps 1 and 2, W9, the barrier rows of §16.6, and §17's frame
   constants. Each with a `./omw release bench` before and after. A change that does not improve
   the worst frame or the p99 does not go in. W8 step 3 follows its own measurement.
2. **Phase 6, the upstream diff:** §16.1, then §16.2, §16.5 and §16.7.
3. **Phase 7, tests and docs:** the test groups in §17, and every doc item, `architecture.md` §1 and
   §13 included.

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
