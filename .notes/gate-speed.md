# The gate's speed: proposal and plan

What `./omw gate` costs on the weak machine, a low-end AMD integrated GPU under Windows, and how to cut it
without dropping a claim it makes. Every figure here is from
[`docs/research/2026-11-13-gate-step-times.md`](../docs/research/2026-11-13-gate-step-times.md), measured on
that one machine between `e21fad4c21` and `9e66e68b8b`; nothing was measured on a high-end NVIDIA laptop GPU, and
no code was changed.

**No whole gate has run on this machine at `9e66e68b8b`.** At `e21fad4c21` it stopped at `tests`, on two tests
that `d0bb9081b3` has since fixed; the steps after `tests` were timed by themselves.

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
  and 64 (`RtxVisibilityTest::shoot`'s `size`). What it pays for is the validation layers' shader checks, below.
- **It applies to `check` and `repeat`, and halves both.** They draw 1920x1080 and are bound by the card:
  119 ms of card zones in a 133 ms frame. At a quarter of the pixels `check` takes 89 to 93 s and `repeat` 85 to
  86 s, with the same 59 checks asked, none failed, and the pair identical.

## Root causes

- **The world runs draw a picture no claim reads.** Fifteen of `check`'s sixteen claims are about what the
  renderer was handed, the camera, the sky, the queue and the frames in flight; `finite` alone counts over pixels,
  and `repeat` hashes them. What a place holds is the same at every size, to the tenth of a MiB.
- **A world stands 420 frames before it is whole, in three processes a gate**, each a frame at full size:
  `check`'s first stop and both of `repeat`'s runs (`MeasureWindow`, one cell a frame by `CellRing::adoptHanded`).
- **The release compile builds the test binaries, which nothing runs in that flavour**: 138 of the sample's 277
  objects and 59% of its compile seconds.
- **Every validated process runs the layers' shader checks over pipelines `check` has them run over anyway.**
  The checks are 4.6 s of a GPU test process's 7.3 s start and 44 of its 46 CPU seconds, and they go on through
  its tests: 103 CPU seconds a shard against 29 without them. The kernels' tuples are specialization constants,
  which the layer's cache of checked modules, keyed by a module's code, saves a third of and no more. `check`
  makes every kernel of the same shader set under the same layers, 338 pipelines to the tests' 337, every gate.
- **That CPU is why the GPU binary does not scale past two processes here**: four and six take as long as two
  with the checks, and 33 and 30 s without them against 51 to 57.
- **Every size the gate draws is whole workgroups.** The shaders' groups are 8 a side, and 1920x1080 and
  1280x720 divide by 8 each way: no world run draws a partial group, and no chain that halves meets an odd
  extent in its first three levels.
- **The gate's order is not its costs' order**: the release compile, minutes on a change, runs ahead of a minute
  of tests.

## Decisions

- **`check` and `repeat` each state a frame of their own where the line names none.** The size belongs to the
  verb, as `check`'s hold already does (`sCheckHoldMs`, "where the line names no `--hold` of its own"), so
  `./omw check` and the gate's are one run on every machine. Refused: a size chosen by the machine, which makes
  what a green gate proved depend on where it ran; a size only the gate passes, which leaves two `check`s to tell
  apart; a ninth of the pixels (640x360), which saves 28 s more of the 373 for under half the pixels again,
  because the host bounds the frame from a quarter down (the ship's frame 56.7 ms against 44.4, the walk alone
  23 ms).
- **What the smaller frame costs is said where the size is stated**: `finite` and `repeat` see a quarter of the
  pixels a frame, so an event one pixel in a million meets is met a quarter as often. No class of fault leaves:
  the layers, the claims about the scene, the frames in flight and the processes' agreement do not read the size.
  A full frame stays one switch away, `--size=1920x1080`.
- **`tests` and `their timing` run ahead of the release compile.** `gate.py` states its order as the order of
  cost, and on a change the compile is the dearer by minutes. A green gate takes as long; a red test says so
  minutes sooner.
- **The release compile builds the programs and no test binary.** A target a test runs leaves `all`
  (`EXCLUDE_FROM_ALL`, set in `openmw_add_test`), and `omw build` still names the tests, as
  `Build.build_default` already does. Refused: every program as now, 59% of the sample's release compile
  seconds and two of its seven longest links for binaries no workflow builds in that flavour (`ci.yml`'s four
  legs are debug, asan and tsan, and `rtx-release.yml` runs `omw archive`). The cost: a test source that warns
  or fails to compile under `-DNDEBUG` alone is found by whoever next runs `./omw release test`. LLVM does the
  same: its unit tests are out of `all` unless `LLVM_BUILD_TESTS` says otherwise, and the `UnitTests` target
  builds them. No test target is installed, which is the one thing CMake warns an excluded target about.
  Decided 2026-11-13.
- **The gate's `repeat` stays at one pair.** Refused: two pairs at the smaller frame, a second comparison for
  43 s a gate. `AGENTS.md` already asks for `--pairs=10` after touching anything a frame reads, which at a
  quarter of the pixels takes less than four pairs do today. Decided 2026-11-13.
- **The GPU binary runs in four shards once its processes stop checking shaders.** Measured with the step's
  other suites beside them: 35.4 s against 55.6 s as now, and 31.8 s in six. Four, because six buys 3.6 s for two
  more starts and devices, and `cmake/Tests.cmake` found a third process no gain on another card: the count is
  measured there before it lands (Batch 3). Sharded by `GTEST_TOTAL_SHARDS`, which runs each test exactly once
  across the shards, and not a test a process, since every process stands up two devices and two renderers.

## Open decisions, yours

**1. A round frame or an odd one?** Both cost the same: `check` 92.7 and 91.4 s at 960x540 against 90.7 and
90.7 s at 957x539, `repeat` 84.9 against 85.7 s, every check passing and both pairs identical.

| option | what it does | cost | who does it that way |
|---|---|---|---|
| **957x539, traced at 638x359 (my pick)** | no extent of the shown or the traced frame is a multiple of 8, and three of the four are odd: every 8x8 pass draws a partial group, and every chain that halves meets an odd extent by its second level | a size nobody recognises, in `--help` and on `check`'s pictures; a fault it finds first shows as a red gate | Khronos's OpenGL ES conformance run draws at 64x64 and then again at 113x47; this tree's own GPU tests draw at 17, 33, 65 and 129 |
| 960x540, traced at 640x360 | the size `AGENTS.md` already names for a bench on a weak machine | of its four extents only 540 is not a multiple of 8, so the traced frame is whole groups, as it is today | — |

**2. Where do the layers' shader checks run?** They compare each pipeline with its shaders and run `spirv-val`
over each specialized module (the layer's `check_shaders`, the same switch as
`VK_VALIDATION_FEATURE_DISABLE_SHADERS_EXT`); its own settings file calls them "CPU intensive during application
start up". The build already runs `spirv-val` over every module it produces. All 499 GPU tests pass without
them, so no test reads their messages.

| option | what it does | cost | measured |
|---|---|---|---|
| **in `check` alone (my pick)** | the test binary's devices and renderers ask for every check but the shader checks; `check` keeps them, over every kernel the renderer makes | a pipeline only a test makes, from a test's own shader or tuple, is no longer compared with its shader by the layers; `./omw test` by itself no longer runs the shader checks over the kernels, and the gate does | the test step's processes done in 35.4 s against 55.6 s |
| in `check` and on the tests' raw devices | only the tests' renderers leave them out, whose kernels are `check`'s | none in coverage; the checks that ran through the tests stay, 30 CPU seconds a shard beyond the start's | not measurable without the change: between the two rows around it |
| everywhere, as now | nothing changes | 4.6 s a start, 74 CPU seconds a shard, and no gain from a third process | 55.6 s |

## Refused

- **Keeping a step's result while its inputs stand** (Bazel's test cache, or a stamp file Ninja tracks). The
  four costly steps read what no build graph holds: the driver, the layers, the player's `openmw.cfg` and
  content. Bazel reruns exactly such a test whatever the cache says (`--cache_test_results`, a test tagged
  `external`), and a stale green is the worst answer a gate can give. The steps a cache could soundly keep sum to
  thirteen seconds.
