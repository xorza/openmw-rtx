# AGENTS.md

## What this is

A fork of OpenMW whose purpose is a **ray-traced renderer**: a 2002 game made to look
astonishing on current hardware — ray-traced visibility, path-traced indirect light, materials
recovered from pre-lit vanilla textures. Upstream OpenMW stays the host engine — cells, references,
physics, scripts, animation, weather, GUI — and stops owning the picture. Vanilla content and the
PBR replacers made for OpenMW are read as they are: what a replacer's companion maps add reaches the
trace, and a vanilla picture does not change because the renderer can read them.

Read `apps/openmw/mwrender/renderer.hpp` and its callers before changing the renderer seam, and
follow scene or resource data back to its owner before changing how the ray tracer consumes it.
What the tree, `--help` or a commit already answers does not belong here.

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
  - hashed alpha (Wyman and McGuire 2017) for the soft edges, a cut fixed to the surface in place of
    one drawn each frame: a moving frame stayed noisier than its frames averaged under the
    upscaler's jitter, and the edges converged harder, with more fireflies (`cutAt`);
  - Shader Execution Reordering (`VK_EXT_ray_tracing_invocation_reorder`): its sorting cost 17–23%
    of the trace, and it shuts out Mesa's drivers;
  - fixes to the rasterizer alone where the ray tracer already does right: its ripple field, which
    steps once a sixtieth and so slows under 60 frames a second, where the ray tracer's steps every
    frame by the time the water's clock moved (`RipplePass::record`); and its object paging, which
    copies a `NightDaySwitch`'s authored child and stands every stage a visibility gate would take
    down.

## Accepted diff

The fork's changes to upstream that stay, each for the reason given. A change to upstream code that
is not here is held to the rules above.

**The rasterizer's picture**, each moved where the ray tracer needed it and upstream's was wrong:

- the optimizer merges in child order, not address order;
- an exterior map tile keeps its land where the quad tree did not build the chunk yet;
- a `NightDaySwitch` shows its mode's child from its first frame, not the child its file opens on;
- a groundcover plant stands under its model's own transforms, as every other reference does, and
  not inside them (`GroundcoverShapes`);
- the seam's: both renderers draw at `[Video] resolution x/y` and show it scaled into the window
  with black beside it (`Misc::Presentation`), so the GUI, the projection, the pointer and Lua read
  one size whichever renderer draws. A settings file from upstream, which kept the window's size
  there, has it moved to the window on the first start.

**The rest of the tree.**

- The `[RTX]` settings pages and their translations.
- `components/crashcatcher`, replaced whole by a Crashpad monitor process, with the calls that set it
  up from the configuration, and the keeper `wrapApplication` forks first under an AppImage
  (`Crash::keepImageMounted`): the runtime unmounts the image once no process holds its keepalive
  pipe, which a monitor, outliving its client, does not.
- The fork's own `README.md`, `CI/` and workflows, and the root tooling: `CMakePresets.json`,
  `.zed/`, `omw`, `omw.cmd`, `.claude/skills/`, `.gitattributes` and `.gitignore`.
- The build the presets and Crashpad need: the raised CMake minimum, which reads the presets'
  `$comment`s, and the directory `GNUInstallDirs_get_absolute_install_dir` then takes as its third
  argument; the raised Boost minimum, whose flat maps the scene identities are; no scan for modules,
  which preprocessed every file twice for modules the tree has none of; the ccache fallback, so a
  runner without it builds; MSVC's embedded debug information, since ccache caches no compile that
  writes a shared PDB; and `$<COMPILE_LANGUAGE:C,CXX>`, which keeps the C++ flags off Crashpad's MASM.
- `install_fork_licenses` and `files/licenses/`: the licences of the third-party code the fork
  ships, which ask to be shipped with it.
- `InstallRequiredSystemLibraries` in the Windows install: the fork ships a portable folder and not
  upstream's installer, which ran the C++ runtime's redistributable.
- The visibility gates (`MWScript::VisibilityGates` and the calls that feed them): without them the
  distance stands scripted stages the game keeps down.
- The order in `~Engine`: the Lua worker joins first, and the world goes before the script manager.
  A frame the ray tracer threw out of on purpose left the worker inside its update, and the ray
  tracer's cell reader marks the visibility gates through the script manager until the world's
  renderer lets it go.
