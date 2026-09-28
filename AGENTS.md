# AGENTS.md

## What this is

A fork of OpenMW whose purpose is an **experimental ray-traced renderer**. Upstream OpenMW
stays the host engine — cells, references, physics, scripts, animation, weather, GUI. It stops
owning the picture.

Read `apps/openmw/mwrender/renderer.hpp` and its callers before changing the renderer seam, and
follow scene or resource data back to its owner before changing how the RT path consumes it. What
the tree, `--help` or a commit already answers does not belong here.

## Posture

A 2002 game made to look astonishing on current hardware — ray-traced visibility, path-traced
indirect light, materials recovered from pre-lit vanilla textures. Vanilla
content and the PBR replacers made for OpenMW, new light transport: what a replacer's companion
maps add reaches the trace, and a vanilla picture does not change because the renderer can read
them.

## Rules

- Do not change the rasterizer, or anything the ray tracer does not need. Two changes the ray
  tracer needs do move the rasterizer's picture, each where upstream's was wrong: the optimizer
  merges in child order, not address order, and an exterior map tile keeps its land where the
  quad tree did not build the chunk yet.
- Both renderers stand behind one interface that exposes no implementation detail. Where the game
  would branch on which renderer it has, the seam abstracts the question instead.
- Performance matters. Compute nothing twice; compute as early as possible.
- Target hardware: NVIDIA RTX 20 series and later.
- One binary ships both renderers, and the one not chosen never starts.
- Opacity micromaps (`VK_EXT_opacity_micromap`) for the cutouts were tried and declined: the
  trace did not get faster, and building the maps only added loading time. Do not propose them again.