- **The shader checks off in `check` as well.** Then nothing a gate runs compares a pipeline with its shader.
- **A suite of the test binary that runs the shader checks once.** It computes what `check` computes, for 46 CPU
  seconds beside the shards.
- **The layer's cache of checked shaders made to cover the tuples.** It is keyed by a module's code and the
  tuples are specializations of one module; that is the layer's design, not a setting.
- **The release compile beside the device steps.** The one lever that would hide the compile whole, and this is
  the machine it cannot be pulled on: a gate here was stopped for memory inside its release compile
  (`.notes/PERF.md`), and `crash.matrix` has already failed once under a loaded gate (`.notes/ISSUES.md`).
- **Two world runs side by side.** `AGENTS.md` shares the card between no two runs, and `check`'s `queue-held`
  and `frames-overlap` time the queue. Reopened only by a measurement of twenty clean gates run that way.
- **The prelude beside the build.** Thirteen seconds, and the gate would no longer stop at its cheapest failure.
- **`check` on the release build.** Faster by the asserts it would lose.
- **A shorter crossing, a shorter warm-up, fewer stops.** Each is a claim asked of less: `rooms-held` and
  `crossings-append` are about a long walk.
- **The integration tests in parallel or at another size.** Upstream's script, at 640x480 already, and its tests
  wait on the wall.
