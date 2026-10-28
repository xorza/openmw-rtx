# Review: the ray tracer, the whole diff against upstream

Base: upstream `4e718b500f`. Scope: `components/rtx`, `components/rtxvulkan`, `components/myguirtx`,
`apps/openmw/mwrender`, the fork's hunks in other upstream code, `apps/rtxtool`, `tools/omw`, and
the tests of all of them. Eleven reviewers read the code. Nothing was built for the review. The
test run below is the debug build that existed.

Two reviewer claims were checked by hand. The reciprocal fold claim: a C program at
`-ffp-contract=off` gives `y * (1/y) != 1` for 23 integers under 200, the first one 41. The caustic
claim: `caustic` in `lib/sea.glsl` adds the cascade curvature without a turn.

## Proposal

### Root causes and the shape that removes each

- **The world frame has no owner, and its description is written in the backend.**
  `VulkanRenderer` composes the frame block (`constantsFor`, `describeWorld`, the `FramePast` fold,
  `sampleFrame`, the picture-light overlay) and holds `FogDrift`, `FramePast` and the previous
  camera. It also carries the world's chain, arena, upscaler and shown radiance loose. The same
  shape for pictures is packaged as `PictureTracer`. Target:
  - `Rtx::FrameDescriber` in the core owns the drift, the past and the previous camera. It turns a
    `FrameRequest` into the sampled constants, `WorldOptions`, `Reconstruction` and `FramePast`, and
    a picture's request into its own. A core test covers the composition without a device. The air
    clock gets one owner, so `holdAir` is one call.
  - `FrameTracer` in the backend is `PictureTracer`'s twin. It owns the chain, the arena, its
    declarations, the upscaler and the shown radiance, with the same `bytesAt`, `resize`, `release`
    and `record`. `createTargets` becomes reserve, world rebuild and `tracer.resize`.
  - Every medium is stepped by the owner before a chain records (`stepRipples` and a new
    `synthesiseSea`). The chain only reads media, and `TraceMedia`'s `const` says something true.
  - Parts are handed their siblings at the call (`PictureTracer::trace`, `TraceChain::record`,
    `SpriteBin`), so member order stops being a correctness rule.

- **`Rtx::Renderer` is one 40-method interface that tests reach through.** Split it by consumer, as
  `GuiRenderer` already is: a scene holder (the six methods `SceneUploader` calls), a picture
  interface (the five `OffscreenTrace` calls), and the GUI half. Test reads move to the concrete
  backend's test probe, never onto the shared contract.

- **Change detection in the mirror samples a face, not every source of change.** A frozen root is
  compared by `Face`, which sees only what the walk visited. A node that the view mask rejects can
  still carry a controller. Target: the walk's child loops record the update state of the children
  the mask rejects, and `Face` carries the identity the walk computed. A frozen root keeps its
  discrete state (lights, switch values) as records and replays it on a pass. A lamp or a plant then
  freezes like every other root.

- **Deferred destruction is a convention per type.** `Owned` puts "everything a submit may read ends
  through the graveyard" into the type for handles. `DeviceMemory` frees at once, and four owners
  bury it by hand. Target: `DeviceMemory` buries itself, one handle template with an end policy
  replaces `Owned`, `Immediate`, `Root` and `Messenger`, `AccelerationStructure` uses volk's
  destroyer, and owners default their moves. A `Batch` owns its read stamp and applies it at
  hand-over, as `keep` already does for burials.

- **Renderer-wide scratch is owned per scene, and transient memory is kept at its peak.** Each
  `DeviceScene` owns a `TextureArrival` whose rooms grow to its largest group and stay. The staging
  ring keeps the size of the largest arrival until the next world load. Target: one `TextureArrival`
  beside `TexturePasses`, handed to `TextureArray::write` at the call. The ring trims after an
  arrival retires.

- **Byte figures are hand-picked from members.** `SceneStats` misses about 120–150 MiB, the bench
  record writes no memory, and `MemoryReport` is Vulkan's heap model in the core. Target: each owner
  answers `getBytes()` over all it allocates, the core reports by use and by three pools (video,
  system, host-written aperture), and the record writes it.

- **The image facts walk the texels several ways.** The mean, the alpha, the shading estimate and the
  mip oracle each walk the finest level. Laid images are widened two or three times. Target: one
  per-block walker in `image/` that yields alpha and a linear palette per block. `layImage` widens
  into the image it returns, and the texture passes read the laid image. One rule: a name identifies
  its texels.

