# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A fork of OpenMW 0.52 whose purpose is an **experimental ray-traced renderer**. Upstream OpenMW is
the host engine — cells, references, physics, scripts, animation, weather, GUI — and it keeps all of
that. What it stops owning is the picture.

Three documents, and they do not overlap:

- **`docs/rtx/openmw.md`** — how the host engine is built and where the seams are. Read before touching
  anything in `apps/openmw/mwrender/`, `components/sceneutil/`, `components/resource/` or the
  settings plumbing.
- **`docs/rtx/plan.md`** — the route: the scene-mirroring decision, the milestones, the tooling.
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
- **A missing extension or feature is a hard failure naming it**, never a fallback path.

### Two renderers

The picture is reached twice — **Vulkan on Ada-class NVIDIA, Metal on Apple silicon** — as two
backends behind one API-neutral core, not a portability layer over either (`docs/rtx/backends.md`).

Content, light transport and what the scene *is* live in the core, written once. What is true of an
API lives in its backend, written twice, and that cost is paid rather than abstracted away.

**Each machine develops its own renderer and leaves the other alone.** The backend this box cannot
run is not built, tested or debugged here; its skipped tests are the result, not a gap to close. The
core is the exception — a mistake there is one nobody here can see.

## Commands

The RTX targets below are created by `docs/rtx/plan.md` M0. If `cmake` does not recognise `OPENMW_RTX`,
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
  -DOPENMW_USE_SYSTEM_RECASTNAVIGATION=ON -DOPENMW_USE_SYSTEM_GOOGLETEST=ON \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=mold
```

Arch packages: `yaml-cpp openscenegraph mygui openal boost recastnavigation ccache mold ninja
vulkan-headers vulkan-validation-layers shaderc spirv-tools`, and **`bullet-dp`, not `bullet`** —
OpenMW requires a double-precision Bullet and refuses to configure against the single-precision one.
The two packages conflict, so the single-precision `bullet` has to come out first.

```sh
cmake --build build -j32                      # everything configured
cmake --build build -j32 --target openmw-rtxtool   # just the harness — the usual inner loop
cmake --build build -j32 --target components-tests openmw-tests
```

**Never `cmake --build --clean-first`.** Upstream declares `files/lang/*.ts` — translation files in
the *source* tree, with thousands of human translations in them — as build byproducts of the
`translations` target. Cleaning deletes them, and the rebuild regenerates them from scratch with
`lupdate`, so every translation becomes `type="unfinished"`. `git checkout -- files/lang/` puts them
back. Delete the build directory instead if a clean build is really wanted.

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
cd build                       # --resources defaults to ./resources, so the tool runs from here

./openmw-rtxtool                                   # a window on the ship at Seyda Neen
./openmw-rtxtool view --view=balmora               # a window somewhere else; P prints the camera
./openmw-rtxtool shot --view=balmora --out=b.png   # one frame, no window — the default way to look
./openmw-rtxtool scene --view=balmora --twice      # what the renderer was handed
./openmw-rtxtool scene --list-views                # the named viewpoints
./openmw-rtxtool info                              # the device and its ray tracing limits

./openmw --skip-menu --new-game --start "Seyda Neen, Census and Excise Office"
```

`~/.config/openmw/openmw.cfg` points at the Morrowind install, so none of these needs `--data`.
Views live in `files/rtx/views.cfg` and each one is a task in `.zed/tasks.json`.

Game data: Morrowind GOTY at `/home/Games/Morrowind`, data files in
`/home/Games/Morrowind/Data Files`. Tests that need it **skip** when it is absent and **fail** when
the path is set and wrong — a silent skip looks like a pass.

### Verification, after changing code and before saying it works

Build the targets you touched, run the test binary that covers them with a filter, then format.
Building the world to check a one-line change in the harness is waste; so is claiming a change works
because it compiled.

**Do not open the game window to check a rendering change.** `openmw-rtxtool shot` renders the real
renderer headlessly in about a second and prints a summary line — hit fraction, camera, frame time —
so a change is checkable without a screenshot ever being looked at.

`openmw-rtxtool view` opens a window, which is for the things only a window shows: how something
moves, how it holds up while you fly through it, whether an artefact is a still or a shimmer. It
takes `--frames N` so it can also be run by something that cannot click — which is how the window
path gets exercised under the validation layers.

## Architecture, in one screen

OpenMW is OpenSceneGraph 3.6 on OpenGL. `Engine::frame` (`apps/openmw/engine.cpp:191`) runs
simulation, then `updateTraversal()`, then `renderingTraversals()` — and **that last call is the only
thing the RT renderer displaces**. Cull still runs: CPU skinning, terrain LOD and object paging are
all cull-time decisions (`docs/rtx/openmw.md` §3.4).

`MWRender::RenderingManager` (`apps/openmw/mwrender/renderingmanager.hpp`) is the rendering
god-object, owned by `MWWorld::World`. There is one abstract interface in the whole rendering layer
and it has one method, so **the seam has to be cut, not found**.

The RT renderer takes its scene by **mirroring the live OSG scene graph**, because by cull time the
world is already CPU-resident triangles with world transforms, resolved texture roles and — for
actors — already-skinned vertex positions. It reaches the screen through **GL/Vulkan interop**, not a
Vulkan window, so the GUI, the inventory doll, the local map and video playback keep working. Both
decisions and their alternatives are in `docs/rtx/plan.md` §2–3.

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
  frame, logging that compiles out. There is a test that enforces this (`docs/rtx/plan.md` §7.3).
- **No exceptions in this fork's code.** A broken contract is an assert; a failure a caller can act
  on is a returned value. `-fno-exceptions` is not available — upstream's `vfs/pathutil.hpp` and
  `esm/*.hpp` throw from headers this code includes — so the rule is held by review.
- **Asserts** guard contracts the code must keep, not data the world might supply. Hot paths use the
  debug-only form; untrusted input is never an assert.
- **Tests assert hand-computed values**, cover the empty and boundary cases, and prove that
  parameters matter (A → X, B → Y, `X != Y`). "It ran and produced something plausible" is not a test.
  Pure refactors need none.

### Adding a setting

Four places, and the build enforces it (`docs/rtx/openmw.md` §6): the category header in
`components/settings/categories/`, the default in `files/settings-default.cfg`, the documentation in
`docs/source/reference/modding/settings/`, and — for a user-facing toggle — either four `UserString`
lines in `files/data/mygui/openmw_settings_window.layout` or a widget in
`apps/launcher/ui/graphicspage.ui` with load/save in `graphicspage.cpp`.

### Warnings

Warnings are errors in this fork's own targets and nowhere else — upstream's tree and `extern/`
belong to other people, and breaking the build on their warnings would only mean turning it off
again. The flags live in one place, `OPENMW_RTX_COMPILE_OPTIONS` in the root `CMakeLists.txt`, on top
of the `-Wall -Wextra -Wshadow -pedantic` the project already sets.

Adding a warning to that list means measuring it first. `-Wfloat-equal` fires 29 times on deliberate
`== 0.0f` sentinels; `-Wold-style-cast` and `-Wuseless-cast` fire inside OpenMW's own headers, which
this fork does not fix. All three were tried and rejected on those counts.

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

**Only this fork's own code.** A defect that came in with upstream OpenMW is not logged, not fixed
and not mentioned — the tree is 400k lines somebody else wrote and cataloguing it is not work this
project wants. Check with `git show d7db6be390:<path>` (the last upstream commit) before writing an
entry. Upstream code the RTX renderer *depends on* is different: if it blocks a milestone, it stops
being an upstream quirk and becomes this project's problem.