- **Frames ahead of a whole world closed untraced.** Under the smaller frame the host's walk bounds them, so
  the trace is not what they cost, and a history a measured frame reads would start elsewhere.

## What the plan comes to

- **With nothing to compile, 516 s becomes some 300 s** on this machine: `check` 178.7 to 91, `repeat` 194.1 to
  85 (Batch 1), and the test step's 59.6 to some 37 if the second open decision goes my way (Batch 3). The last
  is the step's processes timed side by side, not a `ctest` run.
- **The release compile loses its test objects**: 59% of the sample's compile seconds. Its wall without them
  is not measured.
- **What is left is the host in a build with every assert**: 1537 frames of `check` at a median 53 ms and
  1800 of `repeat` at some 42, the walk 23 ms of a frame at the ship. That is the renderer's own cost and no
  part of this plan.

# Plan

How an item is marked:

- **measure** — the gain is not known; the named run decides it before the change is taken.
- **yours** — waits for the answer to an open decision above.
- unmarked — read in the code and measured in the research note.

No item touches upstream's files: `apps/rtxtool`, `tools/omw`, `cmake/Tests.cmake`, `components/rtxvulkan` and
the GPU tests' support are the fork's. Before Batch 1, one `./omw gate` at the tip, so the plan starts from a
gate that passes on this machine and a time for all of it.

## Batch 1 — The world runs' frame

1. **`check` draws its own frame where the line names no `--size`**, the size the first open decision settles.
   `ToolLine::Frame::mSize` becomes a `Named<FrameSize>` as `mHold` is (`apps/rtxtool/toolline.hpp`, read in
   `options.cpp`), and `commandCheck` takes the verb's own frame where it is not named, beside the hold
   (`apps/rtxtool/main.cpp`). The size is one constant beside `sCheckHoldMs` in `framed.hpp`, whose comment
   carries the decision, its cost, and for an odd size which groups and chains it is odd for. `--help`'s line for
   `--size` says what `check` draws.
2. **`repeat` walks at the same size where the line names no `--size`.** A default beside `WALK` and `TAKEN`
   in `tools/omw/repeat.py`, not one of `TAKEN`, which the line may not name: a difference is still read at any
   size. The module's first lines say the size and why. The two constants are one fact in two programs, so a
   test of the driver reads the harness's and holds `repeat`'s to it, as `test_main.py` holds `HARNESS_VERBS`
   to `verbs.cpp`.
3. **Tests.** `apps/components_tests/rtxtool/options.cpp`: `check` with no size is its own, with
   `--size=1920x1080` it is that, and `bench` with none is still 1920x1080, so the default is proved the verb's.
   `tools/omw/tests/test_repeat.py`: the line `repeat` builds carries the size once, and a named one replaces it.
4. **A known hit at the new size.** `repeat` must still say NOT repeatable for a fault it was written to catch:
   with `4924c912f8`'s nearest-cell adoption put back to the first cell handed, in a scratch worktree, `./omw
   repeat --pairs=3` differs. And `check` must still fail: one barrier of the cascade taken out, the layers
   report it at the new size. Neither change is kept.
5. **measure** — both verbs at 1920x1080 and at the new size on a high-end NVIDIA laptop GPU, added to the
   research note. The size is one for every machine; where the host already bounds the frame at 1920x1080 the
   note says the change bought that machine nothing and cost it nothing.
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
   components-tests` still builds and runs; CI's `cmake --install` step, which brings `all` up to date, still
   installs what it did.
