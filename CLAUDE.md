# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A fork of OpenMW 0.52 whose purpose is an **experimental ray-traced renderer**. Upstream OpenMW is
the host engine — cells, references, physics, scripts, animation, weather, GUI — and it keeps all of
that. What it stops owning is the picture.

Three documents, and they do not overlap:

- **`openmw.md`** — how the host engine is built and where the seams are. Read before touching
  anything in `apps/openmw/mwrender/`, `components/sceneutil/`, `components/resource/` or the
  settings plumbing.
- **`plan.md`** — the route: the scene-mirroring decision, the milestones, the tooling.
- **this file** — goals and working rules. Status goes in commits, never here.

The reference implementation is **`/home/xxorza/Projects/rtxmw/`** — a Rust Morrowind ray tracer
with working water, caustics and volumetric fog. Its `docs/design.md` is 3,400 lines of accumulated
findings, most of which are about *Morrowind's content*, not about Rust: ray offsets on sheet
geometry, discarded outermost transforms, Z-first Euler angles, the pre-lit albedo problem, DLSS
parameter traps. **Read the relevant section before debugging something that looks already solved.**
Its shaders are more current than its prose. Its licence is MIT OR Apache-2.0 and it is the same
author's; this fork is GPLv3.

## Posture

A 2002 game made to look astonishing on current hardware — ray-traced visibility, path-traced
indirect light, materials recovered from pre-lit vanilla textures, DLSS Ray Reconstruction, opacity
micromaps, SER. Vanilla content, new light transport.

Priorities, in order:

1. **How it looks.** Trading image quality for simplicity or convenience is the wrong trade.
2. **Performance.** 1920×1080 internal → 3840×2160 at 60 fps (`docs/design.md` §5.3).

Nothing else ranks: no mod compatibility, no configurability for its own sake, no portability layer,
no abstraction over hardware this does not target.

Sports programming — strongest technique over safest, fast path first, delete what stopped earning
its place, settle arguments by measuring. Nothing here is published, so rewriting beats working
around.

### What that means against upstream

Upstream's constraints are not ours. Where they conflict, ours win.

- **The rasterizer stays working and untouched.** The RT path is a separate compile-time option
  (`-DOPENMW_RTX=ON`, off by default) and a separate runtime setting. Not a refactor of the existing
  renderer, not a strategy pattern bolted onto `RenderingManager`.
- **No merge-back discipline.** This fork is not upstreaming. Do not shape a change around what a
  GitLab reviewer would accept.
- **Rasterizer workarounds do not come across.** Render-bin ordering, the transparent pass, the
  distortion pass, shadow-map tuning — the RT path answers those questions with rays.
- Assume **Ada-class NVIDIA on Linux**. A missing extension is a hard failure naming the extension,
  not a fallback path.

## Commands

The RTX targets below are created by `plan.md` M0. If `cmake` does not recognise `OPENMW_RTX`,
or `apps/rtxtool/` is absent, that milestone has not landed and the rest of the tree builds as
upstream does.

Configure once. Turning off the Qt tools cuts the build roughly in half:

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DOPENMW_RTX=ON \
  -DBUILD_COMPONENTS_TESTS=ON -DBUILD_OPENMW_TESTS=ON \
  -DBUILD_OPENCS=OFF -DBUILD_WIZARD=OFF -DBUILD_ESSIMPORTER=OFF \
  -DBUILD_MWINIIMPORTER=OFF -DBUILD_OPENCS_TESTS=OFF \
  -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=mold
