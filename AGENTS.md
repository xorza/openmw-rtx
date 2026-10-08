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
  is not made, and one already made is reverted. A fault the rasterizer alone has is not proposed:
  a fix to upstream's code changes what the ray tracer does as well. The diff stays small, but never
  at the cost of reuse or of the abstraction's quality.
- Both renderers stand behind one interface that exposes no implementation detail. Where the game
  would branch on which renderer it has, the seam abstracts the question instead. One binary ships
  both, and the one not chosen never starts.
- Target hardware: NVIDIA RTX 20 series and later, and AMD RDNA 2 and later. Only the NVIDIA card
  here runs anything; an AMD device is stood up under Mesa's drm-shim (`~/Projects/mesa/build-shim`),
  which compiles every kernel and executes none.
- Tried, declined, and not to be proposed again:
  - opacity micromaps (`VK_EXT_opacity_micromap`) for the cutouts: no faster, and longer loading;
  - async compute (the next trace on a second queue beside this frame's reconstruction): 0.1–0.2 ms
    gained (the branch `async` has the record);
  - Shader Execution Reordering (`VK_EXT_ray_tracing_invocation_reorder`): its sorting cost 17–23%
    of the trace, and it shuts out Mesa's drivers;
  - in the rasterizer alone, which the ray tracer already does right: the ripple field that loses
    its sixtieths under 60 frames a second, and the object paging that copies a `NightDaySwitch`'s
    authored child and stands every stage a visibility gate would take down.

## Accepted diff

The fork's changes to upstream that stay, each for the reason given. A change to upstream code
that is not here is held to the rules above.

**The rasterizer's picture.** Four changes the ray tracer needs move it, each where upstream's was
wrong:

- the optimizer merges in child order, not address order;
- an exterior map tile keeps its land where the quad tree did not build the chunk yet;
- a `NightDaySwitch` shows its mode's child from its first frame, not the child its file opens on;
- a groundcover plant stands under its model's own transforms, as every other reference does, and
  not inside them (`GroundcoverShapes`).

A fifth is the seam's: both renderers draw their frame at `[Video] resolution x/y` and show it
scaled into the window with black beside it (`Misc::Presentation`), so the GUI, the projection, the
pointer and Lua read one size whichever renderer draws. A settings file from upstream, which kept
the window's size there, has it moved to the window on the first start, and the launcher and
`settings-default.cfg` say what each setting now sets.

**The rest of the tree.**

- The `[RTX]` settings pages and their translations.
- `components/crashcatcher`: upstream's crash catcher is replaced whole by the fork's own, a
  Crashpad monitor process, with the calls that set it up from the configuration
  (`Debug::setCrashReports`, the hang limit and the version), and the keeper `wrapApplication`
  forks first under an AppImage (`Crash::keepImageMounted`): the runtime unmounts the image once no
  process holds its keepalive pipe, which a monitor, outliving its client, does not.
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
  SDL2 cannot report. What the port changes:
  - SDL3 has no gamma ramp, so `[Video] gamma` is the renderers' own: the rasterizer applies it in
    its last draw into the frame (`PingPongCanvas`), the ray tracer in its tone pass.
    `[Video] contrast` went with the ramp and is not restored: it had no menu control, upstream
    applied it on Windows alone, and the tone pass has a contrast grade of its own.
  - A controller's buttons are bound by place (`SDL_GAMEPAD_BUTTON_SOUTH`), not by Xbox's labels,
    so a pad with other labels is played with the same thumb.
  - A Lua cursor is sized in the frame's pixels, as upstream sized it in the window's, so it is
    scaled by `Presentation::shownScale` alone, not by the interface's scaling as well.
  - Wayland lets no window warp the pointer, so where relative mouse mode is refused, the pointer
    goes unwrapped and stops at the window's edge (`InputWrapper::mWarpMovesPointer`): there, the
    way back from each of upstream's warps turned the camera.
- One set of checks for every file, on every compiler: the five checks the top-level
  `CMakeLists.txt` adds to upstream's, and the hunks in upstream code that keep it clean under them,
  the patches to `extern/sol3` and `components/files/configurationmanager` included. MSVC's `4244`
  and `4267` are off, since GCC's `-Wall -Wextra` leave `-Wconversion` out.
- A number read from text is finite (`Misc::StringUtils::toNumeric`, which the settings read
  through): `std::from_chars` reads `inf` and `nan`, and no sanitizer stopped either reaching
  the picture. Where `from_chars` has no floating point, the stream reads only the prefix it would
  (`floatPrefix`), so a spelling is one number on every toolchain.
- What the game does in one step and the ray tracer's histories must hear: a reference the world
  moves with its physics placed outright is told as a jump (`World::moveObject`'s `jumps`,
  `RenderingManager::notifyJumped`), and a write of `GameHour` that moves the clock by more than the
  frame's own step is a cut (`World::noteHourWritten`, `DateTimeManager::jumps`).
- What the harness holds of the game through `OMW::EngineHost`, which the game alone asks nothing
  of: a sky stood part of the way between two weathers for one update
  (`WeatherManager::holdWeather`), since the game's crossing runs on a clock of its own and a filmed
  frame must name its sky; a game hour the host stated left where it put it (`holdsGameClock`), the
  clock and not its time scale; and a content script's message box logged rather than shown
  (`WindowManager::scriptMessageBox`, `showsScriptMessageBoxes`), since a box pauses the world and a
  measured run does not stop for an answer.
- The scripts' `math.random` seeded where the harness seeds the world's generators
  (`LuaUtil::LuaState::seedRandom`, `MWBase::LuaManager::seedRandom`): the engine seeds it from
  the clock, and the fish `cellhandlers.lua` spawns in an exterior met for the first time stood
  elsewhere in every run, and drew another list of the world's levelled creatures after them.
- What the ray tracer reads of the rasterizer's own state: the projection offset `SceneFrame` is
  handed beside the projection, and `Precipitation::isShown` and its occlusion setting, so neither
  renderer draws rain the other hides.
- The renderer's answer to what it declines (`Renderer::support`), asked where the console, Lua and
  the settings window would otherwise toggle what does nothing under it, and `ToggleBorders` under
  the ray tracer.
- Four faults the user approved fixing: `Files::LinuxPath` took a failed `read_symlink` for the
  executable's path (`ec.value() != -1` holds for every error), the SDL3 port truncated a
  window's size over its pixel density where `SDLUtil::windowPoints` rounds,
  `cmake/FindOSGPlugins.cmake` restored `CMAKE_FIND_LIBRARY_PREFIXES` unquoted, which dropped
  MSVC's empty prefix and left every later `find_library` blind to `bz2.lib`, and
  `SDLUtil::InputWrapper` logged an unhandled event's type in hex without `std::dec` after it,
  which left every later number the log printed in hex.
- `RenderingManager::getFieldOfView`, which returned the override flag, 1°, wherever a field of
  view was overridden; and the local map's view built in double, as the ray tracer's map tile reads
  it.
- A look of the ray tracer's own against upstream's: its ripple field steps every frame by the time
  the water's clock moved (`RipplePass::record`), where upstream's steps once a sixtieth: the same
  springs over the same time, at a cost every frame pays alike, and a wake that keeps its pace under
  sixty frames a second.

## Where the code lives

**Vulkan behind an API-neutral core**, not a portability layer. A fact about Vulkan that leaks into
the core is a bug whether or not a second backend ever arrives.

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

- `./omw` at the root is the one way in, `omw [flavour] <verb>`, and `./omw help` lists both. The
  flavour comes before the verb (`omw release build`, not `omw build release`) and is `debug`
  unless named: every assert and the tests. `release` is the build a number is quoted from, and
  `profile` runs in it and refuses another flavour. `asan` adds the address and undefined-behaviour
  sanitizers and `tsan` the thread sanitizer, which the daily run builds apart; `full` builds every
  program the tree has, the CS among them; `package` is the one `archive` puts into `dist/`.
- Compiling is not verifying. Build the targets you touched and run the covering binary with a
  filter: `./omw test <binary> --gtest_filter=...`. `./omw build` formats the tree first, except on
  CI, whose checks job checks it once; `./omw format` rewrites the tree alone, and
  `./omw format --check` changes nothing and is what the gate and CI run.
- `./omw test` once before saying it works: every suite CTest has, `rtx-gpu-tests` and the crash
  matrix among them. The GPU binary fails without a device rather than skipping, so a green run
  means a device ran it; `--without-device` leaves it out on a box with no driver.
- `./omw gate` once at the end: the steps `./omw help` lists, from `tools/omw/gate.py`, stopping
  at the first failure. Never a gate beside a build or another gate, and never two runs at once:
  they only share the card.
- Do not open the game window to check a rendering change. The harness's verbs go through the
  driver, which builds `openmw-rtxtool` and runs it in the flavour's directory:
  `./omw [flavour] info|scene|shot|view|bench|check|film|noise`, and `./omw exec ./openmw-rtxtool --help`
  for their options. The places are `files/rtx/views.cfg`, the suites `files/rtx/benches.cfg`.
  `scene` reports what the renderer was handed, `check` asserts the tree's claims at every place of
  its suite, `bench` has the moving camera, `view` is for what only a window shows, and `film` flies
  through the keys `view --keys` wrote.
- `./omw shot --views=all --map --upscale=off --out=<dir>` ahead of a change and `--against=<dir>`
  after it names the pictures the change moved: 56 s for every place, 9 s for one, so an
  investigation narrows to its place and `all` is the last run. Keep the baseline outside `/tmp`,
  whose cleaner empties it under a long session. `--upscale=off`, because the harness upscales at
  `quality` unless told, and an upscaled picture moves with anything its history saw.
- `./omw noise` holds the frame's noise — its distance from the mean of its own independent draws —
  against sixteen frames averaged, and fails a noisier frame; beside it, each place's bias against a
  converged reference and its fireflies (pixels four times over the reference) in a thousand. Its
  legs show different faults: `--strafe=150` and `--walk=150` a history length or a filter's reach,
  `--cut=N` the fireflies after a cut, and `--upscale=native` standing a temporal filter that fetches
  its history off the pixel. A suite is five minutes with the card at 99%, so an A/B is
  `./omw release noise --ab=<switch>`, both sides in one run a leg (`noise --versus`), with `--still` for a switch that touches short histories,
  which the upscaler's jitter keeps at every edge. Narrow it to one place and leg first
  (`--views=`, `--strafe=0 --walk=0 --still`); the suite is the verdict.
- `./omw kernels > before.txt` ahead of a shader change and `--against=before.txt` after it names
  the kernels the change moved, per tuple of their constants; a tuple it did not name draws what it
  drew.
- `./omw repeat --pairs=10` after touching anything a frame reads: two processes walk
  `one-cell-walk` for six seconds with the upscaler and the denoiser off, the second with the queue
  held behind the host, and must agree frame for frame. A pair that finds nothing has found nothing.
  Read a difference with `--exposure=1` and `--pictures`.
- **The denoised frame is not bit-exact on this card** (`docs/rtx/architecture.md`; notes in
  `6b3978a065`): under a busy queue, the first wavelet dispatch after a pipeline drain sometimes
  differs by an ulp on identical inputs, one level of 255. The card's, not a missing barrier.
  `repeat` runs unfiltered and cannot see it; a difference in the composed frame alone is the card's.
- Measure with `./omw release bench`, on a hot card, back to back, never with a sleep between runs,
  after a throwaway warm-up leg; the first run after a shader change compiles the pipelines. No
  frame times until the renderer draws everything the game has.
- Measure on a quiet desktop: start the run in the background and end the turn. In this session's
  foreground it runs under Claude Code's spinner, which Zed redraws and KWin composites nine times a
  second — the host rows move by half, the zone shares by a tenth, the tail by 4 ms. The report's
  `card` lines name another process's work, and `--frame-times=<dir>` writes the series behind a
  tail. KWin's own slices show only under `nsys profile --gpuctxsw=true`, and the renderer cannot
  outrank them (a high-priority queue needs `CAP_SYS_NICE`). So an A/B reads medians and the p99,
  never a mean — the zones' from the record `--json` writes, since the `gpu` row is each zone's
  share of the mean.
- Profiling: `./omw profile` for the CPU — the measured frames alone, at `seyda-neen-ship` unless
  `--views=` or `--suite=` names another, into `build-release/perf/`: a summary by total and by self
  time, and the full text reports beside it (by library, by source line, the callers), which are
  what to read. `--offcpu` says where it waits. For the GPU,
  `./omw release exec nsys profile ./openmw-rtxtool bench ...`; `ncu` is not installed.
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
  `#ifdef` around it. A test of an `assert` calls `Testing::expectAssertDies`, not `#ifndef NDEBUG`. What stays: a chain inside the one file that
  owns a system's difference (Linux beside macOS in a POSIX file), an include only one system has,
  and the headers GLSL and C++ both read, whose differences `shaders/portable.h` holds.
- **Comments say _why_**: an invariant, a workaround and its cause, a trade-off against the obvious
  alternative — never a restatement of the line under it. No decorative dividers. Stale narration
  in code you are editing is fixed with it; sweeping other files is a separate task.
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
- **Nothing is computed twice, and everything as early as it can be** — at initialization or at
  load. A frame reads what it was handed.
- **A shader's float arithmetic is the build's, not the driver's.** Every module is pinned
  (`components/rtxvulkan/spirv/spirvpin.hpp`), so GLSL is written as usual; an operation the pinning
  refuses stops the build, and the message says what it is. `precise` is for a value two shaders
  must compute to the bit.
- **One path through a shader.** A single computation covering every case beats a tree that skips
  work per lane: a factor of zero, a table lookup, a value selected without a jump. Divergence needs
  a measurement saying the branch pays for itself, named where the branch lands.
- **Asserts** guard contracts the code must keep, not data the world might supply. Hot paths use the
  debug-only form; untrusted input is never an assert.
