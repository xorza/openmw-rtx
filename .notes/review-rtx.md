# Review: the ray tracer and the fork's diff with upstream

Scope: `components/rtx`, `components/rtxvulkan` (C++ and shaders), `components/myguirtx`,
`apps/openmw/mwrender/rtx` with the seam, `apps/rtxtool`, `tools/omw`, their tests, and every
upstream file the fork changed since the merge base `16b14001cf`. Read in six slices, each with all
lenses. Tests: all green; components-tests 2622 in 8.3 s, openmw-tests 641 in 0.7 s, rtx-gpu-tests
473 in 32.3 s, no single test over 1 s.

The code is mature. No light-transport, sampling, synchronization or lifetime defect was found. The
upstream `-W` hunks keep every value, and no seam leak was found. What follows is the rest.

## Proposal

The findings come from six root causes. Each change below removes one cause, not one symptom.

- **A room is a constant set apart from what bounds it.** The texture table has no reservation at
  all, and the extractor's and the ring's rooms are sized for a four-cell reach while the reach goes
  to ten. Target: every table the frame path grows is reserved from the constant that bounds it
  (`TextureTable::sCapacity`, `LandReach::sMostCells`), its rows move without a throw, and passing a
  reservation asserts. A rehash or a row copy on an arrival frame is then impossible or loud.
- **A cache or a tag outlives the state it was derived from.** The ring keeps its last ask after
  the counts derived from it are zeroed indoors, and it tags every cell with a statics switch that
  only the constructor sets. Target: what decides what is read is part of `CellWorld`, which is
  compared whole and rebuilds the reader through `follow`; a cached ask and its counts reset
  together.
- **One fact is read two ways.** The device's free room is read from one pool of several; a cell's
  spelling is an exterior by one rule in the model and by another in the stager; the ambient before
  Night-Eye is recovered by a subtraction. Target: one source per fact — the room summed over every
  pool on the video heap, the spelling parsed once into a type at load, the ambient carried.
- **A contract stated at an edge is not kept.** A refused GUI texture is promised to draw as nothing
  and throws on `lock`; NVML's optional fields throw away the required one; the host thread's clock
  scales the cycles and not the time. Target: each edge does what its own doc says, through a pure
  function a test can feed.
- **The harness has no typed boundary for its command line, and `run.hpp` is four modules.** The raw
  `variables_map` travels in `Command` to every verb, which makes `options` and `run` include each
  other. Target: `options` parses into typed records once; describers, the view and suite schema
  (beside `BlockFile` in `model/`), and the hosted run (`hosted.hpp`) each have their own header; the
  run-folder functions join `harnessfolder.hpp`.
- **A result is written and read back, or worked out twice.** The harness decodes PNGs it just wrote;
  the deep read-back copies the picture three times; `gatherBeside` walks a whole model once per
  drawable; the fog's air walk works the lamp chord out twice. Target: keep what is in memory, walk a
  model once per model, state the chord once.

Beside these: comments that name removed code, a few test-shape items, and one upstream change the
Accepted diff does not list.

## Plan

Batches in order of impact. Each leaves the build and the tests green. Whoever addresses an item
deletes it.

### 8. The harness's shape — `apps/rtxtool/{options,run,hosted,main,film,stager,session,measurer,compare,noise,harnessfolder,cameradriver}.*`, `apps/rtxtool/model/{benchrun,blockfile}.*`, `components/rtx/renderer/png.*`, `components/rtxvulkan/{vulkanrenderer.cpp,device/memory/image.cpp}`

- [ ] `run.hpp:238-241` `Command` carries the raw `variables_map` and `const ToolOptions&` to every
  verb, which reads about 100 options by string in `main.cpp`; `run.hpp` includes `options.hpp` for
  that member alone, and `options.cpp` includes `run.hpp` for `Framed` (`:189`) and seven
  constants. Target: `options` parses the line once into typed records per verb; `Command` holds
  those; the cycle goes.
- [ ] `run.hpp` holds four modules: the stop describers (`describeSpot` … `cellArgument`), `SkyNote`
  (standingnote's alone), the view and suite schema (`loadViews` … `chooseViews`,
  `run.cpp:280-473`, while its field parser `BlockFile::readPlace` is in `model/blockfile.cpp:322`),
  and the hosted run (`Command`, `Framed`, `WindowRequest`, `ShippedDefaults`, `runHosted`), plus
  constants; `cameradriver.cpp` includes it for `sTurnSeconds` alone. Target: a describers header
  with `SkyNote`, the schema in `model/` beside `BlockFile`, `hosted.hpp`, each constant beside its
  readers.
- [ ] `options.hpp:96-123` / `options.cpp:773-831`: `ownConfigDirectory`, `ownSavesFolder`,
  `sweepEndedRuns` and `adoptConfigDirectory` own run folders, not options. Target:
  `harnessfolder.hpp`.
- [ ] One cell spelling, two rules: `Stand::mCell` (`model/benchrun.hpp:138-142`) and
  `isExteriorCell` (`film.cpp:89-101`) take a pair of integers for an exterior and anything else
  for an interior, while `Stager` (`stager.cpp:80`) asks `World::findExteriorPosition` first, which
  resolves a named exterior too: a hand-written `cell = Balmora` stages outdoors while the film plans
  an interior cut. Target: parse the spelling once, at load, into an exterior-or-interior type.
