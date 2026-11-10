# Upstream changes

The fork's changes to upstream code that stay, each approved by the user and kept for the reason
given. A change to upstream code that is not here is held to `AGENTS.md`'s first rule: proposed, and
made only on the user's explicit yes, after which it is added here.

**The rasterizer's picture**, each moved where the ray tracer needed it and upstream's was wrong:

- the optimizer merges in child order, not address order;
- an exterior map tile keeps its land where the quad tree did not build the chunk yet;
- a groundcover plant stands under its model's own transforms, as every other reference does, and
  not inside them (`GroundcoverShapes`);
- the seam's: both renderers draw at `[Video] resolution x/y` and show it scaled into the window
  with black beside it (`Misc::Presentation`), so the GUI, the projection, the pointer and Lua read
  one size whichever renderer draws. A settings file from upstream, which kept the window's size
  there, has it moved to the window on the first start.

**The rest of the tree.**

- The `[RTX]` settings pages and their translations, and a settings window and a launcher that offer
  only what the ray tracer honours (`RtxSupportTest`): the rasterizer's pages and controls are
  removed with their strings, and the launcher's Distant Land is Distant Objects, `object paging`
  alone. The keys stay in `settings-default.cfg`, which the rasterizer's code reads.
- `components/crashcatcher`, replaced whole by a Crashpad monitor process, with the calls that set it
  up from the configuration, and the keeper `wrapApplication` forks first under an AppImage
  (`Crash::keepImageMounted`): the runtime unmounts the image once no process holds its keepalive
  pipe, which a monitor, outliving its client, does not. And each line of the log written in one
  write, its stamp with it (`DebugOutputBase::write`): the monitor appends its summaries to the log
  from a process of its own, and one that landed between a stamp and its text split the line.
- The fork's own `README.md`, `CI/` and workflows, and the root tooling: `CMakePresets.json`,
  `.clangd`, `.zed/`, `omw`, `omw.cmd`, `.claude/skills/`, `.gitattributes` and `.gitignore`.
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
- The `SceneUtil::StableIdentity` stamps `MWRender::Objects` puts on every cell root and every
  reference root, through one maker of a cell root (`Objects::cellRootOf`): the mirror tells roots
  apart by the stamp and not by their address or their place under the scene root.
- The lamp body marker (`SceneUtil::LampBody`) that `SceneUtil::addLight` leaves on a light's group:
  the bounce takes no glow from a lamp's own model, whose light already delivers it, and the
  rasterizer never reads it.
- The pose hook in `RenderingManager`'s intersection visitor (`Renderer::poseForIntersection`): a
  skinned body answers a CPU ray with the pose its last cull left, and the ray tracer culls no
  world, so the crosshair met every actor in its bind pose. And `SceneUtil::RigGeometry::hasSkeleton`, which the
  hook asks: a part attached in a frame's logic binds its skeleton in that frame's update, and
  posed before it the rig logged that it had none.
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
  - A window that is hidden, minimized or occluded draws nothing and pauses, as upstream pauses a
    minimized one, read from SDL's window flags and not from its events (`SDLUtil::isWindowShown`):
    KWin minimizes a window by suspending it, which SDL reports as occluded and never as minimized,
    so the game drew on behind it. KWin suspends a window minimized, on another desktop or behind a
    locked screen, and not one another window covers (KDE bug 492924).
  - `[Video] window width/height` hold the window in the desktop's unit, `SDL_GetWindowSize`'s, and
    a resize stores what the window reports in it (`WindowListener::windowSized`): a size in pixels
    went through two densities on a Wayland desktop whose displays differ in scale, and shrank at
    every start. The harness states its window in pixels (`RunSetup::mWindowPixels`).
- One set of checks for every file, on every compiler: the checks the top-level `CMakeLists.txt`
  adds to upstream's, and the hunks in upstream code that keep it clean under them, the patches to
  `extern/sol3` and `components/files/configurationmanager` included. MSVC's `4244` and `4267` are
  off, since GCC's `-Wall -Wextra` leave `-Wconversion` out.