- **Field-stored images meet code that assumes D3D's out-of-bounds reads.** The FSR callbacks map an
  off-frame coordinate onto a real pixel. Target: every read of a field-stored channel at a
  coordinate the kernel did not make goes through one guarded load that answers "nothing" off the
  frame.

- **Quantities are combined across frames of reference.** The caustic adds a tile-frame Hessian to a
  world-frame one. Target: each sea quantity carries its frame, with one turn helper for a vector and
  one for a symmetric tensor.

- **GLSL rules that cannot take an image are written again per kernel.** The split blend, the running
  mean gather and the box statistics are each spelt twice and drift. Target: one macro or helper per
  rule, in the style of `RTX_HISTORY_SHARES`, with kernels supplying only their loads.

- **"Did it happen" is not returned.** `OffscreenTrace::traceInto` returns nothing, and `TracedView`
  infers a recorded copy. The run hook is gated on a timing optional. Target: each step returns
  whether it did its work, and the state follows that.

- **The rule that pairs a frame's figures lives in two places in the harness.** `MeasureWindow`
  pairs samples and arrivals. `Measurer` pairs crossings itself and reads the world inline. Target:
  every per-frame figure goes through `MeasureWindow` as plain input.

- **Hand-kept numbers beside facts the code states.** `sMaxGpuZones = 40` against 37 zones plus the
  six its comment adds. `sMaxHeaps` restates `VK_MAX_MEMORY_HEAPS`. `STREAM_TURN` repeats turns and
  keeps a channel nothing reads. Target: derive each bound from what it bounds, and check each table
  for what its comment promises.

- **The test harness leaves state to each fixture, and tests sit by history, not by unit.** Target:
  `RendererTest::SetUp` resets every setting a test can move, one helper makes an unlit camera, the
  production passes take their sizes so a test drives them, and test files mirror the folders they
  test.

### Observations without a change

- No game code branches on which renderer it has. The kind is chosen once (`engine.cpp:700`). Every
  other question goes through `support()`, `groundReadsGates()`, `isAbandoned()` or the two
  documented null escapes. No Vulkan header reaches the core.
- Every hunk in upstream code outside `mwrender` fits a reason `AGENTS.md` allows, and every
  Accepted-diff entry still matches its code. The exception is two cosmetic reorders in `mwrender`
  (batch 12).
- Pass order and barriers held wherever a reviewer traced them. BLAS/TLAS build, refit, the rebuild
  rota and compaction are correct. The record paths allocate nothing per frame after warm-up. No
  workgroup returns before a `barrier()` non-uniformly. Every shared struct is scalar-packed with a
  size check.
- Math checked sound: the bounded VNDF, Schlick and split-sum, RIS, `falloffAlong`, the fog phase
  constants, RGB9E5, the FFT, the sky fits, the wave spectrum, the harness's nearest-rank quantiles.
- Test run, debug, 2026-10-28: `components` 2543 tests in 17.9 s, `openmw` 625 in 1.5 s,
  `crash.matrix` 9.7 s, the GPU binary 464 tests in two shards of 62.9 s and 58.0 s.
  `RtxVisibilityTest` takes 56 s of the 121 s the shards sum. 56 tests take over 1 s, the slowest
  2.15 s (`RtxBounceTrailTest.theClampShortensTheSkysTrail`). Two `RtxPresenterTest` cases failed
  with "No available video device" (`presenter.cpp:67`), because the session's shell had no
  display.

## Plan

Each batch leaves the build and the tests green. Whoever addresses an item deletes it.

## Changed

- 1, `bluenoise.glsl` `STREAM_TURN`: channel 11 removed and the count made 12. The bounce and the
  sun's disc keep the one `R2` turn: the comment above the table records that the sun's pair under
  its own turn left the frame noisier (`./omw noise`, mean 0.72 and p99 2.03 against 0.71 and 2.00).
- 2, `Face` and the masked controllers: a root freezes only where its count of children asking
  for the update traversal is nought, and the child loops do not test the mask. A frozen root then
  has no controller anywhere, so any one hung later moves the count, the case under a child whose
  count is already 1 included. Cull callbacks are not counted: OpenSceneGraph keeps no count of
  them, and the one the game hangs (`Animation::setAlpha`) goes on an actor, whose skeleton keeps
  it walked. `Face` carries the walk's identity.