- The stamp `NifOsg::Loader` puts on an image a model carries inside it (`SceneUtil::EmbeddedImage`):
  the ray tracer keys a texture by what names it, and such an image has no file. Vanilla's
  `tx_crystal_02.nif` is one.
- `LocalMap::requestMap` asks again for a tile the renderer gave up (`OffscreenView::isAbandoned`):
  the ray tracer draws a tile frames after it is asked, and one whose scene the game left before
  then is not drawn over the wrong world.
- The lamp body marker (`SceneUtil::LampBody`) that `SceneUtil::addLight` leaves on a light's group:
  the bounce takes no glow from a lamp's own model, whose light already delivers it, and the
  rasterizer never reads it.
- The pose hook in `RenderingManager`'s intersection visitor (`Renderer::poseForIntersection`): a
  skinned body answers a CPU ray with the pose its last cull left, and the ray tracer culls no
  world, so the crosshair met every actor in its bind pose.
- `components/sky/vertexrules.hpp`, which `ModVertexAlphaVisitor` reads: both renderers fade the
  cloud shell and the star dome by one rule each.
- `SceneUtil::StateSetUpdater::getGeneration`, which `reset` bumps: the mirror applies an updater to
  a state set of its own, and a glow that `reset` ended or recoloured left the mirror's copy stale.
- `MWWorld::MoonModel::phaseEighths`, a phase continuous in game time: the ray tracer draws the moon
  down to the horizon, where the engine's discrete phase would jump a quarter in plain view.
- The port to SDL3, through the input, the GUI and the window code: the presentation reads a
  window's pixel density and display scale, which a fractionally scaled Wayland desktop sets and
  SDL2 cannot report. What the port changes:
  - SDL3 has no gamma ramp, so `[Video] gamma` is the renderers' own: the rasterizer's last draw
    (`PingPongCanvas`) and the ray tracer's tone pass. `[Video] contrast` went with the ramp: it had
    no menu control, applied on Windows alone, and the tone pass has a contrast grade of its own.
  - A controller's buttons are bound by place (`SDL_GAMEPAD_BUTTON_SOUTH`), not by Xbox's labels.
  - A Lua cursor is sized in the frame's pixels, as upstream sized it in the window's, so it is
    scaled by `Presentation::shownScale` alone.
  - Where Wayland refuses relative mouse mode and pointer warps, the pointer goes unwrapped and stops
    at the window's edge (`InputWrapper::mWarpMovesPointer`): each failed warp turned the camera.
- One set of checks for every file, on every compiler: the checks the top-level `CMakeLists.txt`
  adds to upstream's, and the hunks in upstream code that keep it clean under them, the patches to
  `extern/sol3` and `components/files/configurationmanager` included. MSVC's `4244` and `4267` are
  off, since GCC's `-Wall -Wextra` leave `-Wconversion` out.
- A number read from text is finite (`Misc::StringUtils::toNumeric`, which the settings read
  through): `std::from_chars` reads `inf` and `nan`. Where `from_chars` has no floating point, the
  stream reads only the prefix it would (`floatPrefix`), so a spelling is one number everywhere.
- What the game does in one step and the ray tracer's histories must hear: a reference the world
  moves with its physics placed outright is a jump (`World::moveObject`, `notifyJumped`), and a
  write of `GameHour` past the frame's own step is a cut (`World::noteHourWritten`).
- What the harness holds of the game through `OMW::EngineHost`, which the game alone asks nothing
  of: a sky between two weathers for one update (`WeatherManager::holdWeather`), since a filmed
  frame must name its sky; a game hour the host stated (`holdsGameClock`); a content script's
  message box logged rather than shown (`showsScriptMessageBoxes`), since a box pauses the world;
  and a saves folder of the run's own (`getSavesFolder`), since a run's world is god mode and the
  harness's scripts.
- The scripts' `math.random` seeded where the harness seeds the world's generators
  (`LuaUtil::LuaState::seedRandom`): seeded from the clock, the fish `cellhandlers.lua` spawns stood
  elsewhere in every run.
- What the ray tracer reads of the rasterizer's own state: the projection offset `SceneFrame` is
  handed beside the projection, and `Precipitation::isShown` and its occlusion setting, so neither
  renderer draws rain the other hides.
- `Renderer::support`, asked where the console, Lua, the settings window and F2 would otherwise
  toggle what does nothing under the renderer, and `ToggleBorders` under the ray tracer.
