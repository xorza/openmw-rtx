# The gate's speed: proposal and plan

What `./omw gate` costs on the weak machine, a low-end AMD integrated GPU under Windows, and how to cut it
without dropping a claim it makes. Read and measured at `e21fad4c21` with a clean tree. Every figure here is from
[`docs/research/2026-11-13-gate-step-times.md`](../docs/research/2026-11-13-gate-step-times.md), measured on
that one machine; nothing was measured on a high-end NVIDIA laptop GPU, and nothing in the tree was changed.

**The gate does not pass on this machine at this commit**: two tests of `components-tests` fail under MSVC
(`.notes/ISSUES.md`), so it stops at `tests`. The steps after it were timed by themselves.

## Where the time goes

| step | seconds | |
|---|---|---|
| format check, spellings, lint, the driver's tests | 12.8 | |
| build, the listing check | 3.8 | nothing to build |
| the release compile | 362.0 | two commits' worth: 277 objects, 28 links |
| tests, their timing | 59.6 | the two GPU shards bound it at 57 to 58 s |
| the integration tests | 67.2 | |
| check | 178.7 | |
| repeat | 194.1 | |

516 s with nothing to compile, of which `check` and `repeat` are 373. A change to the renderer adds its debug
build and the same objects again in the release compile.

## The idea of a lower resolution