- [ ] `measurer.hpp:172-173` holds `const SessionRequest&` and `RunRecord&`, siblings inside
  `Session` (`session.hpp:50-157`), which is non-copyable only because `CardWatch` holds an atomic.
  Target: hand both at `Measurer`'s calls, or delete `Session`'s copy and move, as AGENTS.md asks of
  an owner wired by reference.
- [ ] `compare.cpp:262-263` `compareRuns` and `noise.cpp:69-77` `judgeNoise` read back and decode
  the PNGs this process just wrote: every picture of a run, and five 16-bit pictures a place, about
  30 MB decoded each at 2560x1440. Target: the session result keeps the run's pictures
  (`Rtx::PngImage`) from `StopWriter`/`Measurer`, the PNGs are still written, and only a reference
  folder is read.
- [ ] `vulkanrenderer.cpp:917` `readDeepPixels` with `image.cpp:430` `Image::read`: a new read-back
  buffer a call (an allocation of its own past half a 64 MiB block), a copy into a new vector, and
  another into `samples`: three copies of 8 B a pixel, 133 MB at 7680x2160; `readPixels` pays the
  buffer and one extra copy. Target: `Image::read` into a span the caller sized, the landing buffer
  kept as a `GrowableBuffer` as `FrameRing::mPictures` keeps its own.
- [ ] `components/rtx/renderer/png.{hpp,cpp}` is used by `apps/rtxtool` alone (`homekey`,
  `measurer`, `stopwriter`, `compare`, `noise`) and pulls the osgDB registry, zlib and file streams
  into the core. Target: move it and its test into `apps/rtxtool`; `frameImage` stays.
- [ ] `film.cpp:79-86` `loadKeys` repeats `BlockFile::load`'s open-and-refuse. Target: load through
  `BlockFile`, keep `readKeys(istream)` for the tests.

### 9. The core's edges — `components/rtx/renderer/{renderer,nullrenderer}.hpp`, `components/rtxvulkan/{createrenderer.cpp,vulkanrenderer.*,present/*}`, `apps/openmw/mwrender/rtx/rtxrenderer.cpp`

- [ ] `renderer.hpp:30` the API-neutral `Rtx::Renderer::setVerticalSync` takes `SDLUtil::VSyncMode`,
  a window layer's unscoped enum. Target: a core enum of present pacing; `RtxRenderer::setVSync`
  converts at the seam.
- [ ] `createrenderer.cpp:15-55` the backend factory reads `OPENMW_RTX_SYNC_VALIDATION`,
  `OPENMW_RTX_GPU_VALIDATION` and `OPENMW_RTX_WATCH_SUBMITS` and overrides its own options, while
  `OPENMW_RTX_BACKEND` is read game-side (`rtxrenderer.cpp:94`). Target: every environment read
  where the options are made, in `RtxRenderer`'s construction, which both hosts pass through; the
  factory is a function of its options alone.

### 10. Carried, not recovered — `apps/openmw/mwrender/{sceneframe.hpp,framedescriber.cpp}`, `apps/openmw/mwrender/rtx/skyreader.cpp`

- [ ] `skyreader.cpp:138` with `framedescriber.cpp:38` recovers the ambient before Night-Eye as
  `ambient - (ambient - before)`, exact only where the subtraction is (Sterbenz); past a doubling it
  misses `toLinear`'s exact `k/255` table (`colour.cpp:37-41`) by an ulp. Target: `WorldState`
  carries the ambient before Night-Eye beside the lift.

### 11. Tests — `apps/openmw_tests/mwrender/renderer.cpp`, `apps/components_tests/rtx/{scene/scenedesc.cpp,world/skylight.cpp,world/sun.cpp,scene/compositequeue.cpp,common/scratch.cpp}`

- [ ] `renderer.cpp:218-220` `awaitFrameSleepsToTheLimitAndAnswersIt` asserts a wall time under
  2 ms three times, which a loaded runner or a parallel CTest breaks. Target: assert what the limiter
  answers (`free`, `getLastHold`), or a bound that says why it is that size.
- [ ] `scenedesc.cpp`: `theSlotHandedOutIsTheSameHoweverTheSlotsWereGivenBack`,
  `everyTableHandsOutItsLowestFreeSlot` and `theLowestFreeSlotIsTakenHoweverTheSlotsWereFreed` test
  one rule on slices. Target: one sweep of table × free order keeping every case;
  `aFreedRigSlotIsHandedOutLowestFirstHoweverItsMeshesWent` stays apart.
- [ ] `skylight.cpp:132` writes out `sun.cpp:21`'s `vanillaHours()` field by field, for one source
  file. Target: one fixture, the sun's tests in one file.
- [ ] Gaps: `CompositeQueue::take` where the albedo fits and the gloss does not (`mSpecular ==
  sNoIndex` for a reflecting chunk); `dropSlack`'s boundary (`common/scratch.hpp:165`, capacity
  over twice the size).

### 12. Upstream diff — `AGENTS.md`, `components/testing/util.hpp`, `apps/components_tests/{main.cpp,lua/teststorage.cpp}`

- [ ] `components/testing/util.hpp:24-70` keys the test output folder on the clock and the process
  id and empties each test's folder at its start (`FreshTestDirs`); `apps/components_tests/main.cpp:26-32`
  sets `death_test_style` to `threadsafe` and adds that listener; `teststorage.cpp:103, 138` moves
  its files from `temp_directory_path()`. No Accepted-diff entry names these. Needs the user's
  call: an entry stating why the fork's suites need them (parallel processes, repeats, death tests
  under threads), or a revert.
