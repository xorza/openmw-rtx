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

- **Upstream code changes for three reasons only**: a change [Accepted diff](#accepted-diff)
  lists, an improvement to the ray tracer's integration — the seam and the hooks it needs — or a
  bug fix the user approved, which is proposed and waits for a yes. A cleanup or a quality change
  is not made, and one already made is reverted. The diff stays small, but never at the cost of
  reuse or of the abstraction's quality.
- Both renderers stand behind one interface that exposes no implementation detail. Where the game
  would branch on which renderer it has, the seam abstracts the question instead.
- Performance matters. Compute nothing twice; compute as early as possible.
- Target hardware: NVIDIA RTX 20 series and later, and AMD RDNA 2 and later. Only the NVIDIA card
  here runs anything; an AMD device is stood up under Mesa's drm-shim (`~/Projects/mesa/build-shim`),
  which compiles every kernel and executes none.
- One binary ships both renderers, and the one not chosen never starts.
- Tried, declined, and not to be proposed again: opacity micromaps (`VK_EXT_opacity_micromap`)
  for the cutouts, which made the trace no faster and only added loading time; async compute (a
  second queue, the next trace beside this frame's reconstruction), whose overlap gained 0.1–0.2 ms
  (the branch `async` has the record); and Shader Execution Reordering
  (`VK_EXT_ray_tracing_invocation_reorder`), whose sorting cost 17–23% of the trace and which
  shuts out Mesa's drivers.

## Accepted diff

The fork's changes to upstream that stay, each for the reason given. A change to upstream code
that is not here is held to the rules above.

**The rasterizer's picture.** Five changes the ray tracer needs move it, each where upstream's was
wrong:

- the optimizer merges in child order, not address order;
- an exterior map tile keeps its land where the quad tree did not build the chunk yet;
- a `NightDaySwitch` shows its mode's child from its first frame, not the child its file opens on;
- the particles a NIF saves wear what their age affectors give them from their first frame, not
  the controller's initial colour and size;
- a groundcover plant stands under its model's own transforms, as every other reference does, and
  not inside them (`GroundcoverShapes`).

A sixth is the seam's: both renderers draw their frame at `[Video] resolution x/y` and show it
scaled into the window with black beside it (`Misc::Presentation`), so the GUI, the projection, the
pointer and Lua read one size whichever renderer draws. A settings file from upstream, which kept
the window's size there, has it moved to the window on the first start, and the launcher and
`settings-default.cfg` say what each setting now sets.

**The rest of the tree.**

- The `[RTX]` settings pages and their translations.
- `components/crashcatcher`: upstream's crash catcher is replaced whole by the fork's own, a
  Crashpad monitor process, with the calls that set it up from the configuration
  (`Debug::setCrashReports`, the hang limit and the version).
- `README.md`, which is the fork's own page and what a package ships, and `CI/`.
- The fork's workflows in place of upstream's four, and the root tooling: `CMakePresets.json`,
  `.zed/`, `omw`, `omw.cmd`, `.claude/skills/`, `.gitattributes` and `.gitignore`. The CI the fork
  runs, and the driver every verification step goes through.
- The build the fork's presets and its Crashpad need: CMake 3.31, which reads the `$comment`s in
  `CMakePresets.json`; Boost 1.83, whose flat maps the scene identities are; no scan for modules
  (`CMAKE_CXX_SCAN_FOR_MODULES OFF`), which preprocessed every file twice for modules the tree has
  none of; the ccache fallback, so a runner without it builds; the embedded debug information
  (`CMAKE_MSVC_DEBUG_INFORMATION_FORMAT`), since ccache caches no compile that writes a shared PDB;
  and the `$<COMPILE_LANGUAGE:C,CXX>` wrapping, which keeps the C++ flags off Crashpad's MASM.
- `install_fork_licenses` and `files/licenses/`: the licences of what the fork ships — Crashpad,
  VMA, FidelityFX and the Vulkan loader — which ask to be shipped with them.
- The visibility gates (`MWScript::VisibilityGates` and the calls that feed them): without them
  the distance stands scripted stages the game keeps down.
- The lamp body marker (`SceneUtil::LampBody`), which `SceneUtil::addLight` leaves on the group it
  hangs a light in: the ray tracer's bounce takes no glow from a lamp's own model, whose lamp
  already delivers that light, and the rasterizer never reads it.
- The pose hook in `RenderingManager`'s intersection visitor (`Renderer::poseForIntersection`): a
  skinned body answers a CPU ray with the copy its last cull posed, and the ray tracer culls no
  world, so the crosshair met every actor in its bind pose.
- The sky meshes' vertex rules, `components/sky/vertexrules.hpp`, which `ModVertexAlphaVisitor`
  reads: the rasterizer and the ray tracer fade the cloud shell and the star dome by one rule each.
- `SceneUtil::StateSetUpdater::getGeneration`, which `reset` bumps: the mirror applies an updater to
  a state set of its own after the node's update did, and a glow that ended or changed colour by
  `reset` left the mirror's copy with its last sheet.
- `MWWorld::MoonModel::phaseEighths`, the phase continuous in game time beside the engine's
  discrete one: the ray tracer draws the moon to the horizon, where the engine changes a phase, and
  its terminator follows this one rather than jumping a quarter phase in plain view.
- The port to SDL3, through the input, the GUI and the window code: the presentation reads a
  window's pixel density and display scale, which a fractionally scaled Wayland desktop sets and
  SDL2 cannot report. SDL3 has no gamma ramp, so `[Video] gamma` is the renderers' own: the
  rasterizer's canvas applies it in its last draw into the frame (`PingPongCanvas`), as the tone
  pass does in the ray tracer. `[Video] contrast` went with the ramp and is not restored: it had no
  menu control, upstream applied it on Windows alone, and the tone pass has a contrast grade of its
  own. SDL3 names a controller's buttons by their place (`SDL_GAMEPAD_BUTTON_SOUTH`) where SDL2
  named them by Xbox's labels, and the bindings keep the places: a pad with other labels is played
  with the same thumb. A Lua cursor is sized in the frame's pixels, as upstream sized it in the
  window's, so it is scaled by the frame's shown scale alone (`Presentation::shownScale`), and not
  by the interface's scaling as well. Where relative mouse mode is refused, upstream wraps the
  pointer by warping it; Wayland, which SDL3 takes where SDL2 took X11, lets no window move the
  pointer, so there it goes unwrapped and stops at the window's edge
  (`InputWrapper::mWarpMovesPointer`), where the way back from each warp turned the camera.
- The five checks the top-level `CMakeLists.txt` adds to upstream's, on for the whole tree, and
  the hunks in upstream code that keep it clean under them, the patches to `extern/sol3` and
  `components/files/configurationmanager` included: one set of checks for every file. And MSVC's
  `4244` and `4267` off for the whole tree: GCC's `-Wall -Wextra` leave `-Wconversion` out, so the
  narrowing they warn of held MSVC's builds alone, and one set of checks is one on every compiler.
- A number read from text is finite (`Misc::StringUtils::toNumeric`, which the settings read
  through): `std::from_chars` reads `inf` and `nan`, and no sanitizer stopped either reaching
  the picture. Where `from_chars` has no floating point, the stream reads only the prefix it would
  (`floatPrefix`), so a spelling is one number on every toolchain.
- What the game does in one step and the ray tracer's histories must hear: a reference the world
  moves with its physics placed outright is told as a jump (`World::moveObject`'s `jumps`,
  `RenderingManager::notifyJumped`), and a write of `GameHour` that moves the clock by more than the
  frame's own step is a cut (`World::noteHourWritten`, `DateTimeManager::jumps`).
- What the ray tracer reads of the rasterizer's own state: the projection offset `SceneFrame` is
  handed beside the projection, and `Precipitation::isShown` and its occlusion setting, so neither
  renderer draws rain the other hides.
- The renderer's answer to what it declines (`Renderer::support`), asked where the console, Lua and
  the settings window would otherwise toggle what does nothing under it, and `ToggleBorders` under
  the ray tracer.
- Three faults the user approved fixing: `Files::LinuxPath` took a failed `read_symlink` for the
  executable's path (`ec.value() != -1` holds for every error), the SDL3 port truncated a
  window's size over its pixel density where `SDLUtil::windowPoints` rounds, and
  `cmake/FindOSGPlugins.cmake` restored `CMAKE_FIND_LIBRARY_PREFIXES` unquoted, which dropped
  MSVC's empty prefix and left every later `find_library` blind to `bz2.lib`.
- `RenderingManager::getFieldOfView`, which returned the override flag, 1°, wherever a field of
  view was overridden; and the local map's view built in double, as the ray tracer's map tile reads
  it.
- A look of the ray tracer's own against upstream's: its ripple field steps every frame by the time
  the water's clock moved (`RipplePass::record`), where upstream's steps once a sixtieth. The same
  springs over the same time, at a cost every frame pays alike, and a wake that keeps its pace under
  sixty frames a second, where upstream's slows.

## Where the code lives

**Vulkan on ray-tracing hardware, NVIDIA Turing and AMD RDNA 2 and later**, behind an API-neutral core rather than
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

- Build the targets you touched and run the covering test binary with a filter. `./omw build`
  formats the tree first, except on CI, whose checks job checks it once; `./omw format` rewrites
  the tree alone, and `./omw format --check` changes nothing and is what the gate and CI run.
  Compiling is not verifying.
- `./omw` at the root is the one way in, `omw [flavour] <verb>`, and `./omw help` lists both. The
  flavour is `debug` unless named: every assert and the tests. A flavour comes before the verb and is
  refused after it (`omw release build`, not `omw build release`). `release` is the build a number is
  quoted from, and `profile` runs in it and refuses another flavour named before it. `asan` adds
  the address and undefined-behaviour sanitizers and `tsan` the thread sanitizer, which the daily
  run builds apart, `full` builds every program the tree has, the CS among them, and `package` is
  the one `archive` puts into `dist/`.
- `./omw test <binary> --gtest_filter=...` builds and runs one test binary with a filter.
- `./omw test` once before saying it works: every suite CTest has, `rtx-gpu-tests` and the crash
  matrix among them. The GPU binary fails without a device rather than skipping, so a green run
  means a device ran it; `--without-device` leaves it out on a box with no driver.
- The first run after a shader change includes the driver compiling its pipelines: time a suite on
  a second run.
- `./omw gate` once at the end: the steps `./omw help` lists, from `tools/omw/gate.py`, stopping
  at the first failure. Never a gate beside a build or another gate.
- Do not open the game window to check a rendering change. The harness's verbs go through the
  driver, which builds `openmw-rtxtool` and runs it in the flavour's directory:
  `./omw [flavour] info|scene|shot|view|bench|check|film|noise`, and `./omw exec ./openmw-rtxtool --help`
  for their options. The places are `files/rtx/views.cfg`, the suites `files/rtx/benches.cfg`.
- `./omw shot --views=all --map --upscale=off --out=<dir>` ahead of a change and `--against=<dir>`
  after it says which pictures the change moved: 56 s for every place, 9 s for one, so an
  investigation narrows to its place and `all` is the last run. A baseline stays outside `/tmp`,
  whose cleaner empties it under a long session. `--upscale=off`, because the harness upscales at
  `quality` unless told, and an upscaled picture moves with anything its history saw. `scene` reports what the renderer was handed. `check` asserts
  the tree's claims at every place of its suite. `bench` has the moving camera. `view` is for what
  only a window shows, and `film` flies through the keys `view --keys` wrote. `noise` holds the
  frame's noise — its distance from the mean of its own independent draws — against sixteen frames
  averaged, and fails a frame noisier; beside it, each one's bias against a converged reference;
  `--strafe=150` takes the frame after the eye flew in from the side, and `--walk=150` from
  behind, which is what a history length or a filter's reach shows in; `--cut=N` takes the frame N
  frames after the cut a stop begins with, standing, which is where fireflies show; and
  `--upscale=native` standing is jittered with no upscaled resampling over it, which is where a
  temporal filter that fetches its history off the pixel shows, as a bias. Each place's
  line counts its fireflies, pixels four times over the reference, in a thousand. A run is five minutes a
  suite with the card at 99%, so an A/B is `./omw release noise --ab=<switch>`: the strafe and the
  walk legs, both sides in one run a leg (`noise --versus`), and the still leg with `--still`, which a
  switch that touches short histories still moves, since the upscaler's jitter keeps edges short.
  Narrow an A/B to one place and leg first (`--views=`, `--strafe=0 --walk=0 --still`); the suite is
  the verdict. Two runs at once only share the card.
- `./omw kernels > before.txt` ahead of a shader change and `--against=before.txt` after
  it names the kernels the change moved, per tuple of their constants; a tuple it did not name
  draws what it drew.
- `./omw repeat --pairs=10` after touching anything a frame reads: two processes walk
  `one-cell-walk` for six seconds with the upscaler and the denoiser off, the second with the queue
  held behind the host, and must agree frame for frame. The walk is always the same one, so every
  repeat compares with every other. A run is the same run twice, and a pair that finds nothing has
  found nothing. Read a difference with `--exposure=1` and `--pictures=<dir>`.
- **The denoised frame is not bit-exact on this card** (`docs/rtx/architecture.md`; notes in
  `6b3978a065`): under a busy queue, the first wavelet dispatch after a pipeline drain sometimes
  differs by an ulp on identical inputs, one level of 255 in the picture. The card's, not a missing
  barrier; a wait for idle per frame hides it. `repeat` runs unfiltered and cannot see it; a
  difference in the composed frame alone is the card's.
- Measure with `./omw release bench`, on a hot card, back to back, never with a sleep between
  runs. Take a throwaway warm-up leg first. No frame times until the renderer draws everything the
  game has.
- Measure on a quiet desktop. A bench started from this session's foreground runs under Claude
  Code's spinner, which Zed redraws and KWin composites nine times a second, and every figure
  moves with it — the host rows by half, the zone shares by a tenth, the tail by 4 ms. Start the
  run in the background and end the turn; the report's `card` lines name another process's work,
  and `--frame-times=<dir>` writes the series behind a tail. KWin's slices are not in them: a slice
  stops the queue for its switch and holds little work, and only `nsys profile --gpuctxsw=true`
  shows them. The renderer cannot outrank them: the driver refuses a high-priority queue to a
  process without `CAP_SYS_NICE`, which KWin holds. So an A/B reads medians and the p99, which a
  slice in a few frames hardly moves, and never a mean, which takes all of it: the rows' own, and
  the zones' from the record `--json` writes, since the `gpu` row is each zone's share of the mean.
- Profiling: `./omw profile` for the CPU — the measured frames alone, at `seyda-neen-ship` unless
  `--views=` or `--suite=` names another, into `build-release/perf/`: a summary by total and by self
  time, and the full reports beside it as text — by library, by source line, the callers — which
  are what to read. `--offcpu` says where it waits.
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
  saying why, the way `memory.cpp` does for the allocator. The headers GLSL reads as well,
  `components/rtx/shaders/*.h` and `components/rtxvulkan/shaders/shared/*.h`, are the one exception
  to `#pragma once`, and `portable.h` says why.
- **Include what you name.** A file that spells `std::size_t` includes `<cstddef>`. A `.cpp` may
  lean on its own header for what that header's interface already needs, and on nothing else.
- **A part that needs another part's state is handed it at the call** (`ThreadContent`, whose
  cache is handed the preprocessor at each read), and holds no reference to a sibling member or to
  its owner. An owner that does wire its parts together by reference — a renderer's passes holding
  its device — is pinned: a class with a constructor, its copy and move deleted, each member
  declared after everything it refers to. **Never an aggregate**: MSVC bound a reference that an
  aggregate's default member initializer took to a sibling to another object, inside a designated
  initializer, and the sky's first image read address nought on Windows alone.
- **The preprocessor switches only where nothing else can.** What systems spell differently goes
  in a `…posix.cpp` and `…win32.cpp` pair that CMake chooses, behind one header: a general fact in
  `components/platform` (`Platform::Process`, `Platform::SharedMemory`), the crash catcher's own
  in its `…system.hpp`. A build flag is defined in every build as `0` or `1` and read once into a
  `constexpr bool` (`Rtx::sDebugNames`, `Rtx::sValidationByDefault`), which code asks with
  `if constexpr`. Code only one build has is a file CMake chooses (`crashunsupported.cpp`), never an
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