- 3, the entry points' phases: the game-side ones take `Run` beside `Between`, since the run's hook
  acts on the game (`StopWriter::writeDoll` places an object, which adds a ripple emitter); only
  the start-up ones take `Between` alone. `onDetachWorld` asserts none, as its comment says: the
  world goes on the way out of an exception a frame threw.
- 4, `sMaxGpuZones`: a second placement opens six zones and not the comment's five, `Blas` to
  `Ground`, since the ground's composites bake in the placement: 37 + 1 + 6 = 44.
- 4, `fogColumnOver`: the series beside the quotient, below a climb of half a scale height, with
  no test of its own: the error it removes is under 1.5e-4 of the falloff's mean, which no picture
  separates, and a test of one function needs a probe pass of its own.
- 5, the crossing pairing: the window closes a crossing with the span of the frame that first
  stood in the new cell (`Taken::mClosedCrossing`), as it closes every figure. The host rows still
  cover the frames from the one before the first measured to the one before the last: closing the
  last measured frame's own span needs a frame past it, which moves every stop's length and the
  film's numbering. `Measurer` still reads the player's cell for `WindowFrame` and the pause's
  cause from `Stager`: neither is in the frame's report.
- 5, `BenchSpan`: `--frames` beside a named `--seconds` is refused (`benchSpanFrom`, beside
  `filmLengthFrom`), and the struct keeps its two fields: a variant reaches 24 constructions and
  every reader of the two, for a state the one constructor from the line now refuses.
- 5, the stale outputs: `RunRecord::begin` removes every output the request names, and the shot's
  `clearPictures` is gone. The shared `out`/`against` sequence of `shot` and `bench` is left.
- 6, the refused slot: `SlotTexture::write` and `isSized` serve the painted mirror and the shared
  one, which had the same retake on every draw. `Texture::lock` still throws on a refused slot:
  a lock hands MyGUI a pointer it writes later, and a buffer standing in for a slot past the
  device's side is the hundreds of megabytes the refusal saved.
- 6, the presenter's tests: a suboptimal present is driven through `Presenter::suboptimal`, public
  as `surfaceLost` is. Out of date only sets the staleness, and a hidden surface needs a window the
  test machine minimises, so neither has a test of its own.
- 7, `fitCursors`: the three inputs compared as one `CursorFit`, and the cursor shown again by its
  name after a refit and after a name registered again. The rule has no test of its own: it is the
  struct's equality, and the window manager it sits in stands up with the whole interface.
- 8, the shared `TextureArrival`: the renderer's own beside `ScenePasses`, which every scene takes
  as `const`, handed to `DeviceScene` and `TextureArray` at the call.
- 8, the staging ring: a frame that handed nothing over lets go of one free block, the largest,
  while the ring holds more than `sStagingKept` (`CommandPool::shedStaging`), so no frame pays for
  the whole trim. The texture bytes are not written straight into lent staging.
- 8, `SceneStats`: the structures' working memory is one figure, `mWorkBytes` (the rows, their
  packing, the poses and every scratch), reported and recorded beside the tables. The shared
  arrival's rooms and the descriptor pool are the renderer's and not a scene's, and stay out.
- 8, `MemoryReport`: kept as the heaps the device states, the use beside them. Three pools would
  have to choose which one a resizable-BAR heap is, video and aperture at once, which the heap line
  states as it is. `sMaxHeaps` is held to `VK_MAX_MEMORY_HEAPS` by a `static_assert` in the backend.
