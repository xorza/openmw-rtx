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
- The fork's own `README.md`, `env/` (upstream's `CI/`, renamed for what it holds, the systems a build runs
  on) and workflows, and the root tooling: `CMakePresets.json`, `.clangd`, `.zed/`, `omw`, `omw.cmd`,
  `.claude/skills/`, `.gitattributes` and `.gitignore`.
- The build the presets and Crashpad need: the raised CMake minimum, which reads the presets'
  `$comment`s; the raised Boost minimum, whose flat maps the scene identities are; no scan for modules,
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
  `extern/sol3` included. MSVC's `4244` and `4267` are off, since `-Wall -Wextra` leave `-Wconversion`
  out.
- A post-processing technique's integer is held to `int`'s range before it is converted
  (`fx::Lexer::scanNumber`): a double past it converts to no int, which upstream's check read back after the
  conversion, and Clang, which builds the fork, folded that check away, so `18446744073709551615` parsed.
- MurmurHash3 reads its blocks through bytes (`extern/smhasher`): it cast a key at any address to
  `const uint64_t*`, which Clang's undefined-behaviour check stopped on as a misaligned pointer.
- `CSMWorldUniversalIdTest.shouldFailToConstructFromNoneWithInvalidType` builds its unknown type one past
  the last, inside the enumeration's range, where `INT_MAX` was outside it and so no value of the type,
  which Clang's undefined-behaviour check stopped on.
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
  - the navmesh updater read the steady clock wherever it timed a delay, so
    `repeated_tile_updates_should_be_delayed` raced the wall: posted again once the first batch was built, its tiles
    stood within the update interval only where that batch built in less, and under ThreadSanitizer on a busy
    runner none was delayed. The updater now takes the clock it times by (`DetourNavigator::Clock`, the steady
    one unless a test hands its own), and the job queue takes the time it is asked at from its caller alone;
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
  A tile is keyed on the triangles of its recast mesh, and `MeshSource`, which carried each model's file and transform
  into upstream's database key, is gone with the source arguments of `RecastMesh` and `RecastMeshBuilder::addObject`:
  a key of the file and the transform read a tile built in one pose of an animated part back for every pose. The cost
  is a conflict with an upstream edit to those signatures or to `TileCachedRecastMeshManager::makeMesh`.
  `ObjectTransform` is gone with it, an object's position and scale that only that key read: `ObjectShapes`,
  `DoorShapes`, `CollisionShape` and `RecastMeshObject` carry none, and the scene, the world and `openmw-navmeshtool`
  build none. The cost is a conflict with an upstream edit to the navigator's calls in `MWWorld::Scene`'s
  `addObject` and `World::updateNavigatorObject`, or to the navmeshtool's objects.
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
- AlmaLinux 8 with gcc-toolset-15 for CI's Linux builds, the vcpkg set and the release image, from
  `env/install_almalinux_deps.sh`, in place of upstream's Ubuntu 24.04: the AppImage runs on glibc 2.28. A desk on
  Ubuntu 26.04 installs the same system headers from `env/install_ubuntu_deps.sh`, with upstream's
  `install_debian_deps.sh` folded into it, and so does CI's tsan job, which runs on the runner itself. Both build with
  the pinned Clang over GCC 15's libstdc++, in place of upstream's GCC. The cost is a conflict with upstream's edits to
  its package lists.
- `components/CMakeLists.txt` as `openmw-components` and `openmw-components-qt`, each folder's files
  named in a `target_sources` list and its headers as a file set, in place of the stem macros
  (`add_component_dir`, `add_component_qt_dir`) and the hand-made unity build (`enable_unity_build`,
  `OPENMW_UNITY_BUILD`), with no unity build in their place; without upstream's nested
  `project (Components)`; the platform folder's lists in `components/platform/CMakeLists.txt`, for Windows
  and Linux alone, so upstream's other Unix and `filestdio.cpp` branches go; every app linking the new names. The cost is a conflict with every upstream change to a component's list,
  which the merge resolves by naming the file in its folder's list.