- Faults the user approved fixing:
  - `Files::LinuxPath` took a failed `read_symlink` for the executable's path (`ec.value() != -1`
    holds for every error);
  - the SDL3 port truncated a window's size over its pixel density, where `SDLUtil::windowPoints`
    rounds;
  - `cmake/FindOSGPlugins.cmake` restored `CMAKE_FIND_LIBRARY_PREFIXES` unquoted, which dropped
    MSVC's empty prefix and left every later `find_library` blind to `bz2.lib`;
  - `SDLUtil::InputWrapper` logged an unhandled event's type in hex without `std::dec` after it,
    which left every later number in the log in hex;
  - `NavMeshDb` committed each tile under a rollback journal at full synchronisation, several fsyncs
    a tile; it now journals into a write-ahead log at normal synchronisation, where a tile a crash
    loses is generated again.
- `RenderingManager::getFieldOfView`, which returned the override flag, 1°, wherever a field of
  view was overridden; and the local map's view built in double, as the ray tracer's map tile reads
  it.

## Where the code lives

**Vulkan behind an API-neutral core**, not a portability layer. A fact about Vulkan that leaks into
the core is a bug whether or not a second backend ever arrives. `docs/rtx/architecture.md` is the
shape as a reader meets it — the seam, the layers, who owns whom, the order a frame is computed in —
and the headers hold the detail.

- `components/rtx/` — the core: what the scene _is_, the light transport. No graphics API, no game
  headers.
- `components/rtxvulkan/` — the backend. What is true of an API lives here and nowhere else; the two
  places that stand one up name `VulkanRenderer` and nothing else does.
- `components/myguirtx/` — MyGUI's backend.
- `apps/openmw/mwrender/` — the seam `MWRender::Renderer`, `GlRenderer` beside upstream's files, and
  the game-side owner in `rtx/`.
- `apps/rtxtool/` — the harness: what a run visits and what a place came to in `model/`, and in
  `instruments/` what a measured run is taken with, none of which knows a world.
- The core and the backend stand in folders by responsibility, in the order `architecture.md` lists:
  a folder includes only the folders before it, and a folder inside another is a part of it.
  `RtxSourceTreeTest` holds the order, so a folder that needs a later one is a file in the wrong
  folder or a file to split.

## Verification

`./omw [flavour] <verb>` is the one way in, and `./omw help` lists the verbs and the flavours. The
flavour comes before the verb and is `debug` unless named; `release` is the build a number is quoted
from. `./omw exec ./openmw-rtxtool --help` lists the harness's options.

- Compiling is not verifying. Build the targets you touched and run the covering binary with a
  filter: `./omw test <binary> --gtest_filter=...`. `./omw lint driver` before a push that touches
  `tools/omw`.
- `./omw test` once before saying it works. The GPU binary fails without a device rather than
  skipping, so a green run means a device ran it.
- `./omw gate` once at the end. Never a gate beside a build or another gate, and never two runs at
  once: they share the card.
- Do not open the game window to check a rendering change: the harness's verbs check it, at the
  places in `files/rtx/views.cfg` and the suites in `files/rtx/benches.cfg`. `view` is for what only
  a window shows.
- `./omw shot --views=all --map --upscale=off --out=<dir>` ahead of a change and `--against=<dir>`
  after it names the pictures the change moved. Narrow an investigation to its place, and run `all`
  last. Keep the baseline outside `/tmp`, whose cleaner empties it under a long session.
  `--upscale=off`, because an upscaled picture moves with anything its history saw.
- `./omw noise` holds the frame's noise against its own frames averaged, with each place's bias
  against a converged reference and its fireflies. Its legs show different faults: `--strafe` and
  `--walk` a history length or a filter's reach, `--cut` the fireflies after a cut, and
  `--upscale=native` a temporal filter that fetches its history off the pixel. An A/B is
  `./omw release noise --ab=<switch>`, with `--still` for a switch that touches short histories,
  which the upscaler's jitter keeps at every edge. Narrow it to one place and leg first; the suite is
  the verdict.
- `./omw kernels > before.txt` ahead of a shader change and `--against=before.txt` after it names
  the kernels the change moved; a tuple of constants it did not name draws what it drew.
- `./omw repeat --pairs=10` after touching anything a frame reads: two processes walk one place with the
  upscaler and the denoiser off, the second with the queue held behind the host, and must agree
  frame for frame. A pair that finds nothing has found nothing. Read a difference with
  `--exposure=1` and `--pictures`.