- Async compute (a second queue, the next trace beside this frame's reconstruction) was tried and
  declined: the overlap gained 0.1–0.2 ms. The branch `async` has the record. Do not propose it
  again.
- Shader Execution Reordering (`VK_EXT_ray_tracing_invocation_reorder`) was tried and declined:
  sorting cost 17–23% of the trace, and the extension shuts out Mesa's drivers. Do not propose it
  again.
- Keep the diff against upstream minimal, but never at the cost of reuse or of the abstraction's
  quality. The `[RTX]` settings pages and their translations are a fine price, and so is
  `components/crashcatcher`: upstream's crash catcher is replaced whole by the fork's own, a
  Crashpad monitor process, and that diff is accepted rather than kept small. So are `README.md`,
  which is the fork's own page and what a package ships, and `CI/`.

## Where the code lives

**Vulkan on ray-tracing NVIDIA hardware, Turing and later**, behind an API-neutral core rather than
a portability layer. A fact about Vulkan that leaks into the core is a bug whether or not a second
backend ever arrives.

- `components/rtx/` — the core: the scene description, the light transport, what the scene _is_. No
  graphics API, no game headers.
- `components/rtxvulkan/` — the backend. What is true of an API lives here and nowhere else; the
  two places that stand one up name `VulkanRenderer` and nothing else does.
- `components/myguirtx/` — MyGUI's backend.
- `apps/openmw/mwrender/rtx/` — the game-side owner. `apps/rtxtool/` — the harness: what a run
  visits and what a place came to in `model/`, and in `instruments/` what a measured run is taken
  with — the frame times, the card's clock, perf's fifo, the driver's cache, a frame hash, a scene
  digest and a texture sheet, none of which knows a world.
  `MWRender::Renderer` — the seam, and `GlRenderer` beside upstream's files in `mwrender/`.
- `docs/rtx/architecture.md` — the shape of the above as a reader meets it: the seam, the
  layers, who owns whom, and the order a frame is computed in. The headers hold the detail.
- The core and the backend stand in folders by responsibility, in the order `architecture.md`
  lists: a folder includes only the folders before it, and a folder inside another is a part of
  it. `RtxSourceTreeTest` holds the order, so a file goes where what it includes allows, and a
  folder that needs a later one is a file in the wrong folder or a file to split.

## Verification

- Build the targets you touched, run the covering test binary with a filter, then `./omw format`,
  which rewrites the tree; `./omw format --check` changes nothing and is what the gate runs.
  Compiling is not verifying.
- `./omw` at the root is the one way in, `omw [flavour] <verb>`, and `./omw help` lists both. The
  flavour is `debug` unless named: every assert and the tests. `release` is the build a number is
  quoted from, and `profile` runs in it whatever is named. `asan` adds the sanitizers, `plain` is
  upstream's tree with its suites whole, and `package` is the one `archive` puts into `dist/`.
- `./omw test <binary> --gtest_filter=...` builds and runs one test binary with a filter.
- `./omw test` once before saying it works: the `fork` label of CTest — the fork's half of
  `components-tests` and `openmw-tests`, `rtx-gpu-tests`, and the crash matrix. `--all` adds
  upstream's suites. The GPU binary fails without a device rather than skipping, so a green run
  means a device ran it; `--without-device` leaves it out on a box with no driver.
- `./omw gate` once at the end: format check, the driver's tests, build, the release compile,
  tests, `check`, one repeat pair, stopping at the first failure. Never a gate beside a
  build or another gate.
- Do not open the game window to check a rendering change. The harness's verbs go through the
  driver, which builds `openmw-rtxtool` and runs it in the flavour's directory:
  `./omw [flavour] info|scene|shot|view|bench|check|film|noise`, and `./omw exec ./openmw-rtxtool --help`
  for their options. The places are `files/rtx/views.cfg`, the suites `files/rtx/benches.cfg`.
- `./omw shot --views=all --map --out=<dir>` ahead of a change and `--against=<dir>` after it says
  which pictures the change moved. `scene` reports what the renderer was handed. `check` asserts
  the tree's claims at every place of its suite. `bench` has the moving camera. `view` is for what
  only a window shows, and `film` flies through the keys `view --keys` wrote. `noise` holds the
  frame against a converged reference, and fails a frame noisier than sixteen frames averaged.
- `./omw kernels > before.txt` ahead of a shader change and `--against=before.txt` after
  it names the kernels the change moved, per tuple of their constants; a tuple it did not name
  draws what it drew.
- `./omw repeat --pairs=10` after touching anything a frame reads: two processes walk
  `one-cell-walk` for six seconds with the upscaler and the denoiser off, the second with the queue
  held behind the host, and must agree frame for frame; `--views=` and `--seconds=` move the walk.
  A run is the same run twice, and a pair that finds nothing has found nothing. Read a difference
  with `--exposure=1` and `--pictures=<dir>`.
- Measure with `./omw release bench`, on a hot card, back to back, never with a sleep between
  runs. Take a throwaway warm-up leg first. No frame times until the renderer draws everything the
  game has.
- Measure on a quiet desktop. A bench started from this session's foreground runs under Claude
  Code's spinner, which Zed redraws and KWin composites nine times a second, and every figure
  moves with it — the host rows by half, the zone shares by a tenth, the tail by 4 ms. Start the
  run in the background and end the turn; the report's `card` lines say whether that held, and
  `--frame-times=<dir>` writes the series behind a tail.
- Profiling: `./omw profile` for the CPU — the measured frames alone, at `seyda-neen-ship` unless
  `--view=` or `--suite=` names another, into `build-release/perf/`. `--offcpu` says where it
  waits, `--dwarf` unwinds without the frame pointers, and `--tui` walks the last recording.
  `./omw release exec nsys profile ./openmw-rtxtool bench ...` for the GPU. `ncu` is not installed.
- `./omw crash <dump>` reads a player's crash dump against a release's `-symbols.zip`, or the
  newest in `dist/`. `./omw game` is the game on the newest quicksave. A fresh box takes
  `./omw bootstrap` for the pinned Vulkan SDK, and `./omw setup <morrowind dir>`.

## Conventions

**C++20, `.clang-format` at 120 columns.** The user's global Rust rules do not apply to this tree;
the posture behind them does.

- **`#pragma once`, and includes in five blocks** a blank line apart: the file's own header, the C++
  standard library, `<gtest/...>`, other libraries, `<components/...>` and `<apps/...>`, then quoted
  local headers — of the file's own folder only, and any other folder is spelled from the root.
  `.clang-format` preserves the blocks and sorts inside each, so the order is the author's and the
  sorting is not. A conditional `#include` goes last, and a block out of order carries the comment
  saying why, the way `memory.cpp` does for the allocator. `components/rtx/shaders/*.h` is the one
  exception to `#pragma once`, and `portable.h` says why.
- **Include what you name.** A file that spells `std::size_t` includes `<cstddef>`. A `.cpp` may
  lean on its own header for what that header's interface already needs, and on nothing else.
- **The preprocessor switches only where nothing else can.** What systems spell differently goes
  in a `…posix.cpp` and `…win32.cpp` pair that CMake chooses, behind one header: a general fact in
  `components/platform` (`Platform::Process`, `Platform::SharedMemory`), the crash catcher's own
  in its `…system.hpp`. A build flag is defined in every build as `0` or `1` and read once into a
  `constexpr bool` (`Rtx::sDebugNames`, `Settings::sRayTracingBuilt`), which code asks with
  `if constexpr`. Code only one build has is a file CMake chooses (`nortxrenderer.cpp`), never an
  `#ifdef` around it. A test of an `assert` calls
  `Testing::expectAssertDies`, not `#ifndef NDEBUG`. What stays: a chain inside the one file that
  owns a system's difference (Linux beside macOS in a POSIX file), an include only one system has,
  and the headers GLSL and C++ both read, whose differences `shaders/portable.h` holds.
- **Comments say _why_**: an invariant, a workaround and its cause, a trade-off against the obvious
  alternative — never a restatement of the line under it. No decorative dividers.
- **Fix stale narration in code you are already editing.** Sweeping files you are not otherwise in
  is a separate task.
- **Frame times are uniform**, and an average that hides a spike is not an answer. Work is
  _incremental_, never _batched behind a threshold_: a table recycles its slots, a resource is
  appended rather than rebuilt. What cannot be made cheap belongs off the frame path entirely, not
  on a rota. Report the p99 and the worst frame beside the median.
- **Allocation is a metric on the frame path.** Persistent scratch buffers refilled with `clear()`,
  results into an out-parameter, no `std::string` or `std::function` per frame, logging that
  compiles out. A test enforces it.
- **Loading allocates no more freely than a frame does.** A loader is a persistent object owning its
  buffers, `clear()`ed and refilled for each thing it reads. Cells arrive while the game is running,
  so a spike taken at load is a spike a player feels.
- **Whatever can be computed once is computed once** — at initialization or at load. A frame reads
  what it was handed.
- **A shader's float arithmetic is the build's, not the driver's.** Every module is pinned
  (`components/rtxvulkan/spirv/spirvpin.hpp`), so GLSL is written as usual; an operation the pinning
  refuses stops the build, and the message says what it is. `precise` is for a value two shaders
  must compute to the bit.
- **One path through a shader.** A single computation covering every case beats a tree that skips
  work per lane: a factor of zero, a table lookup, a value selected without a jump. Divergence needs
  a measurement saying the branch pays for itself, named where the branch lands.
- **Asserts** guard contracts the code must keep, not data the world might supply. Hot paths use the
  debug-only form; untrusted input is never an assert.