- A number read from text is finite (`Misc::StringUtils::toNumeric`, which the settings read
  through): `std::from_chars` reads `inf` and `nan`. Where `from_chars` has no floating point, the
  stream reads only the prefix it would (`floatPrefix`), so a spelling is one number everywhere.
- The test support the fork's suites run under (`components/testing/util.hpp`,
  `apps/components_tests/main.cpp`): death tests in the `threadsafe` style, since ThreadSanitizer
  refuses a forked child of a process with threads; a run's output folder keyed on the process as
  well as the clock, since CTest starts the suites and the GPU shards together; each test's folder
  emptied at its start (`FreshTestDirs`), so a repeated pass starts from nothing; and upstream's two
  Lua storage tests (`lua/teststorage.cpp`) writing there and not under a fixed name in the temp
  directory.
- What the game does in one step and the ray tracer's histories must hear: a reference the world
  moves with its physics placed outright is a jump (`World::moveObject`, `notifyJumped`), and a
  move of the clock past the frame's own step is a cut, found once a frame where the weather is
  updated (`MWWorld::HourWatch`): a Lua global script writes the hour from the Lua worker while the
  frame draws.
- What the harness holds of the game through `OMW::EngineHost`, which the game alone asks nothing
  of: a sky between two weathers for one update (`WeatherManager::holdWeather`), since a filmed
  frame must name its sky; a game hour the host stated (`holdsGameClock`); a content script's
  message box logged rather than shown (`showsScriptMessageBoxes`), since a box pauses the world;
  and a saves folder of the run's own (`getSavesFolder`), since a run's world is god mode and the
  harness's scripts.
- The scripts' `math.random` seeded where the harness seeds the world's generators
  (`LuaUtil::LuaState::seedRandom`): seeded from the clock, the fish `cellhandlers.lua` spawns stood
  elsewhere in every run.
- What the loading screen's preparation reads of the content (`Rtx::ContentPreparation`, run inside
  `Renderer::awaitShaders`): `ESMTerrain::Storage::getLayerInfo` made public and `Store<ESM::LandTexture>`'s
  `begin` and `end`, which list the ground layers as the ground reader finds them (`MWRender::contentLayers`), and
  the `PreparingContent` label in each of the engine's six languages. And the note the loading screen shows above
  its label while the preparation runs (`Loading::Listener::setNote`, `PreparingContentNote`): a cold start with a
  large mod list waits minutes there, and a player told nothing takes it for a hang.
- What the ray tracer reads of the rasterizer's own state: the projection offset `SceneFrame` is
  handed beside the projection, and `Precipitation::isShown` and its occlusion setting, so neither
  renderer draws rain the other hides.
- `Renderer::support`, asked where the console, Lua and F2 would otherwise
  toggle what does nothing under the renderer, and `ToggleBorders` under the ray tracer.
- A Light bolt that strikes no actor stands its light off the surface it struck, for the effect's
  duration (`files/data-mw/scripts/omw/surfacelight/`), as the user asked: vanilla lit only the
  actor a bolt struck, and a bolt that struck a wall lit nothing. Built-in scripts, so it is on in
  every game, through upstream's own projectile hit handler and no engine code.
- The launcher's DLSS download, into the folder `getUserLocalDataPath` names on each of the five
  path classes: NVIDIA's feature library is the player's to install, and a per-user folder is one
  the game may write to on every system.
- The sky's clocks in `components/sky` (the time of day, the clouds' scroll, the stars' roll) and
  `SkyManager` split from `Precipitation`: the ray tracer lights an hour and draws the rain with no
  rasterizer standing, and each reads the one rule the rasterizer reads.
- The small hooks the mirror reads the game's state through, each an accessor or a stamp of state
  upstream already keeps: `Terrain::World::getActiveGrid`; `LightController`'s type and colour and
  `LightSource`'s controller, source radius and const light; `RigGeometry`'s skeleton, influences,
  bones and skin matrix; `Skeleton::markReached`, which the mirror's pose walk stamps as a cull would;
  `PaintedTexture`, `ImageRegion` and `OffscreenFraming`, a map tile's picture as both renderers paint
  it; `Animation`'s glow fade and the glow light's source radius; `World::getStormDirection`; and the
  terrain's height solved in double, so the ray tracer's ground and the game's agree on every build of
  OSG.