- Every app's `CMakeLists.txt` in the same form: files named in `target_sources` lists in place of
  `add_openmw_dir`, `opencs_units` and `opencs_hdrs`, `add_executable` and `qt_add_executable` in place of
  upstream's wrappers, Qt's resources through `AUTORCC`, and `openmw-navmeshtool` without its library. The cost is a conflict with every upstream change
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
  the C++; `omw lint cmake`, upstream's tab check, goes, since
  the layout holds it. The cost is a conflict with every upstream change to a CMake file, which the rewrite of
  every one already costs.
- `components/stereo/multiview.cpp`'s `sMultiview`, written twice and never read, gone: Clang 23's
  `-Wunused-but-set-global` stopped the build on it. The call that caches and logs whether multiview
  stands stays. The cost is a conflict with an upstream edit to `configureExtensions`.
- One build in place of upstream's switches (`docs/rtx/build.md`): every configuration's flags in `cmake/Flags.cmake`,
  read through `CMAKE_USER_MAKE_RULES_OVERRIDE`, with line tables and frame pointers in Release too and a PDB on every
  MSVC link; warnings as errors by CMake's `CMAKE_COMPILE_WARNING_AS_ERROR` in place of `OPENMW_CXX_FLAGS` and
  `OPENMW_MSVC_WERROR`, and link-time optimisation by `CMAKE_INTERPROCEDURAL_OPTIMIZATION` in place of `OPENMW_LTO`.
  Gone with their branches: every program's switch (`OPENMW_BUILD_*`), as every flavour builds every program, the
  tests and benchmarks behind CMake's own `BUILD_TESTING` and the Doxygen targets failing by name where Doxygen is
  missing; plain Lua (`OPENMW_LUAJIT`; the sources' `NO_LUAJIT` branches stay, as upstream wrote them), the system's
  TinyXML (`OPENMW_SYSTEM_TINYXML`, `cmake/FindTinyXML.cmake`), coverage, every precompiled header
  (`OPENMW_MSVC_PRECOMPILED_HEADERS`), since ccache caches through none, and the console option, the console fixed to
  Debug and RelWithDebInfo as its default had it. One portable install layout on every system, every file beside the
  programs and the libraries in `lib/`, so the system-install branch goes: `OPENMW_LOCAL_INSTALL`, the suffixes, the
  icon folder, the `GLOBAL_*` cache entries, which `Files::LinuxPath` reads as empty, and the global
  `files/openmw.cfg`. Every generator single-config, so `get_generator_is_multi_config` and the per-configuration
  resource folders go. One Windows manifest for every program, `files/windows/openmw.manifest`, which `openmw-system`
  adds to each executable: the game's `openmw.exe.manifest` and its line in `openmw.rc` go, and the game gains the
  per-monitor DPI awareness and long paths the other programs had. The cost is a conflict with every upstream change
  to an option, a precompiled header list or a manifest line, which the merge drops.
- Every third-party library from the vcpkg set, static (`vcpkg.json`, `docs/rtx/build.md`), in place of the system's
  on Linux and upstream's prebuilt set on Windows, whose tags `CI/deps_versions.msvc.sh` held and which goes:
  `cmake/Dependencies.cmake` finds each package by its own CMake files or pkg-config's, with no version floors, and
  every fetched project is built from its pinned archive alone, never a system's (`FIND_PACKAGE_ARGS`). The finders
  the set's own files replace go (`FindFFmpeg`, `FindLZ4`, `FindMyGUI`, `FindOSGPlugins`, `FindLIBUNSHIELD`,
  `FindLuaJit` with `LibFindMacros` and `COPYING-CMAKE-SCRIPTS`, the licence of finders gone before, and the unused
  `FindSphinx`), and so do `CheckBulletPrecision`, as the set's Bullet states its precision, and
  `CheckLuaCustomAllocator`, a warning about the one LuaJIT the set pins. OpenSceneGraph's plugins are linked static
  and registered in one generated file every program takes (`components/resource/osgplugins.cpp.in`), as upstream's
  static builds did in `imagemanager.cpp`; upstream's run-time check that they can be loaded
  (`Misc::checkRequiredOSGPluginsArePresent`, its call in `apps/openmw/main.cpp`) goes, since the link now holds them.
  The cost is a conflict with an upstream change to a found package, a plugin or the check.