- **The denoised frame is not bit-exact on this card** (`docs/rtx/architecture.md`): under a busy
  queue, the first wavelet dispatch after a pipeline drain sometimes differs by an ulp on identical
  inputs. The card's, not a missing barrier. `repeat` runs unfiltered and cannot see it; a
  difference in the composed frame alone is the card's.
- Measure with `./omw release bench`, on a hot card, back to back, never with a sleep between runs,
  after a throwaway warm-up leg; the first run after a shader change compiles the pipelines.
- Measure on a quiet desktop: start the run in the background and end the turn. In this session's
  foreground it runs under Claude Code's spinner, which Zed redraws and KWin composites, and
  every row moves. The report's `card` lines name another process's work, and `--frame-times=<dir>`
  writes the series behind a tail. KWin's own slices show only under `nsys profile --gpuctxsw=true`,
  and the renderer cannot outrank them (a high-priority queue needs `CAP_SYS_NICE`).
- An A/B reads medians and the p99, never a mean — the zones' from the record `--json` writes, since
  the `gpu` row is each zone's share of the mean.
- Profiling: `./omw profile` for the CPU, into `build-release/perf/`: read the full text reports
  beside the summary (by library, by source line, the callers); `--offcpu` says where it waits. For
  the GPU, `./omw release exec nsys profile ./openmw-rtxtool bench ...`.

## Conventions

**C++20, `.clang-format` at 120 columns.** The user's global Rust rules do not apply to this tree;
the posture behind them does.

- **`#pragma once`, and includes in five blocks** a blank line apart: the file's own header, the C++
  standard library, `<gtest/...>`, other libraries, `<components/...>` and `<apps/...>`, then quoted
  local headers — of the file's own folder only, and any other folder is spelled from the root.
  `.clang-format` sorts inside each block and keeps the blocks. A conditional `#include` goes last,
  and a block out of order carries the comment saying why, the way `memory.cpp` does for the
  allocator. The headers GLSL reads as well (`components/rtx/shaders/*.h`,
  `components/rtxvulkan/shaders/shared/*.h`) are the one exception to `#pragma once`, and
  `portable.h` says why.
- **Include what you name.** A `.cpp` may lean on its own header for what that header's interface
  already needs, and on nothing else.
- **A part that needs another part's state is handed it at the call** (`ThreadContent`, whose cache
  is handed the preprocessor at each read), and holds no reference to a sibling member or to its
  owner. An owner that does wire its parts together by reference is pinned: a class with a
  constructor, its copy and move deleted, each member declared after everything it refers to.
  **Never an aggregate**: MSVC bound a reference that an aggregate's default member initializer
  took to a sibling to another object inside a designated initializer.
- **The preprocessor switches only where nothing else can.** What systems spell differently goes in
  a `…posix.cpp` and `…win32.cpp` pair that CMake chooses, behind one header: a general fact in
  `components/platform` (`Platform::Process`), the crash catcher's own in its `…system.hpp`. A build
  flag is defined in every build as `0` or `1` and read once into a `constexpr bool`
  (`Rtx::sDebugNames`), which code asks with `if constexpr`. Code only one build has is a file CMake
  chooses (`crashunsupported.cpp`), never an `#ifdef` around it. A test of an `assert` calls
  `Testing::expectAssertDies`. What stays: a chain inside the one file that owns a system's
  difference, an include only one system has, and the headers GLSL and C++ both read, whose
  differences `shaders/portable.h` holds.
- **Comments say _why_**: an invariant, a workaround and its cause, a trade-off against the obvious
  alternative — never a restatement of the line under it. No decorative dividers. Stale narration
  in code you are editing is fixed with it; sweeping other files is a separate task.
- **Frame times are uniform**, and an average that hides a spike is not an answer. Work is
  _incremental_, never _batched behind a threshold_: a table recycles its slots, a resource is
  appended rather than rebuilt. What cannot be made cheap belongs off the frame path entirely, not
  on a rota. Report the p99 and the worst frame beside the median.
- **Allocation is a metric on the frame path and at load.** Persistent scratch buffers refilled with
  `clear()`, results into an out-parameter, no `std::string` or `std::function` per frame, logging
  that compiles out; a test enforces it. A loader is a persistent object owning its buffers,
  `clear()`ed and refilled for each thing it reads: cells arrive while the game is running, so a
  spike taken at load is a spike a player feels.
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