4. **measure** — the release compile's wall for one renderer header touched, before and after 2, into the
   research note.

## Batch 3 — The layers' shader checks and the shards

1. **yours** — **`ValidationOptions` says whether the layers check shaders**, an `enum class` and not a flag,
   checked unless said otherwise, beside `ValidationLevel` in the core and in its words, naming no layer and
   no API (`components/rtx/renderer/renderer.hpp`). `Instance` alone turns it into
   `VK_VALIDATION_FEATURE_DISABLE_SHADERS_EXT` (`components/rtxvulkan/device/instance.cpp`, which today
   disables nothing).
   The one place that asks for none is the GPU tests' support (`makeHarness` and `describeRenderer` in
   `apps/components_tests/rtxvulkan/support/device/harness.cpp`, or `describeRenderer` alone under the second
   option), with the reason and where the checks run instead.
2. **yours** — **A test holds `check` to the shader checks**, since the tests now lean on it: the options
   `check` is made with ask for them and for the layers, in `apps/components_tests/rtxtool/options.cpp`.
3. **A known hit.** In a scratch worktree, a kernel's layout loses a binding its shader reads: `./omw check`
   reports the layers' error and fails. Not kept.
4. **The GPU binary in four shards** (`cmake/Tests.cmake`), its comment rewritten from these measurements: what
   a process pays to start, why two were the most with the shader checks on, and the figures for two, four and
   six.
5. **measure** — `./omw test` on this machine after 1 and 4, and `components-tests`' own time inside it: 37 s in
   one process beside two checked shards, 19.7 s alone. If it bounds the step it is registered in shards as the
   GPU binary is; four took 7 to 12 s beside four GPU shards.
6. **measure** — two shards against four on a high-end NVIDIA laptop GPU, the shader checks off. The count is
   one for every machine: four unless it is slower there than two, and then the note says by how much each
   machine would lose to the other's count.
7. **The research note takes each figure**, and `their timing`'s limit is left alone: no test's own time
   changes.

## Batch 4 — The record

1. **`AGENTS.md`, Verification**: `repeat`'s line says the frame it walks at, and `./omw test`'s that the layers'
   shader checks are `check`'s if the second open decision goes that way. Proposed as wording; the file is yours
   to commit.
2. **The lasting decisions fold into their owners**: the frame and its cost into `framed.hpp` and `repeat.py`,
   the order and the release compile's reach into `gate.py`, the excluded targets and the shard count into
   `cmake/Tests.cmake`, where the shader checks run into `renderer.hpp` and the tests' `harness.cpp`. This
   file is then deleted.

# Sources

- Khronos, Vulkan-ValidationLayers, `layers/vk_layer_settings.txt`: `check_shaders` and `check_shaders_caching`,
  <https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/main/layers/vk_layer_settings.txt>
- Khronos, Vulkan-ValidationLayers, `docs/shader_validation.md`: when shaders are checked, what the cache keys
  on, and that specialization constants are injected for the checks,
  <https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/main/docs/shader_validation.md>
- Khronos, VK-GL-CTS, `glcKhronosMustpassEs.hpp`: the conformance run's surfaces, 64x64 and 113x47,
  <https://github.com/KhronosGroup/VK-GL-CTS/blob/main/external/openglcts/modules/runner/glcKhronosMustpassEs.hpp>
- CMake, the `EXCLUDE_FROM_ALL` target property,
  <https://cmake.org/cmake/help/latest/prop_tgt/EXCLUDE_FROM_ALL.html>
- LLVM, "Building LLVM with CMake": `LLVM_BUILD_TESTS` and the `UnitTests` target,
  <https://llvm.org/docs/CMake.html>
- GoogleTest, "Distributing Test Functions to Multiple Machines",
  <https://google.github.io/googletest/advanced.html>
- Bazel, the user manual, `--cache_test_results`, <https://bazel.build/docs/user-manual>