- `docker/`, upstream's Ubuntu build in a container (OpenMW's PPA, SDL2, Qt 5, `cmake` and `make`), which cannot
  build a tree that takes its libraries from the vcpkg set and builds through `omw`: the release's container is
  `env/release-base.Dockerfile`. `upstream.yml` keeps the folder deleted, which drops an upstream edit to it at
  the merge.
- Dead CMake gone: `cmake/escape_regex_control_characters.cmake`, which
  nothing called; the commit date the root read with `git log` at every configure, which the AppStream
  metadata's release date reads and `cmake/GitVersion.cmake` fills in at every build instead; the
  `OPENMW_RESOURCES_ROOT` guards atop `files/data`, `files/data-mw`, `files/shaders` and `files/lua_api`,
  which the root always sets first; the install components, which nothing installs by since CPack went; the
  empty `CMAKE_CXX_CLANG_TIDY` in `extern/`; and `mwmechanics/character.cpp`'s `-Wno-maybe-uninitialized`,
  a flag of GCC's, which builds nothing of the tree. The cost is a conflict with an upstream edit to those lines.
- What the pinned set makes constant, no longer tested:
  `cmake/CheckOsgMultiview.cmake`'s `try_compile`, as OpenMW's OSG at the pinned commit has no multiview, so
  `OSG_HAS_MULTIVIEW` is never defined and the sources' multiview branches stay unbuilt, as they were;
  `files/data-mw`'s configure dependency for every generator but Ninja, which is every preset's; and
  `OPENMW_USE_UNSHIELD`, the tree's own spelling of `NOT WIN32`, which the wizard's list and the unshield search
  now read directly (the wizard's source keeps its definition). The cost is a conflict with an upstream edit to
  those lines.
- Resources through `configure_file` itself: `copy_resource_file`,
  `configure_resource_file` and `copy_all_resource_files`, which joined a path and called it, go from
  `cmake/OpenMWMacros.cmake`, and `GitVersion.cmake` no longer includes that file. The runtime folder has one
  name, CMake's `CMAKE_RUNTIME_OUTPUT_DIRECTORY`, where `OPENMW_RESOURCES_ROOT` and `PROJECT_BINARY_DIR` named
  it too. The cost is a conflict with an upstream change to a resource list's loop.
- The version filled once: a build is of a Git clone, which a configure
  without `.git` refuses, so `GIT_CHECKOUT` and the configure-time fallback with empty hashes go. The version
  files are configured once with everything but the commit, its date and the latest tag, and
  `cmake/GitVersion.cmake` fills those three at every build, now into the AppStream metadata too, whose
  release date had read a date taken at configure. The templates read `PROJECT_VERSION*`, so the
  `OPENMW_VERSION_*` copies of it go. The cost is a conflict with an upstream edit to the version's templates
  or its target.
- Each dependency found one way: libunshield by its header and archive, as
  MyGUI and LuaJIT are, in place of pkg-config, which no other package needs; Boost in one `find_package`; and
  Google Test and Google Benchmark under one `BUILD_TESTING`. The cost is a conflict with an upstream edit to
  those finds.
- Third-party code in one style and quiet: the vendored projects under
  `extern/` (`osgQt`, `oics`, `osg-ffmpeg-videoplayer`, `smhasher`, `rgbcx`) named and listed as the tree's own
  targets are, with keyword links and `osgQt` through `AUTOMOC` in place of `qt_wrap_cpp`, and their warnings
  off from `extern/`'s directory flags in place of `rgbcx`'s own branch. The cost is a conflict with an upstream
  edit to one of those lists.
- Tests and smoke runs: the test binaries' `-Og` an interface target each
  links where it is defined, in place of a property `openmw_add_test` set once a binary; the system's half of the
  test support one object library the three binaries that read it link, in place of a choice in each; and the
  benchmarks CTest tests that run Google Benchmark's dry run, in place of CI's step that ran them. The cost is a
  conflict with an upstream edit to a test binary's link list.
- Translations: the `translations` and `purge-obsolete-translations`
  targets go for `omw translations [--purge]`, which reads the one source list `omw lint translations` checks
  against; the `.qm` files through `qt_add_lrelease` in place of the deprecated `qt_add_translation`; Qt's own
  translations from `QT6_INSTALL_TRANSLATIONS` in place of `qmake -query`, `qtbase_` alone, as every language
  has it. The cost is a conflict with an upstream edit to those targets.
- Install and Windows properties: each installed program's own PDB, by
  `$<TARGET_PDB_FILE>`, in place of every `.pdb` in the build's root folder less 14 named folders, which also
  carried the tests' and the harness's; the game's console by `WIN32_EXECUTABLE` set to `$<CONFIG:Release>` in
  place of an MSVC branch around `/SUBSYSTEM`. The cost is a conflict with an upstream edit to those lines.
- `/cgthreads1` gone from its 8 files: upstream's, for a runner with 8 GB,
  where the fork's public repository's Windows runners have 16 GB; held by three Windows runs without it. The
  cost is a conflict with an upstream edit to those lists.
- One set of flags, the set's code generation shared: the sanitizers,
  MSVC's runtime and debug information format, and position-independent programs in `cmake/Flags.cmake`, out of
  `cmake/Options.cmake` and the root; the build types narrowed to the two `cmake/Flags.cmake` defines,
  `RelWithDebInfo` and `Release`. The cost is a conflict with an upstream edit to those lines.
- The build files in one layout: the installed programs named once in
  `cmake/Install.cmake`, with their PDBs and `defaults-cs.bin`, in place of an `install(TARGETS)` in each app's
  folder; the CS's console as the game's, by `WIN32_EXECUTABLE` set to `$<CONFIG:Release>`, where upstream opens
  one in every configuration; the set's libraries without CMake targets imported by one function; `pack_resource_file`
  written out in `files/`, its one caller, and `openmw_qt_platforms` beside Qt's `find_package`, so
  `cmake/OpenMWMacros.cmake` and `CMAKE_MODULE_PATH` go. The cost is a conflict with an upstream edit to an app's
  install line, the CS's executable, or those finds.
- `openmw-lib` an object library rather than a static one: under MSVC's link-time code generation its archive
  came to 3.6 GB of the 4 GB `lib.exe` writes (LNK1248), and the three programs that link it, the game, the
  harness and `openmw-tests`, take its objects directly. The cost is a conflict with an upstream edit to that line.
- The launcher's and the wizard's look (`docs/rtx/look.md`): `Platform::Application` takes a `Platform::Look`,
  and for `Graphite` dresses the program in the fork's palette and style and skips upstream's Windows dark
  mode, which the editor keeps; the launcher and the wizard no longer build upstream's Windows dark sheet in.
  The cost is a conflict with an upstream edit to the application's constructor or to those three calls.
- The launcher's glyphs and error ink (`docs/rtx/look.md`): its toolbar's pages drawn from Lucide's icons in
  code rather than Oxygen's PNGs in `mainwindow.ui`, which leave the tree, its fallback icon theme always taken
  and redrawn from Lucide, and an invalid profile name in the look's error ink rather than `Qt::red`. The
  cost is a conflict with an upstream edit to those icons or to the two lines that colour a name.
- The launcher's contains-data mark from Lucide through `Misc::ScalableIcon` rather than Oxygen's
  `openmw-plugin.png`, which leaves the tree, and the wizard's four icons redrawn from Lucide in place
  (`docs/rtx/look.md`). The cost is a conflict with an upstream edit to that line or to those icons.

