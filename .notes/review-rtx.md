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
- **The harness has no typed boundary for its command line.** The raw `variables_map` travels in
  `Command` to every verb. Target: `options` parses into typed records once.
- **A result is written and read back, or worked out twice.** The harness decodes PNGs it just wrote;
  the deep read-back copies the picture three times; `gatherBeside` walks a whole model once per
  drawable; the fog's air walk works the lamp chord out twice. Target: keep what is in memory, walk a
  model once per model, state the chord once.

Beside these: comments that name removed code, a few test-shape items, and one upstream change the
Accepted diff does not list.

## Plan

Batches in order of impact. Each leaves the build and the tests green. Whoever addresses an item
deletes it.

### 8. The harness's shape — `apps/rtxtool/{options,hosted,main}.*`

- [ ] `hosted.hpp` `Command` carries the raw `variables_map` and `const ToolOptions&` to every
  verb, which reads about 100 options by string in `main.cpp` (`variables["hold"].as<bool>()`).
  Target: `options` parses the line once into typed records per verb; `Command` holds those.

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
