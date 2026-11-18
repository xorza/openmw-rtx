# AGENTS.md

## What this is

A fork of OpenMW whose purpose is a **ray-traced renderer**: a 2002 game made to look astonishing on current hardware —
ray-traced visibility, path-traced indirect light, materials recovered from pre-lit vanilla textures. Upstream OpenMW
stays the host engine — cells, references, physics, scripts, animation, weather, GUI — and stops owning the picture.
Vanilla content and the PBR replacers made for OpenMW are read as they are: what a replacer's companion maps add reaches
the trace, and a vanilla picture does not change because the renderer can read them.

Read `apps/openmw/mwrender/renderer.hpp` and its callers before changing the renderer seam, and follow scene or resource
data back to its owner before changing how the ray tracer consumes it. What the tree, `--help` or a commit already
answers does not belong here.

## Rules

- **Upstream code changes only on the user's explicit yes**, anywhere in the tree: a fix, an improvement or a change of
  design. Each is proposed first, with what it changes, why, and what it costs upstream's merges, and waits for a yes.
  An approved change is recorded in [`.notes/UPSTREAM.md`](.notes/UPSTREAM.md) with its reason. One you made without
  recording it there is reverted; one already in the tree that you did not make is reported and left alone. The diff
  stays small, but never at the cost of reuse or of the abstraction's quality.
- **Both renderers stand behind one interface** that exposes no implementation detail. Where the game would branch on
  which renderer it has, the seam abstracts the question instead. One binary ships both, and the game starts the ray
  tracer alone: a start it cannot make ends in the fatal error box, and the OpenGL renderer stays built and unstarted
  so that upstream's code merges as it stands.
- **Target hardware**: NVIDIA RTX 20 series and later, AMD RDNA 2 and later. A vendor the machine has no card of is
  stood up under Mesa's drm-shim, which compiles every kernel and executes none.