- `components/settings` naming the core's `Upscale` and `MAX_BOUNCE_RAYS`, which its `[RTX]` category
  stores and sanitizes.
- Faults the user approved fixing:
  - `Files::LinuxPath` took a failed `read_symlink` for the executable's path (`ec.value() != -1`
    holds for every error);
  - `SettingsFileParser::saveSettingsFile` and `StateManager::saveGame` wrote `settings.cfg` and a save in place,
    so a crash or a full disk during the write left a part of either: both now write through
    `Platform::writeWhole`, synced, which the platform's I/O library holds (`openmw-platform-io`);
  - the SDL3 port truncated a window's size over its pixel density, where `SDLUtil::windowPoints`
    rounds;
  - `SDLUtil::InputWrapper` logged an unhandled event's type in hex without `std::dec` after it,
    which left every later number in the log in hex;
  - `openmw_settings_window.layout` gave the `ScriptBox` widget its `name` attribute twice, which an
    XML parser refuses;
  - `Debug::wrapApplication`'s fatal error box read only whether a terminal started the game, not
    `OPENMW_CRASH_DIALOG` as the crash dialog does, and showed before the error was logged: a start
    that failed under a harness waited for a click, with nothing in the log.
  - `Resource::SceneManager` handed OSG's COLLADA reader a model as a stream, which the reader copies
    into a buffer of its length and parses as a C string, on into whatever the heap held past it: which
    models loaded followed the order the game read them in, and a renderer that read in another order
    lost other statics, and four of upstream's integration tests the navigation mesh they stand on. A
    nought now ends the bytes (`aColladaModelLoadsWhateverWasReadBeforeIt`).
  - the moon phase a script reads (`GetMasserPhase`, `GetSecundaPhase`) before the weather ran
    outdoors: upstream gave a new moon until the sky dome existed, so a game started in the prison
    ship or a save loaded indoors read nought; it is now the date's phase, and the last exterior's
    after that, as vanilla answers indoors (`WeatherManager::getMoonPhase`);
  - `Resource::SceneManager` deep-copied a COLLADA rig's state set down to its textures' images, and then
    put the source's own state set on the copy: on Windows, whose OpenSceneGraph counts a block-compressed
    image with no chain at four bytes a pixel, the image's copy read past its buffer and ended the game as
    it loaded the example suite's player body. The rig is copied with its state set shared;
  - the void reset in `Scene::playerMoved` measured against a point every interior lowered and only
    `Scene::clear` started over, so a visit to a deep interior left the player under a shallower one's
    floor; and no collision object moved, rotated, scaled or removed after it was added changed that
    point, so a lowered platform reset a player standing on it. `MWWorld::LowestPoints` holds the point
    of each object in the active interior, which `World::collisionObjectChanged` keeps, and
    `PhysicsSystem::updateScale` scales an object at once, since a shape holds its own scale and the
    bounds are read next (`void reset in an interior reads that interior alone`, `void reset follows a
    collision object after it was added`);
  - the cell preloader was never asked for what inserting an actor builds: `Npc::getModelsToPreload` now
    lists the worn parts of an NPC the game never met out of its record's inventory (`MWClass::appendRecordWornParts`,
    `anUnmetNpcListsThePartsItsRecordWears`), and `CreatureLevList::getModelsToPreload` every creature the list may
    spawn, which made up to 57 of the 293 ms of a crossing's slow frames on a large content list;
  - the navmesh updater's queue took a delayed job only once no other update was queued, and moved due ones by
    their distance to the player only when the player's tile changed: a tile near the player changed twice within
    `min update interval ms` waited behind every farther tile, the whole navmesh after a new game on a slow
    machine, so a placed object stayed out of the mesh for half a minute. `JobQueue::pop` now ranks a due delayed
    job with the rest (`pop_should_return_ready_delayed_nearer_to_player_before_farther_updating`);
  - the launcher showed the language `launcher.cfg` named, or English where it named none, and wrote that
    language's encoding over the one `openmw.cfg` held at every Play: a Russian profile's first Play turned
    `win1251` into `win1252`, and the game drew its Cyrillic as Latin-1. The box now shows a language that
    reads the game's encoding (`Config::languageForEncoding`), and the launcher and the wizard read one
    table of a language's encoding (`Config::encodingOfLanguage`);
  - `Scene::addPostponedPhysicsObjects` gave a script's placed object its physics and not its place in the
    navigation mesh, so actors planned their paths through it until its cell loaded again. It now takes the
    one route every other collision object takes (`addObject`), which the suite `test_script_placed` holds.