- 9, `Rtx::FrameDescriber` (`renderer/`, since it reads the world's drift): the drift, the past and
  the previous camera, the request's description and a picture's, with a core test of the past's
  fold and the picture's light. The air clock keeps its two owners: the sky's clock is the game
  side's (`SkyReader`), which the backend never sees.
- 9, `FrameTracer`: the chain, the arena and its declarations, and the upscaler, with `declare`,
  `bytesAt`, `movesTo`, `release` and `resize`. The frame's recording stays in `renderFrame`, which
  reads the ring, the display and the stress pass beside it; the shown radiance is the display's
  answer and stays beside it.
- 9, the siblings held by reference: `PictureTracer` takes the media, the display and the GUI
  textures at each `trace`. `TraceChain`'s and `SpriteBin`'s passes and `DeviceScene`'s stay held:
  each is the renderer's pass table, made before every part and fixed for its life, which the
  pinned owner's order states (`AGENTS.md`, an owner that wires its parts by reference).
- 13, the callbacks classified once: by the class's `type_info`, asked once whether it is a
  `StateSetUpdater`, so an entry is made only where a chain holds one or the node stands under
  something animated. No test of its own: the resolver's entries are read by nothing outside it.
- 13, `ChainKeys::retire`: each pass sorts only what the pairs that went put into the sink, a run
  of its own, and a key is counted in every run; no holder count and no worklist. The reference
  count the unref worker moves only falls, so a retire it races waits a walk and is never early.
- 16, `attachAutoMaps`: each map at the unit past both what the caller's list and the state set
  written to hold (`nextFreeUnit`), and the doc says so. No test of its own: one needs an image
  manager over a file system with the companion files in it.
- 14, the narrow levels' reuse: the comment says what they share, step by step; the tile sized by
  the step is not built, since it waits on a measurement.
- 10, the renderer's interface: `SceneHolder` and `PictureRenderer` (a holder that traces
  pictures), which `Renderer` inherits beside `GuiRenderer`; a picture's view takes the second,
  since a picture is of a scene it hands over. The test-only members went to
  `Testing::RendererProbe`, a friend; the GUI forwards stay, since they implement `GuiRenderer`.
  The texture count the GPU test reads is `DeviceScene::getTextureCount`, read through the probe.
- 11, `DeviceMemory` buries itself through its allocator, which the device hands itself, and the
  graveyard gives a range back at once; `ImageArena` and `TextureArrival` default what they wrote.
  `Buffer` and `Image` keep their moves, which empty the husk, and declare the memory before the
  handle, so a destructor buries the handle first.
- 11, the tests: `buildMips` and the growth's burial are held; `stagingfit.cpp` stays apart from
  `commands.cpp`, since `bestFit` needs no device and `commands.cpp` runs in the binary that does.
- 12, the texel walk: a band in light turns a block's palette once (`readLightBand`), which
  `meanTexel` reads; the alpha keeps its own pass and `ShadingMap` its own sums, whose quantity is
  the stored colour and not light.
- 12, the passes' scratch: both texture passes describe into one scratch the preprocessor holds,
  which halves what a thread keeps; they still widen the source and not the laid image, since a pass
  is asked before anything is laid and its key is the source's finest level.

- 14, the digest's header: the bindings, the slots and the workgroup moved beside
  `DigestConstants`, and the denoiser's counts are `DigestPass`'s, derived from `DenoiserImage`;
  the lanes, the seeds and the fold stay in the core.
- 14, the tone curve: its grey (`toneMapGrey`) is the shared core `displayedLevel` reads, with the
  shadow ramp and the compression its two halves, which `toneMap` calls; no kernel moved.
- 14, the tests: the shared exponent is held by a probe of its own (`sharedexponent.comp`); the
  wave's turn, the lamp's falloff and the history box are not, each a probe kernel of its own.
- 15, the scheduling rules: `ViewQueue` draws through `ViewSchedule`, a template over the view,
  whose budget, ground wait, forgetting inside a flush and leaving the scene a table holds. The
  ripple emitters keep their reach into the world, since their rules are `ripplerules.hpp`'s,
  already held, and what is left is which `Ptr` a cell holds; `TracedView`'s copy states and the
  frame describer's (held in batch 9) are not tabled again.
- 18, the renderer test's start: `SetUp` puts back the upscaler, the filtering and the sea as the
  renderer was made with them; the fog field stays the one test's own to put back, since telling a
  field drains the frames and nothing compares two fields, and the picture room is never shrunk.
  The upscale tests keep their scoped modes, which also reset a mode between the cases of a loop.
- 18, the unlit camera: `Testing::unlit` is what both dark cameras call; the 77 tests that zero the
  zenith alone keep it, since each keeps the sun or the ambient it means to.

## Declined