```

```sh
cmake --build build -j32                      # everything configured
cmake --build build -j32 --target openmw-rtxtool   # just the harness — the usual inner loop
cmake --build build -j32 --target components-tests openmw-tests
```

Tests are gtest binaries run directly. **There is no ctest registration in this project.**

```sh
./build/components-tests
./build/openmw-tests
./build/components-tests --gtest_filter='SettingsValuesTest.*'   # one suite
./build/components-tests --gtest_filter='*shouldLoadFromSettingsManager*'  # one test
./build/components-tests --gtest_list_tests
```

Formatting and the other CI gates:

```sh
CLANG_FORMAT=clang-format-14 CI/check_clang_format.sh   # CI pins 14; this box has 22, they differ
CI/check_file_names.sh
CI/check_cmake_format.sh
clang-format -i <file>...
```

Running things:

```sh
./build/openmw-rtxtool shot out.png --view seyda-shore      # headless, no window — the default way to look
./build/openmw --skip-menu --new-game --start "Seyda Neen, Census and Excise Office"
./build/openmw-navmeshtool --help                           # the model for headless world loading
```

Game data: Morrowind GOTY at `/home/Games/Morrowind`, data files in
`/home/Games/Morrowind/Data Files`. Tests that need it **skip** when it is absent and **fail** when
the path is set and wrong — a silent skip looks like a pass.

### Verification, after changing code and before saying it works

Build the targets you touched, run the test binary that covers them with a filter, then format.
Building the world to check a one-line change in the harness is waste; so is claiming a change works
because it compiled.

**Do not open the game window to check a rendering change.** `openmw-rtxtool shot` renders the real
renderer headlessly in about a second and prints a summary line — hit fraction, mean luminance, frame
time — so a change is checkable without a screenshot ever being looked at. The window is for the
things only the window can show.

## Architecture, in one screen

OpenMW is OpenSceneGraph 3.6 on OpenGL. `Engine::frame` (`apps/openmw/engine.cpp:191`) runs
simulation, then `updateTraversal()`, then `renderingTraversals()` — and **that last call is the only
thing the RT renderer displaces**. Cull still runs: CPU skinning, terrain LOD and object paging are
all cull-time decisions (`openmw.md` §3.4).

`MWRender::RenderingManager` (`apps/openmw/mwrender/renderingmanager.hpp`) is the rendering
god-object, owned by `MWWorld::World`. There is one abstract interface in the whole rendering layer
and it has one method, so **the seam has to be cut, not found**.

The RT renderer takes its scene by **mirroring the live OSG scene graph**, because by cull time the
world is already CPU-resident triangles with world transforms, resolved texture roles and — for
actors — already-skinned vertex positions. It reaches the screen through **GL/Vulkan interop**, not a
Vulkan window, so the GUI, the inventory doll, the local map and video playback keep working. Both
decisions and their alternatives are in `plan.md` §2–3.

Code lives in `components/rtx/` (Vulkan, knows no OSG scene graph and no game headers),
`components/rtxbridge/` (`osg::Node` → scene description), `apps/openmw/mwrender/rtx/` (the game-side
owner) and `apps/rtxtool/` (the headless harness).

## Conventions

**C++20, `.clang-format` at 120 columns.** The user's global Rust rules do not apply to this tree;
the posture behind them does.

- **Comments say *why*.** A block comment on a type or a non-obvious function says what it is for.
  Inside a body, a comment earns its place by naming an invariant, a workaround and its cause, or a
  trade-off against the obvious alternative — never by restating the line under it. No decorative
  dividers.
- **Fix stale narration in code you are already editing**, like fixing indentation on a line you are
  changing. Sweeping files you are not otherwise in is a separate task.
- **No backward compatibility.** Rename, change signatures, rewrite callers, delete what stopped
  earning its place. No shims, no compat wrappers.
- **Allocation is a metric on the frame path.** Persistent scratch buffers refilled with `clear()`,
  results into an out-parameter, nothing that constructs a `std::string` or a `std::function` per
  frame, logging that compiles out. There is a test that enforces this (`plan.md` §7.3).
- **Asserts** guard contracts the code must keep, not data the world might supply. Hot paths use the
  debug-only form; untrusted input is never an assert.
- **Tests assert hand-computed values**, cover the empty and boundary cases, and prove that
  parameters matter (A → X, B → Y, `X != Y`). "It ran and produced something plausible" is not a test.
  Pure refactors need none.

### Adding a setting

Four places, and the build enforces it (`openmw.md` §6): the category header in
`components/settings/categories/`, the default in `files/settings-default.cfg`, the documentation in
`docs/source/reference/modding/settings/`, and — for a user-facing toggle — either four `UserString`
lines in `files/data/mygui/openmw_settings_window.layout` or a widget in
`apps/launcher/ui/graphicspage.ui` with load/save in `graphicspage.cpp`.

### Adding a shader

`components/rtx/shaders/`. Compiled by `glslc` and validated by `spirv-val` in the same build step,
so an invalid module fails the build rather than the frame. Structures shared with C++ live in
headers guarded by `#ifdef __cplusplus`, included by both sides — never duplicated.

## Not on my own initiative

- **Committing.** "Do the work" authorizes the change, not the commit. Finish, verify, stop for the
  diff to be inspected. Wait for "commit" / "commit push" / "ship it".
- **New dependencies.** Propose the library and why, then wait.
- **Posting to GitHub/GitLab** — no PRs, comments, issues or merges. Read-only `gh` is fine.
- **Authorship.** Nothing in a commit, comment or shared artefact mentions AI tooling. No co-author
  trailers, no "generated with".

## Issue log

A bug noticed outside the current change goes to `./.notes/ISSUES.md` (create `.notes/` if missing)
and then work carries on — no fixing it, widening the task, or stopping to ask. Describe the issue
only: no fix, patch, severity or rationale. A fixed issue is **deleted**, not annotated; the file
lists open issues only.