- `Resource::SceneManager::letGo` and `GenericResourceManager::removeFromCache`: the ray tracer's preparation has
  the loader's caches let go of a model or an image as the last job reading it ends, its template, its NIF file,
  its image and the shared state no template uses (`docs/rtx/architecture.md`, "The preparation's memory is
  dispatched"), where clearing them once a second held a second of walks.
- The interface's fonts rasterized again while the game runs (`docs/rtx/architecture.md`, "A live
  scale and font size"): `Gui::FontLoader` keeps the TrueType fonts it makes and rasterizes them in
  place (`FontLoader::fit`), each the one way, the load's included, so a code point a face lacks
  draws the font's stated substitute after the load as after a fit; each font file's bytes are held
  in `MyGUIPlatform::DataManager::hold`, so a rasterization reads nothing and sets no resource path;
  `ScalingLayer::getScaleFactor` takes a stated view; and every text sub-widget is the fork's
  (`Gui::FontUsers`, `Gui::TrackedText`), standing in MyGUI's factories, so a rasterization reaches
  every text, and a text with no height of its own takes `[GUI] font size` rather than its font's
  raster height. A book page is a font user too, which drops its book on a refit and has its owner
  typeset it again (`BookPage::adviseRetypeset`): the dialogue history through `updateHistory`, and
  the journal through what it now keeps of how each book on its stack was typeset.
  `Gui::InterfaceFonts` owns the fonts and both registries and refits them as one, which
  `WindowManager::layOut` asks of it at every layout (`WindowManager::fitFonts`), in place of the
  raster the constructor fixed. What else held the scale follows it: `MouseManager` keeps the cursor
  in the frame's pixels, every window is held inside the view after a layout, the local map
  describes a tile at the raster of the moment and draws the player's cell again where it moved, and
  the inventory places itself and sizes its preview on a layout. `[GUI] scaling factor` applies
  live, from a slider in the Video page that writes it where the player leaves it
  (`Gui::SettlingScrollBar`).
- `[GUI] font size` held by the fonts' owner and read from it alone (the same section): the fit is
  the setting's one reader (`FontFit::mFontSize`), and every size the interface computes from it
  asks `MWBase::WindowManager::getFontSize`, which a source scan holds to. A fit to another size
  sets the fonts at it in place and has every text, list and page follow: a text with no height of
  its own takes its registry's (`FontUsers::getTextHeight`), so `FontWrapper`, which stamped the
  size into each widget as a height of its own, is gone, and four files that took `Settings` through
  it include it; every list is the fork's (`Gui::ListBox`), standing in MyGUI's factory, whose lines
  stand a line of text high and follow, in place of the line height the font loader wrote into every
  skin as it loaded; a book is typeset at a stated size and drawn at it (`BookTypesetter::create`);
  and a window that keeps rows sized by it lays them out again (`WindowBase::refitText`), the
  character creation's dialogs included. `omw/mwui`'s templates follow through an internal handler
  (`_onFontSizeChanged`), and the setting applies live, from a settling slider in the Video page
  under the interface scale.
- The auto normal and specular maps upstream's shader visitor added, moved into one helper both
  renderers read (`components/shader/automaps`): in the `recreateShaders` path upstream put the
  specular map on the normal map's unit, which lost the normal map, and took unit 7, which holds
  the tangents it writes, as a fallback for unit 0's coordinates. Both faults are gone from both.
- `RenderingManager::getFieldOfView`, which returned the override flag, 1°, wherever a field of
  view was overridden; and the local map's view built in double, as the ray tracer's map tile reads
  it.
- The navigator's tiles in the content store (`DetourNavigator::NavMeshTileStore`), and SQLite out of the build: the
  fork keeps what it derives from the content in one store, every profile shares the tiles they share, and the store's
  key covers the code by a version a test holds (`DetourNavigatorTileVersionTest`). `openmw-navmeshtool` writes the
  store the game reads, and loses `--remove-unused-tiles` and the vacuum; the launcher loses their controls, and
  `[Navigator]` loses `write to navmeshdb` and `max navmeshdb file size`. The cost is a conflict with every upstream
  change to the navigator's database worker, its input serialization or `openmw-navmeshtool`. Recast is the
  commit `extern/` pins on every system, its headers included as a system's, since the tile version cannot
  follow a distribution's build.
- GitLab's pipeline, macOS and Android, out of the tree: the fork's CI is GitHub's and it builds
  Linux and Windows alone. `.gitlab-ci.yml`, `.gitlab/`, every `CI/` script only that pipeline ran,
  `docker/generic-linux`, and the groups of `CI/install_debian_deps.sh` only it installed go;
  `omw integration` pins the example suite's revision itself (`EXAMPLE_SUITE_REVISION`), which
  `upstream.yml` copies from upstream's pipeline at each merge. macOS's bundle, its files under
  `files/mac`, `cmake/SignMacApplications.cmake`, `Files::MacOsPath`, and every `APPLE`, `__APPLE__`
  and `Q_OS_MAC` branch go; so do Android's entry (`androidmain.cpp`), `Files::AndroidPath`, its
  ICU cross-build, gl4es's manual start (`OPENMW_GL4ES_MANUAL_INIT`) with the GLES context it asked
  for, and every `ANDROID` branch. `upstream.yml` keeps every deleted path deleted, so an upstream
  change to one holds no merge; the cost is a conflict with every upstream change inside a macOS or
  Android branch of a file the fork keeps. What only those builds and GitLab's generic Linux build
  used goes with them: OpenSceneGraph, MyGUI, Bullet and ICU built from source, every static link
  of them (`OSG_STATIC`, `MYGUI_STATIC`, `BULLET_STATIC`, `QT_STATIC`, `cmake/WholeArchive.cmake`),
  so each comes from the system on Linux and from vcpkg on Windows; and the FreeBSD, OpenBSD and
  MinGW branches. Upstream's user manual (`docs/source`) keeps its macOS
  and Android sections.
- CMake 4.4 at least (`cmake_minimum_required`), and every check for an older one gone: `omw`
  builds with Kitware's 4.4.5 on every system, which GitHub's runners and Ubuntu do not carry. The
  cost is a conflict with an upstream change to the lines around the checks.
- clang-format 23.1.5 in place of 14, with upstream's `.clang-format`, over upstream's sources as over
  the fork's, with C++ LF on every clone: one formatter for one tree, the same on every system. The
  cost is a reformat of 294 sources where 23 lays upstream's style out otherwise, and a table written
  in rows that loses its trailing comma, which 23 reads as one element a line; `omw merge-file` takes
  the layout out of each merge, as the merge workflow lays out the base and both sides with the pinned
  clang-format before git merges them.
- Upstream's project files and scripts the fork has no use for: `.resubmitted_merge_requests.txt`
  (GitLab's bookkeeping), `.readthedocs.yaml` (upstream's published manual), `CONTRIBUTING.md`
  (upstream's merge requests; the fork's rules are `AGENTS.md`), and under `scripts/` the Teal
  declarations and preprocessed-size scripts only GitLab's pipeline ran, with `osg_stats.py` and
  `HOWTO-benchmark.md` for the rasterizer's OSG statistics, where `omw bench` measures the ray tracer.
  `upstream.yml` keeps them deleted. `.editorconfig` covers the ray tracer's shaders and the headers
  GLSL and C++ share (`.h`, `.rgen`), LF as `.gitattributes` holds C++, and loses the stages the tree has
  no file of.
- Ubuntu 26.04 for every Linux build in place of 24.04, CI's runners, the release image and the docs
  image alike, from one package script, `CI/install_ubuntu_deps.sh`, with `install_debian_deps.sh`
  folded into it: one build system, with GCC 15.2, and GCC 15's false `-Wmaybe-uninitialized` off as
  14's was. The cost is the AppImage's floor, glibc 2.43, and a conflict with upstream's edits to the
  package lists.
- `components/CMakeLists.txt` as `openmw-components` and `openmw-components-qt`, each folder's files
  named in a `target_sources` list and its headers as a file set, in place of the stem macros
  (`add_component_dir`, `add_component_qt_dir`) and the hand-made unity build (`enable_unity_build`,
  `OPENMW_UNITY_BUILD`), with no unity build in their place; without upstream's nested
  `project (Components)`; the platform folder's lists in `components/platform/CMakeLists.txt`, for Windows
  and Linux alone, so upstream's other Unix and `filestdio.cpp` branches go; every app linking the new names. The cost is a conflict with every upstream change to a component's list,
  which the merge resolves by naming the file in its folder's list.
- Every app's `CMakeLists.txt` in the same form: files named in `target_sources` lists in place of
  `add_openmw_dir`, `opencs_units` and `opencs_hdrs`, `add_executable` and `qt_add_executable` in place of
  upstream's wrappers, Qt's resources through `AUTORCC`, `openmw-navmeshtool` without its library, and
  `BUILD_WITH_CODE_COVERAGE` a setting of the checks target. The cost is a conflict with every upstream change
  to an app's list, which the merge resolves by naming the file in its list.
- The test binaries and benchmarks in the same form: files named in `target_sources` lists in place of
  `file(GLOB)` over names, links in the keyword form. The cost is a conflict with every upstream change to a
  test list, which the merge resolves by naming the file in its list.
- The top-level `CMakeLists.txt` split by responsibility: options and the install layout in
  `cmake/Options.cmake`, every `find_package` in `cmake/Dependencies.cmake`, the install rules in
  `cmake/Install.cmake`, the vendored libraries in `extern/CMakeLists.txt`, the resource files in
  `files/CMakeLists.txt`, the Qt translations in `files/lang/CMakeLists.txt`, the Doxygen targets in
  `docs/CMakeLists.txt`; the version from `project(VERSION)`. CPack and the NSIS installer go, with the
  `vcredist` bundling, the Visual Studio generator's compiler override and the branches for compilers below
  the floor. The cost is a conflict with every upstream change to the root, which the merge places in the
  file that now holds it.
- Every setting on a target: the found packages as imported targets carrying their headers and the
  definitions those need, sol3 a target in `extern/`, and Windows's header trims and MSVC's options an
  interface target every tree-built target links, in place of the directory-wide `include_directories`,
  `add_compile_definitions` and `add_compile_options`; `OPENMW_MP_BUILD`, which only the Visual Studio
  generator read, goes. The cost is a conflict with an upstream change to a link list or a found package.
- Every CMake option and cache entry named `OPENMW_*`, upstream's `BUILD_*`, `USE_*` and bare names
  renamed (`BUILD_OPENMW` is `OPENMW_BUILD_GAME`, `BUILD_OPENCS` `OPENMW_BUILD_CS`): one style for one
  tree, since GitLab's pipeline, the names' only outside reader, is gone. The cost is a conflict with an
  upstream change that adds or reads an option, which the merge renames.
- Every CMake file laid out by gersemi 0.29.2 (`.gersemirc`), upstream's among them, as clang-format lays out
  the C++, and the commit listed in `.git-blame-ignore-revs`; `omw lint cmake`, upstream's tab check, goes, since
  the layout holds it. The cost is a conflict with every upstream change to a CMake file, which the rewrite of
  every one already costs.
- `components/stereo/multiview.cpp`'s `sMultiview`, written twice and never read, gone: Clang 23's
  `-Wunused-but-set-global` stopped the build on it. The call that caches and logs whether multiview
  stands stays. The cost is a conflict with an upstream edit to `configureExtensions`.