- **It does not apply to the GPU tests.** `rtx-gpu-tests` already draws 16 to 65 pixels a side, mostly 32, 33
  and 64 (`RtxVisibilityTest::shoot`'s `size`). Its time is a start of 7.2 s a process and some 0.15 s a test.
- **It applies to `check` and `repeat`, and halves both.** They draw 1920x1080 and are bound by the card:
  119 ms of card zones in a 133 ms frame. At 960x540 `check` takes 89.2 s and `repeat` 86.0 s, with the same 59
  checks asked, none failed, and the pair identical.

## Root causes

- **The world runs draw a picture no claim reads.** Fifteen of `check`'s sixteen claims are about what the
  renderer was handed, the camera, the sky, the queue and the frames in flight; `finite` alone counts over pixels,
  and `repeat` hashes them. What a place holds is the same at every size, to the tenth of a MiB.
- **A world stands 420 frames before it is whole, in three processes a gate**, each a frame at full size:
  `check`'s first stop and both of `repeat`'s runs (`MeasureWindow`, one cell a frame by `CellRing::adoptHanded`).
- **The release compile builds the test binaries, which nothing runs in that flavour**: 138 of the sample's 277
  objects and 59% of its compile seconds.
- **A validated start makes 337 pipelines its cache already holds over six seconds and 45 CPU seconds**, where
  the unvalidated renderer in the same process makes the same 337 in 0.14 s. Every GPU shard pays it, and so does
  `check`; `repeat`'s runs load no layers and do not.
- **GPU test processes do not scale past two here**: four and six take as long as two, and a process spends
  1.5 to 1.8 times its wall in user CPU. `components-tests` takes 46 s beside two GPU shards against 19.7 s
  alone.
- **The gate's order is not its costs' order**: the release compile, minutes on a change, runs ahead of a minute
  of tests.

## Decisions

- **`check` and `repeat` each state a frame of their own, 960x540, where the line names none.** The size
  belongs to the verb, as `check`'s hold already does (`sCheckHoldMs`, "where the line names no `--hold` of its
  own"), so `./omw check` and the gate's are one run on every machine. Refused: a size chosen by the machine,
  which makes what a green gate proved depend on where it ran; a size only the gate passes, which leaves two
  `check`s to tell apart; 640x360, which saves 28 s more of the 373 for under half the pixels, because the host
  bounds the frame from 960x540 down (the ship's frame 56.7 ms against 44.4, the walk alone 23 ms). Khronos's
  OpenGL conformance suite draws small for the same reason: its README asks for 256x256 as the least that gives
  stable results, and its Android example runs at 128x128.
- **What the smaller frame costs is said where the size is stated**: `finite` and `repeat` see a quarter of the
  pixels a frame, so an event one pixel in a million meets is met a quarter as often. No class of fault leaves:
  the layers, the claims about the scene, the frames in flight and the processes' agreement do not read the size.
  A full frame stays one switch away, `--size=1920x1080`.
- **`tests` and `their timing` run ahead of the release compile.** `gate.py` states its order as the order of
  cost, and on a change the compile is the dearer by minutes. A green gate takes as long; a red test says so
  minutes sooner.
- **The GPU binary keeps its two shards until its start is understood.** Four and six were measured no faster
  here, and `cmake/Tests.cmake` measured three no faster on another card. The count is not the lever; the start
  and the CPU a process spends over its wall are (Batch 3).
- **The release compile builds the programs and no test binary.** A target a test runs leaves `all`
  (`EXCLUDE_FROM_ALL`, set in `openmw_add_test`), and `omw build` still names the tests, as
  `Build.build_default` already does. Refused: every program as now, 59% of the sample's release compile
  seconds and two of its seven longest links for binaries no workflow builds in that flavour (`ci.yml`'s four
  legs are debug, asan and tsan, and `rtx-release.yml` runs `omw archive`). The cost: a test source that warns
  or fails to compile under `-DNDEBUG` alone is found by whoever next runs `./omw release test`. LLVM keeps its
  unit tests out of `all` the same way and builds them for `check-all`. Decided 2026-11-13.
- **The gate's `repeat` stays at one pair.** Refused: two pairs at the smaller frame, a second comparison for
  43 s a gate. `AGENTS.md` already asks for `--pairs=10` after touching anything a frame reads, which at
  960x540 takes less than four pairs do today. Decided 2026-11-13.

## Refused

- **Keeping a step's result while its inputs stand** (Bazel's test cache, or a stamp file Ninja tracks). The
  four costly steps read what no build graph holds: the driver, the layers, the player's `openmw.cfg` and
  content. Bazel reruns exactly such a test whatever the cache says (`--cache_test_results`, a test tagged
  `external`), and a stale green is the worst answer a gate can give. The steps a cache could soundly keep sum to
  thirteen seconds.
- **The release compile beside the device steps.** The one lever that would hide the compile whole, and this is
  the machine it cannot be pulled on: a gate here was stopped for memory inside its release compile
  (`.notes/PERF.md`), and `crash.matrix` has already failed once under a loaded gate (`.notes/ISSUES.md`).
- **Two world runs side by side.** `AGENTS.md` shares the card between no two runs, and `check`'s `queue-held`
  and `frames-overlap` time the queue. Reopened only by a measurement of twenty clean gates run that way.
- **More shards, or a test a process.** Measured: no faster past two, since every process pays the start.
- **The prelude beside the build.** Thirteen seconds, and the gate would no longer stop at its cheapest failure.
- **`check` on the release build.** Faster by the asserts it would lose.
- **A shorter crossing, a shorter warm-up, fewer stops.** Each is a claim asked of less: `rooms-held` and
  `crossings-append` are about a long walk.
- **The integration tests in parallel or at another size.** Upstream's script, at 640x480 already, and its tests
  wait on the wall.
- **Frames ahead of a whole world closed untraced.** Under the smaller frame the host's walk bounds them, so
  the trace is not what they cost, and a history a measured frame reads would start elsewhere.

## What the plan comes to

- **With nothing to compile, 516 s becomes 318 s** on this machine by Batch 1 alone: `check` 178.7 to 89.2,
  `repeat` 194.1 to 86.0.
- **The release compile loses its test objects**: 59% of the sample's compile seconds. Its wall without them
  is not measured.
- **Batch 3 promises nothing until it has measured.** If a validated start's six seconds are the layers checking
  shaders they have checked before, three starts a gate carry them, the two shards' and `check`'s, and a cheap
  start is what would let more than two shards pay.
- **What is left is the host in a build with every assert**: 1537 frames of `check` at a median 52 ms and
  1800 of `repeat` at some 42, the walk 23 ms of a frame at the ship. That is the renderer's own cost and no
  part of this plan.

# Plan

How an item is marked:

- **measure** — the gain is not known; the named run decides it before the change is taken.
- unmarked — read in the code and measured in the research note.

No item touches upstream's files: `apps/rtxtool`, `tools/omw` and `cmake/Tests.cmake` are the fork's. The first
item is not this plan's: the two failing tests are fixed first, or no gate here says anything.

## Batch 1 — The world runs' frame

1. **`check` draws 960x540 where the line names no `--size`.** `ToolLine::Frame::mSize` becomes a
   `Named<FrameSize>` as `mHold` is (`apps/rtxtool/toolline.hpp`, read in `options.cpp`), and `commandCheck`
   takes the verb's own frame where it is not named, beside the hold (`apps/rtxtool/main.cpp`). The size is one
   constant beside `sCheckHoldMs` in `framed.hpp`, whose comment carries the decision and its cost. `--help`'s
   line for `--size` says what `check` draws.
2. **`repeat` walks at 960x540 where the line names no `--size`.** A default beside `WALK` and `TAKEN` in
   `tools/omw/repeat.py`, not one of `TAKEN`, which the line may not name: a difference is still read at any size.
   The module's first lines say the size and why.
3. **Tests.** `apps/components_tests/rtxtool/options.cpp`: `check` with no size is 960x540, with
   `--size=1920x1080` it is that, and `bench` with none is still 1920x1080, so the default is proved the verb's.
   `tools/omw/tests/test_repeat.py`: the line `repeat` builds carries the size once, and a named one replaces it.
4. **A known hit at the new size.** `repeat` must still say NOT repeatable for a fault it was written to catch:
   with `4924c912f8`'s nearest-cell adoption put back to the first cell handed, in a scratch worktree, `./omw
   repeat --pairs=3` differs. And `check` must still fail: one barrier of the cascade taken out, the layers
   report it at 960x540. Neither change is kept.
5. **measure** — both verbs at both sizes on a high-end NVIDIA laptop GPU, added to the research note. The size
   stays 960x540 unless the host already bounds the frame there at 1920x1080, in which case the note says the
   change bought that machine nothing and cost it nothing.
6. `./omw repeat --pairs=10` once at the new size, then the gate.

## Batch 2 — The gate's order and what the release compile builds

1. **`tests` and `their timing` move ahead of `the release compile`** in `STEPS` (`tools/omw/gate.py`). The
   help's line follows from `SUMMARY`; `test_main.py` holds the two to each other already.
2. **A target a test runs is no part of `all`.** `openmw_add_test` sets `EXCLUDE_FROM_ALL` on its
   target (`cmake/Tests.cmake`), with the reason beside it. `Build.build_default` names the test targets already,
   so `omw build`, `omw test` and CI build what they built. `_release_compile`'s comment says what it no longer
   compiles.
3. **Verification of 2.** `ninja -t targets all` in `out/release` names no test binary and still names every
   program; `./omw build` in a fresh debug folder still builds all nine test targets; `./omw release test
   components-tests` still builds and runs.
4. **measure** — the release compile's wall for one renderer header touched, before and after 2, into the
   research note.

## Batch 3 — The GPU tests' start and their CPU

Nothing here is changed before its measurement. Windows records a CPU sampling trace by itself (`wpr`); reading
one takes Windows Performance Analyzer or Visual Studio's profiler, whichever the machine already has, and an
install is asked for before it is made. The runs below that need no trace go first.

1. **measure** — **what the layers do with a cached pipeline**: 45 CPU seconds over 337 pipelines in
   `rtx-gpu-tests --gtest_filter=RtxPinningTest.*`. It is the core checks' cost and not synchronization
   validation's: `./omw info` makes its pipelines at one rate under `--validation=on` and `sync`. The layer
   documents a shader validation cache, on by default (`check_shaders_caching`), and no file named
   `shader_validation_cache*` stood in the temp folder after these runs; that is where to look first, by the
   layer's own settings from the environment, one at a time, before any trace.
2. **measure** — **which thread spends the CPU a process has over its wall**, from the same kind of trace over
   one half of the binary alone and both halves together: 68.6 s of user CPU in 44.8 s, 103 s in 57 s.
3. **measure** — **why `components-tests` takes 46 s beside two GPU shards and 19.7 s alone**, its own user CPU
   going from 9.7 to 21.9 s. Once the GPU shards drop under 46 s it bounds the step.
4. **What 1 to 3 find decides the change**, each a decision brought with its source: a layer setting stated where
   the instance gathers its settings (`components/rtxvulkan/device/instance.cpp`), a wait that should block, or
   `components-tests` registered in shards as the GPU binary is (four took 3.6 to 6.6 s alone). The shard count
   is measured again only after the start is cheap, on both cards.

## Batch 4 — The record

1. **`AGENTS.md`, Verification**: `repeat`'s line says the frame it walks at, and the gate's cost on a weak
   machine if one is worth stating. Proposed as wording; the file is yours to commit.
2. **The lasting decisions fold into their owners**: the frame and its cost into `framed.hpp` and `repeat.py`,
   the order and the release compile's reach into `gate.py`, the excluded targets into `cmake/Tests.cmake`.
   This file is then deleted.