- **Tried, declined, and not to be proposed again**:
  - opacity micromaps (`VK_EXT_opacity_micromap`) for the cutouts: no faster, and longer loading;
  - async compute (the next trace on a second queue beside this frame's reconstruction): 0.1–0.2 ms gained;
  - hashed alpha (Wyman and McGuire 2017) for the soft edges, a cut fixed to the surface in place of one drawn each
    frame: a moving frame stayed noisier than under the upscaler's
    jitter, and the edges converged harder, with more fireflies;
  - Shader Execution Reordering (`VK_EXT_ray_tracing_invocation_reorder`): its sorting cost 17–23% of the trace, and it
    shuts out Mesa's drivers;
  - fixes to the rasterizer alone where the ray tracer already does right, such as its ripple field's fixed sixtieth
    step and its object paging's copy of a `NightDaySwitch`'s authored child.

## Where things live

**Vulkan behind an API-neutral core**, not a portability layer. A fact about Vulkan that leaks into the core is a bug
whether or not a second backend ever arrives. `docs/architecture.md` is the shape as a reader meets it — the seam, the
layers, who owns whom, the order a frame is computed in — and the headers hold the detail.

- `components/rtx/` — the core: what the scene _is_, the light transport. No graphics API, no game headers.
- `components/rtxvulkan/` — the backend. What is true of an API lives here and nowhere else; the two places that stand
  one up name `VulkanRenderer` and nothing else does.
- `components/myguirtx/` — MyGUI's backend.
- `components/contentstore/` — what is derived from the content, kept on disk across sessions.
- `apps/openmw/mwrender/` — the seam `MWRender::Renderer`, `GlRenderer` beside upstream's files, and the game-side
  owner in `rtx/`.
- `apps/rtxtool/` — the harness: what a run visits and what a place came to in `model/`, and in `instruments/` what a
  measured run is taken with, none of which knows a world.
- The core and the backend stand in folders by responsibility, in the order `architecture.md` lists: a folder includes
  only the folders before it, and a folder inside another is a part of it. `RtxSourceTreeTest` holds the order, so a
  folder that needs a later one is a file in the wrong folder or a file to split.
- `docs/` — the fork's design documents (`architecture.md`, `build.md`, `settings.md`, `look.md`), and dated
  measurements in `docs/research/`.
- `.notes/` — working notes: `UPSTREAM.md`, `ISSUES.md` (open issues), `PERF.md`, `todo.md`, and a proposal while its
  work is open.

## Verification

`./omw [flavour] <verb>` is the one way in; `./omw help` lists the verbs and the flavours, and
`./omw exec ./openmw-rtxtool --help` the harness's options. The flavour is `debug` unless named; `release` is the build
a number is quoted from.

Three kinds of content load the renderer differently; where each installs, and the profile that names it, is the
machine's:

- vanilla Morrowind, what the player's `openmw.cfg` names and every suite has been measured on;
- a large mod list with Tamriel Rebuilt, such as the Morrowind Fullrest Repack, taken through its own profile with
  `--replace=config --config=<profile>`;
- Morrowind with PBR replacers stacked by a profile of their own: the content a measurement of companion maps, texture
  memory or residency needs.

The user runs the harness through the Zed tasks in `.zed/tasks.json`, one task a profile.

**Building and testing**

- Compiling is not verifying. Whether a change compiles is one source's object: `./omw exec ninja "$PWD/<source>^"`
  from the root, since `build.ninja` names sources by absolute path, and a header through one or two of its includers.
  Then build the targets you touched and run the covering binary with a filter: `./omw test <binary>
  --gtest_filter=...`. `./omw tidy --changed` lints what the working tree changed; `./omw lint driver` before a push
  that touches `tools/omw`.
- `./omw test` once before saying it works mid-round. The GPU binary fails without a device rather than skipping, so a
  green run means a device ran it. A shader change is checked against the validation layers by `check`, not by a test.
- `./omw gate` once at the end. It runs every suite, the whole clang-tidy and `check` itself, so none of them runs
  again beside it; its `repeat` walks one pair, so the ten a change to what a frame reads owes still run on their own.
  Never a gate beside a build or another gate, and never two runs at once: they share the card.

**Checking a rendering change** — through the harness at the places in `files/rtx/views.cfg` and the suites in
`files/rtx/benches.cfg`, never by opening the game window; `view` is for what only a window shows.

- `./omw shot --views=all --map --upscale=off --out=<dir>` before a change and `--against=<dir>` after it names the
  pictures the change moved. Narrow an investigation to its place, and run `all` last. Keep the baseline outside
  `/tmp`, whose cleaner empties it. `--upscale=off`, because an upscaled picture moves with anything its history saw.
- `./omw noise` holds the frame's noise against its own frames averaged, with each place's bias and fireflies. Its legs
  show different faults: `--strafe` and `--walk` a history length or a filter's reach, `--cut` the fireflies after a
  cut, `--upscale=native` a temporal filter that fetches its history off the pixel. An A/B is
  `./omw release noise --ab=<switch>`, with `--still` for a switch that touches short histories. Narrow it to one place
  and leg first; the suite is the verdict.
- `./omw kernels > before.txt` before a shader change and `--against=before.txt` after it names the kernels it moved.
- `./omw repeat --pairs=10` after touching anything a frame reads: two processes walk one place with the upscaler and
  the denoiser off, and must agree frame for frame. A pair that finds nothing has found nothing. Read a difference with
  `--exposure=1` and `--pictures`. The denoised frame is not bit-exact on every card (`docs/architecture.md`), so a
  difference in the composed frame alone is the card's.
- `./omw release tour --every=1` before a release, once a content: it flies every land cell and the largest rooms and
  fails on a fault met anywhere. A tour flies every fourth land leg and every fourth room unless `--every` says
  otherwise, a sample whose legs' p99 median stands within a few milliseconds of the whole tour's in a quarter of the
  time. What builds up over a flight shows only in the whole tour: the handheld's Repack rooms ran at 21 ms flown
  alone and at 51 ms after the land legs. `--plan` says what it will fly and for how long. To verify a fix or A/B a
  change, fly only the legs its question needs: `--legs=` takes `rooms`, `land`, legs by number, `a-b` and `a-` (the
  rest after a crash, with `--every=1`), each leg held against the same leg of a whole tour by its name.

**Measuring**

- `./omw release bench`, on a hot card, back to back, after a throwaway warm-up leg; never a sleep between runs. The
  first run after a shader change compiles the pipelines. On a weak machine bench at `--size=960x540`, the same size on
  both sides of an A/B.
- On a quiet desktop: start the run in the background and end the turn. In the foreground it runs under the agent's
  own redraws, and every row moves. The report's `card` lines name another process's work.
- An A/B reads medians and the p99, never a mean — the zones' from the record `--json` writes, since the `gpu` row is
  each zone's share of the mean.
- A heap profile runs in a cgroup with a memory cap (`systemd-run --user --scope -p MemoryMax=<GiB>`): heaptrack's
  interpreter outgrew the desk's memory over a Repack tour and took the desktop down.
- Profiling: `./omw profile` for the CPU, into `out/release/perf/` (`--offcpu` says where it waits); read the full
  reports beside the summary. For the GPU, `./omw release exec nsys profile ./openmw-rtxtool bench ...`.

## Conventions

**C++20, `.clang-format` at 120 columns.** The user's C++ rules (`~/.claude/rules/cpp.md`) hold for the code the fork
writes, with the additions below, which win where the two differ; upstream's code is brought in line only where
`.clang-tidy` checks one.

- **Includes in five blocks** a blank line apart: the file's own header, the C++ standard library, `<gtest/...>`,
  other libraries, `<components/...>` and `<apps/...>`, then quoted local headers — of the file's own folder only, any
  other folder spelled from the root. A block out of order carries a comment saying why.
- **Fixed-width integers, `std::`-qualified, and never a change of signedness**: `std::int32_t`, `std::uint8_t` and their
  kin, `std::size_t` and `std::ptrdiff_t` for sizes, counts, indices and differences; `char` for text, `int main`, and
  a library's own typedef where the value is its. `long` is chosen by its width, each by hand. `omw format` writes it,
  and a keyword kept on purpose carries `NOLINT(google-runtime-int)` with its reason.
- **The headers GLSL reads as well** are the one exception to `#pragma once` and the place the preprocessor still
  switches, and `shaders/portable.h` says why.
- **A general system fact lives in `components/platform`.** A test of an `assert` calls `Testing::expectAssertDies`.
- **GPU vendors stand behind one entity, as systems do**: one interface with an implementation file per vendor, chosen
  once, from the driver's identity at device creation or by CMake where a vendor's SDK is built in. Every other file
  asks a capability or an interface, never a vendor ID, a driver ID or a vendor's name; a branch on the vendor
  anywhere else carries its reason beside it.
- **Frame times are uniform**, and an average that hides a spike is not an answer. Work is _incremental_, never
  _batched behind a threshold_: a table recycles its slots, a resource is appended rather than rebuilt. What cannot be
  made cheap belongs off the frame path entirely, not on a rota. Report the p99 and the worst frame beside the median.
- **Loading is a hot path too**, its allocations held by a test as the frame's are. A loader is a persistent object
  owning its buffers: cells arrive while the game is running, so a spike taken at load is a spike a player feels.
- **Nothing is computed twice, and everything as early as it can be** — at initialization or at load. A frame reads
  what it was handed.
- **A shader's float arithmetic is the build's, not the driver's.** Every module is pinned
  (`components/rtxvulkan/spirv/spirvpin.hpp`), so GLSL is written as usual and an operation the pinning refuses stops
  the build. Nothing is fused, so every product and sum rounds where it is written, and an `fma` is refused.
- **One path through a shader.** A single computation covering every case beats a tree that skips work per lane: a
  factor of zero, a table lookup, a value selected without a jump. Divergence needs a measurement saying the branch
  pays for itself, named where the branch lands.
- **Asserts** guard contracts the code must keep, not data the world might supply. The tree ships every caller of its
  interfaces, so each contract is internal and a debug-only `assert`, hot paths included: the debug build every test
  runs in finds a misuse, and the release build pays nothing for it.