- 12, `addCloudSheet` read on the ring's reader: the frame reads a sheet a script names once per name, a
  hitch of a few milliseconds; off the frame, the sky draws the wrong deck until the reading arrives
  (the user's call).
- 1, `spirvpin.cpp` `Pinner::fold`: the reciprocal fold is what makes a division by a constant the
  same on every device, which the pinning promises (`AGENTS.md`: a shader's float arithmetic is the
  build's). A fold for powers of two alone gives every other divisor back to each device's 2.5-ulp
  division, and that division owes `mod` no exact multiple either.
- 2, lamps and switches frozen as records: every `LightSource` carries update callbacks of its own
  (`CollectLightCallback`, the `LightController`), and every night/day switch a `DayNightCallback`.
  Freezing them means exempting callbacks by class, where the root keeps only a count, so a
  controller hung later on the same chain would move nothing the face reads: the blind spot the
  first item of batch 2 removes.
- 3, an injectable sleep and clock for the renderer tests: the sleep is upstream's
  `Misc::FrameRateLimiter`, which sleeps on `std::this_thread` itself. Injecting one means the seam
  carrying a limiter of its own beside upstream's for a test's sake.
- 5, the perf fifo's late reader: a latch on the writer's first refused open is a hook in
  `PerfControl` for a test's sake. The 100 ms stand under the open's 10 s patience, so the test
  cannot fail by a race.
- 8, the descriptor pool shared by the passes: the bytes are the two sets each array allocates at
  the layout's maximum, which a shared pool allocates the same; sizing a set to its scene is the
  set made again at every arrival that the comment at `sBindings` refuses.
- 10, `setScene` building alone with one `placeScene` on every branch: the build stands the scene
  placed, its rows, its first copy's set and its top level, and a test traces it straight after.
  A placement after it would refit and build the top level over the second copy for nothing, on
  the frame a rebuild already makes the longest.
- 12, `ShadingMap` moved to the test support: the harness's contact sheet draws with it
  (`contactsheet.cpp`), so it is no test's alone and what its comment says of the sheet holds.
- 15, whether a ground hears the gates said by the ground: the engine builds the gates before the
  world stands a ground up (`Engine::prepareEngine`), so the question is the renderer's to answer,
  and a ground of a renderer that answers no is never told a gate.
- 11, a batch that owns its stamp: the one batch open across submits is the interface's, whose
  images a host reads only through `GuiTextures::read`, which hands the batch over first, and whose
  buffers are its own staging, given back stamped at the hand-over. Naming at hand-over would keep
  a list of every stamp a batch named, for a reader that does not exist.
- 11, the cycle between the command pool and the memory wrappers: it is of `.cpp` files on headers
  inside one folder, `device`, and no header includes the other's; the move costs 31 call sites for
  an order the folder rule does not ask.
- 14, the composite resolved by tile: the composite costs 0.14 ms of the gate's stress frame,
  so a shared-memory apron and a blend without a branch buy a part of that, and the cascade's last
  level composes no sea frame for a measurement nobody took.
- 14, one gather and one box for the denoiser's kernels: the loops sum different sets (the fill and
  the sampled tile's inset in one, neither in the other), and the clamp at nought the other lacks is
  `packRgb9e5`'s own floor, which both stores pass through.
- 14, the sun's bent path filled by the host: a `refract` and a Fresnel term per call on a source's
  direction, against three fields more in each of the frame's three sources, for no measured cost.
- 16, Lua asking `support()` for the post-processing chain: the processor's absence is the
  renderer's own answer (the ray tracer makes none), and the bindings call the processor they test
  for; asking the seam first states the one fact twice.
- 16, one route from the game to the renderer: each is one expression into the world's renderer,
  and a route through the environment adds a member to upstream's `Environment` for five sites.
- 16, the record-type set beside `Terrain::RefKinds`: the four loops are the scripted ESM3 types
  among `handsOver`'s, which their comment names; a set both derive from is a type list over record
  classes for four lines.
- 16, Linux and macOS in files of their own: `AGENTS.md` keeps a chain inside the one file that
  owns a system's difference, and each POSIX file is that file, chosen by CMake against Windows'.
- 17, the harness's layout: no header includes itself through the chain (`film.cpp` reaches
  `film.hpp` again through `run.hpp`, which the guard answers), and the folder rule holds; the move
  of five files and the noise split is a re-layout nothing asks.
- 18, `WavePass` described by its tiles: the grid is the shaders' constant, and a pass made to take
  another for a test's small grid is a production interface made for the test.
- 18, the GUI textures' test file split: its halves share one fixture, which a split moves into a
  header both include, for a file read by its test names.
- 18, tests moved to files mirroring their units: eleven groups moved between files, each found by
  its name; new tests take the layout.
- 18, one resource fixture: four lines a test over a file system of its own, which a fixture's
  constructor would take as the same list.
- 18, the monitor's sleep: the test asks whether a waiter returned while the worker was in its
  turn, which only time shows; asserting the predicate the wait reads tests the predicate.
- 18, the free-slot rule as one table: each case builds its own table's rows through its own
  fixture, so one table over the three is the same assertions behind a switch on the table.
