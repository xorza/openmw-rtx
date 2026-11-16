# Upstream changes

The fork's changes to upstream code that stay, each approved by the user, with its reason. A change to upstream code
that is not here is held to `AGENTS.md`'s first rule: proposed, made only on the user's explicit yes, then added here.

Unless an item names another, its cost is a conflict with an upstream edit to the lines it names. `upstream.yml` keeps
every path deleted here deleted, so an upstream edit to one holds no merge.

**The seam**, `MWRender::Renderer`, which both renderers stand behind, so the game owns no OpenSceneGraph viewer. A
conflict here resolves by keeping the seam:

- `OMW::Engine` loses `createWindow`, its `osgViewer::Viewer`, `Stereo::Manager`, `Resource::Profiler`, the
  `StatsHandler` and `OPENMW_OSG_STATS_FILE`: the renderer owns the window and its frame, and takes the content to
  prepare through `Renderer::prepareContent`.
- `MWWorld::World::init`, `RenderingManager` and `MWGui::WindowManager` take the `Renderer`; `SDLUtil::InputWrapper`
  takes a `SDLUtil::GraphicsListener` (`MWInput::RendererKeys`), so a key whose request the renderer declines (F3, F4)
  is told to the player.
- `CharacterPreview`, `LocalMap` and `GlobalMap` stand on the renderer's `OffscreenView` and `MapOverlay`; the
  post-processor's exterior flag comes from `GlWorld`.
- `Renderer::support`, asked where the console, Lua and F2 would toggle what does nothing under the renderer, and
  `ToggleBorders` under the ray tracer; a script's post-processing shader under a renderer that declines the chain loads
  by name and draws nothing, where it stopped at the null post-processor, and the chain lists empty
  (`RenderSupport::declinedSetting`; `load` and `getChain` in `postprocessingbindings.cpp`).
- MyGUI's backend behind `MyGUIPlatform::GuiRenderManager` (`components/myguiplatform`), which `myguirtx` implements
  beside OSG's, and `VideoWidget`'s texture through `MyGUIPlatform::shareTexture` (`videowidget.cpp`).

**The rasterizer's picture**, each moved where the ray tracer needed it and upstream's was wrong:

- the optimizer merges in child order, not address order;
- an exterior map tile keeps its land where the quad tree did not build the chunk yet;
- a groundcover plant stands under its model's own transforms, not inside them (`GroundcoverShapes`);
- both renderers draw at `[Video] resolution x/y`, shown scaled into the window (`Misc::Presentation`), so the GUI, the
  projection, the pointer and Lua read one size; an upstream settings file's window size moves to the window on the
  first start.

**One rule both renderers read**, moved out of the rasterizer's code into a header or a helper:

- the cloud shell's and the star dome's fade (`components/sky/vertexrules.hpp`, read by `ModVertexAlphaVisitor`); the
  sky's clocks (time of day, cloud scroll, star roll) in `components/sky`, with `SkyManager` split from `Precipitation`;
  the moon's phase continuous in game time (`MWWorld::MoonModel::phaseEighths`), which the ray tracer draws down to the
  horizon;
- the ripple's and the sea's rules (`ripplerules.hpp`: `isWading`, `strikesWater`; `searules.hpp`), which
  `RippleSimulation` reads in place of its emission lines (`ripplesimulation.cpp`);
- the grass's density (`DensityCalculator` in `groundcoverdensity.hpp`, out of `groundcover.cpp`);
- the auto normal and specular maps (`components/shader/automaps`, `Shader::AutoMapRules`, `setAutoMaps` in place of the
  five pattern setters, read by `Shader::attachAutoMaps` and the shader visitor `getTemplate` runs), which also fixes
  upstream's: in `recreateShaders` the specular map took the normal map's unit, and unit 7, which holds the tangents,
  stood in for unit 0's coordinates;
- the reference walk `ObjectPaging` hands the ray tracer (`visitESM3References`, `visitESM4References` into a sink,
  `Terrain::RefKinds`, `Terrain::PagedCellRef`), filtered by what each renderer takes (`typeFilter`).

**What the ray tracer reads of the game**, each an accessor, a stamp or a hook on state upstream already keeps:

- `Terrain::World::getActiveGrid`; `LightController`'s type and colour; `LightSource`'s controller, source radius and
  const light; `RigGeometry`'s skeleton, influences, bones, skin matrix and `hasSkeleton`; `Skeleton::markReached`;
  `PaintedTexture`, `ImageRegion` and `OffscreenFraming`; `Animation`'s glow fade and the glow light's radius;
  `World::getStormDirection`; the projection offset beside the projection; `Precipitation::isShown` and its occlusion
  setting; and the terrain's height solved in double, so both grounds agree on every OSG build.
- Stamps: `SceneUtil::StableIdentity` on every cell and reference root (`Objects::cellRootOf`), so the mirror tells
  roots apart without addresses; `SceneUtil::LampBody` on a light's group, so a lamp's model adds no glow its light
  already delivers; `SceneUtil::EmbeddedImage` on an image a NIF carries inside it, which has no file to key by.
- `SceneUtil::StateSetUpdater::getGeneration`, which `reset` bumps, so the mirror's copy of a glow follows a reset.
- The pose hook in `RenderingManager`'s intersection visitor (`Renderer::poseForIntersection`): the ray tracer culls no
  world, so the crosshair met every actor in its bind pose.
- `LocalMap::requestMap` asks again for a tile the renderer gave up (`OffscreenView::isAbandoned`).
- The visibility gates (`MWScript::VisibilityGates` and what feeds them): without them distance stands scripted stages
  the game keeps down.
- `~Engine` joins the Lua worker first and drops the world before the script manager: a frame the ray tracer left on
  purpose left the worker in its update, and the cell reader marks gates through the script manager.
- One step the histories must hear: a reference moved with its physics placed outright is a jump (`World::moveObject`,
  `notifyJumped`), and the clock moved past the frame's step is a cut (`MWWorld::HourWatch`, once a frame where the
  weather updates). A rest or a wait is a cut (`RenderingManager::notifyTimeJumped`) where `World::advanceTime` told it
  as a change of worldspace.
- The loading screen's preparation (`Rtx::ContentPreparation` in `Renderer::prepareContent`):
  `ESMTerrain::Storage::getLayerInfo` public and `Store<ESM::LandTexture>`'s `begin` and `end`
  (`MWRender::contentLayers`), the `PreparingContent` label in six languages, and a note above it
  (`Loading::Listener::setNote`, `PreparingContentNote`): a cold start with a large mod list waits minutes there.
- `Resource::SceneManager::setModelPrograms` (`ModelPrograms::Omitted`), so a renderer that compiles no GLSL loads
  models without programs; `SceneManager::letGo` and `GenericResourceManager::removeFromCache`, so the preparation lets
  go of a model, its NIF, its images and their shared state as the last job reading them ends.
- `components/settings` naming the core's `Upscale` and `MAX_BOUNCE_RAYS`, which `[RTX]` stores and sanitizes.

**How content is loaded** (`docs/architecture.md`, "Loading"), kept in the fork rather than offered upstream; a merge
that touches `components/resource`, `bsa`, `files` or `platform/file*` resolves against it:

- One load a key in flight (`GenericObjectCache::getOrLoad`), which `SceneManager::getTemplate`, `NifFileManager::get`,
  `ImageManager::getImage`, `KeyframeManager::get` and `BulletShapeManager::getShape` load through, each load a private
  function; `CacheStats` counts loads, waits and reloads. Two threads that missed one model both loaded it. Its test of
  a throwing load holds the loader's catch until the waiter is joined, since the exception both share is freed through
  a count in libstdc++, which ThreadSanitizer cannot see.
- An archive opened once and read by position (`Platform::File::readAt`): `Bsa::BSAFile` holds a `Files::SharedFile`,
  and each entry's `ConstrainedFileStreamBuf` keeps its own position, where each entry opened the archive again (18,082
  opens of `Morrowind.bsa` in 75 s); POSIX `pread`, Windows `ReadFile` at an `OVERLAPPED`'s offset, the file shared for
  writing and deletion. Cost: a conflict with an edit to the stream buffer, `BSAFile::open` and `close`, or where the
  four archive kinds open an entry's stream.
- The caches kept by a byte budget in one least-recently-used order (`Resource::CacheBudget`, owned by `ResourceSystem`)
  in place of `cache expiry delay`; each load states its bytes (`Resource::Loaded`), and `navmeshtool` and
  `bulletobjecttool` hand a budget of nought. A run may hold it under a limit (`CacheBudget::limit`, the harness's
  `--cache-budget`), so the desk loads as a machine short of memory does. Cost: also a conflict with an edit to the
  managers' constructors, `ResourceSystem`'s, or the physics system's shape manager.
- `CellPreloader::preload` skips a cell while the caches stand past their budget with everything in use, counted as
  `CellPreloader Skipped`: a preload is a guess, and past the budget it holds memory the frame needs.
- A model's texture holds the file's header and not its texels where the renderer reads texels itself
  (`ImageManager::getModelImage` in place of `getImage` in the NIF loader's `getTextureImage`, `attachAutoMaps` and the
  scene manager's image reader; `Resource::ModelImages::Headers`, `Resource::ImageHeader`, `Resource::texelsOf`,
  `releaseTexels`): cached models held 7.6 GiB of texels under the PBR replacers while the device held 2.2.
- The navigator's tiles in the content store (`DetourNavigator::NavMeshTileStore`) in place of its SQLite database,
  which leaves the build: one store for what the fork derives from the content, shared across profiles, its key
  versioned (`DetourNavigatorTileVersionTest`). A tile is keyed on its recast mesh's triangles, so `MeshSource` and
  `ObjectTransform` go, from `ObjectShapes`, `DoorShapes`, `CollisionShape` and `RecastMeshObject` too: a key of file
  and transform read one pose of an animated part back for every pose. `openmw-navmeshtool` loses
  `--remove-unused-tiles` and the vacuum, `[Navigator]` loses `write to navmeshdb` and `max navmeshdb file size`, and
  Recast is the commit `extern/` pins, as the tile version cannot follow a distribution's build. Cost: a conflict with
  every upstream change to the database worker, the input serialization, `openmw-navmeshtool`, the signatures of
  `RecastMesh` and `RecastMeshBuilder::addObject`, `TileCachedRecastMeshManager::makeMesh`, and the navigator's calls in
  `MWWorld::Scene::addObject` and `World::updateNavigatorObject`.

**Runs that repeat**: two runs of one seed must draw the same frames (`./omw repeat`), and each of these broke it:

- the wind held on a paused frame (`MWWorld::windSpeedAfter`, called at the three wind sites), as upstream holds the
  thunder: `WeatherManager::update`'s paused step drew from the world's generator, which a save keeps;
- `NifOsg::BoxPlacer` drawing from `Misc::Rng`, as `ParticleShooter` does, in place of `osgParticle::BoxPlacer`, whose
  `rand` NVIDIA's driver also draws from inside `vkQueueSubmit2` (`nifloader.cpp`, `sky.cpp`);
- `SceneUtil::CopyRigVisitor` (`attach.cpp`) adds a skinned part's pieces in the order met, where a `std::set` of
  pointers added them by address (a candidate for upstream);
- the optimizer merges drawables (`LessGeometry`) and groups (`MergeGroupsVisitor`) whose states hold the same
  (`LessStateContent`), not only those sharing one state object, which the shared state cache's history decides;
- `MWWorld::Scene::CellStoreCollection` a `std::vector` in load order, and `World::getActiveCells` with it, where a set
  of pointers unloaded cells in address order (a candidate for upstream);
- the scripts' `math.random` seeded where the harness seeds the world (`LuaUtil::LuaState::seedRandom`).

**What a crash report says**:

- `components/crashcatcher` replaced by a Crashpad monitor process, with the keeper `wrapApplication` forks under an
  AppImage (`Crash::keepImageMounted`), since a monitor outlives the pipe that keeps the image mounted.
- Each log line written in one write with its stamp (`DebugOutputBase::write`), as the monitor appends to the log from
  another process.
- A crash note (`Crash::NoteScope`) at `SceneManager::getTemplate`'s load, `StateManager::loadGame`'s save and records,
  and `Scene::changeCellGrid` and `Scene::changeToInteriorCell`; a model's VFS path on its root
  (`Misc::OsgUserValues::sFilePath`), so a refusal and a note name the file.
- `Debug::wrapApplication`'s fatal error box reads `OPENMW_CRASH_DIALOG` as the crash dialog does, and shows after the
  error is logged: under a harness it waited for a click with nothing in the log.

**What the harness holds of the game** (`OMW::EngineHost`, which the game alone asks nothing of): a sky between two
weathers for one update (`WeatherManager::holdWeather`), a stated game hour (`holdsGameClock`), a script's message box
logged rather than shown (`showsScriptMessageBoxes`), and a saves folder of the run's own (`getSavesFolder`). And the
test support (`components/testing/util.hpp`, `apps/components_tests/main.cpp`): death tests in the `threadsafe` style
for ThreadSanitizer, a run's folder keyed on the process too, each test's folder emptied at its start (`FreshTestDirs`),
and upstream's two Lua storage tests writing there.

**The interface**:

- The `[RTX]` settings pages and their translations; the settings window and the launcher offer only what the ray tracer
  honours (`RtxSupportTest`), the rasterizer's pages removed with their strings and Distant Land made Distant Objects.
  The keys stay in `settings-default.cfg`, which the rasterizer reads.
- The fork's name, OpenMW RTX, in the launcher's and the wizard's titles, their translations and the menu entry.
- The port to SDL3, for the pixel density and display scale a fractionally scaled Wayland desktop sets:
  - `[Video] gamma` is the renderers' own (`PingPongCanvas`, the tone pass), as SDL3 has no gamma ramp, and `[Video]
    contrast` goes with it;
  - a controller's buttons are bound by place (`SDL_GAMEPAD_BUTTON_SOUTH`);
  - a Lua cursor is sized in the frame's pixels (`Presentation::shownScale`);
  - where Wayland refuses relative mode and warps, the pointer goes unwrapped (`InputWrapper::mWarpMovesPointer`);
  - a hidden, minimized or occluded window draws nothing and pauses, read from the window's flags
    (`SDLUtil::isWindowShown`), as KWin suspends a minimized window and reports it occluded (KDE bug 492924);
  - `[Video] window width/height` hold the desktop's unit (`WindowListener::windowSized`), and only a restored window's
    size is stored (`SDLUtil::isRestoredSize`), where a size in pixels shrank at every start and a maximized size opened
    the next start's window that large.
- The fonts rasterized again while the game runs (`docs/architecture.md`, "A live scale and font size"):
  `Gui::FontLoader` keeps its TrueType fonts and refits them in place (`FontLoader::fit`); each font file's bytes are
  held (`MyGUIPlatform::DataManager::hold`); every text sub-widget and list is the fork's (`Gui::FontUsers`,
  `Gui::TrackedText`, `Gui::ListBox`) in MyGUI's factories; a book page drops its book on a refit and its owner typesets
  it again (`BookPage::adviseRetypeset`); `Gui::InterfaceFonts` owns fonts and registries and refits them at every
  layout (`WindowManager::fitFonts`). The cursor, the windows, the local map and the inventory follow the scale, and
  `[GUI] scaling factor` applies live from a settling slider (`Gui::SettlingScrollBar`).
- `[GUI] font size` read from the fonts' owner alone (`FontFit::mFontSize`, `MWBase::WindowManager::getFontSize`, held
  by a source scan): `FontWrapper` goes, a book is typeset at a stated size (`BookTypesetter::create`), a window with
  rows sized by it lays them out again (`WindowBase::refitText`), and `omw/mwui` follows (`_onFontSizeChanged`). It
  applies live from a slider under the interface scale.
- The launcher's look (`docs/look.md`): `Platform::Application` takes a `Platform::Look`, and `Graphite` dresses the
  launcher and the wizard in the fork's palette, skipping upstream's Windows dark mode, which the editor keeps; the
  toolbar's pages, the fallback icon theme, the contains-data mark and the wizard's four icons drawn from Lucide in
  place of Oxygen's PNGs, which leave the tree; an invalid profile name in the look's error ink.
- The launcher's window 900 by 760 at first with no minimum of its own, so Qt holds it to what its pages need; the
  content selector's game file list a third of its row and the search field two.
- The launcher's DLSS download, into `getUserLocalDataPath` (`LinuxPath`, `WindowsPath`): NVIDIA's feature library is
  the player's to install, and that folder is writable on every system.
- A Light bolt that strikes no actor lights the surface it struck for the effect's duration
  (`files/data-mw/scripts/omw/surfacelight/`), as the user asked: built-in scripts through upstream's projectile hit
  handler, no engine code.

**Faults fixed**:

- `Files::LinuxPath` took a failed `read_symlink` for the executable's path (`ec.value() != -1` holds for every error).
- `SettingsFileParser::saveSettingsFile` and `StateManager::saveGame` wrote in place, so a crash or a full disk left
  part of either: both write through `Platform::writeWhole`, synced (`openmw-platform-io`), and a save the system could
  not confirm on the disk is a warning.
- The SDL3 port truncated a window's size over its pixel density (`SDLUtil::windowPoints` rounds).
- `SDLUtil::InputWrapper` logged an event type in hex without `std::dec`, which left every later number in hex.
- `openmw_settings_window.layout` gave `ScriptBox` its `name` twice, which an XML parser refuses.
- OSG's COLLADA reader was handed a stream it parses as a C string, past the model into the heap: a nought now ends the
  bytes (`aColladaModelLoadsWhateverWasReadBeforeIt`). And a COLLADA rig's state set was deep-copied down to its images,
  which read past a block-compressed image's buffer on Windows; the rig is copied with its state set shared.
- `GetMasserPhase` and `GetSecundaPhase` read a new moon until the sky dome existed; they read the date's phase, and the
  last exterior's after that (`WeatherManager::getMoonPhase`).
- The void reset in `Scene::playerMoved` measured against a point every interior lowered and no moved object changed:
  `MWWorld::LowestPoints` holds the active interior's, kept by `World::collisionObjectChanged`, and
  `PhysicsSystem::updateScale` scales at once (`void reset in an interior reads that interior alone`).
- The cell preloader missed what inserting an actor builds: `Npc::getModelsToPreload` lists an unmet NPC's worn parts
  (`MWClass::appendRecordWornParts`), and `CreatureLevList::getModelsToPreload` every creature the list may spawn: up to
  57 of a crossing's 293 ms slow frames on a large content list.
- The navmesh job queue ranks a due delayed job with the rest (`JobQueue::pop`), where a near tile changed twice waited
  behind every farther one; the updater takes its clock (`DetourNavigator::Clock`), so a delay test no longer races the
  wall.
- The launcher wrote its shown language's encoding over `openmw.cfg`'s at every Play, turning `win1251` into `win1252`:
  it shows a language that reads the game's encoding (`Config::languageForEncoding`), from one table
  (`Config::encodingOfLanguage`).
- `Scene::addPostponedPhysicsObjects` gave a script's placed object physics and no navmesh place; it now takes
  `addObject`, as every collision object does (`test_script_placed`).
- `SceneUtil::registerSerializers` is called by `SceneManager` as it is made, without `skipGeometry`: the game read
  `SceneUtil::Material` models without the wrapper the editor alone registered, and the flag swapped `osg::Geometry`'s
  wrapper for an empty one. The editor's own call in `RenderWidget` goes.
- `RenderingManager::getFieldOfView` returned the override flag, 1°, wherever a field of view was overridden; the local
  map's view is built in double, as the ray tracer reads it.
- A post-processing integer is held to `int`'s range before conversion (`fx::Lexer::scanNumber`): Clang folded the check
  after it away.
- `Misc::StringUtils::toNumeric` refuses `inf` and `nan`, and reads floats by `from_chars` alone (a `static_assert`
  holds every toolchain to it): upstream's stream for Clang read `1.5x` and `0x10` otherwise.
- Base64 encodes a string shorter than a block (`extern/Base64`, `i + 2 < in_len`, approved 2026-11-14); MurmurHash3
  reads its blocks through bytes (`extern/smhasher`), not a misaligned `const uint64_t*`.
- `CSMWorldUniversalIdTest.shouldFailToConstructFromNoneWithInvalidType` builds its unknown type inside the enum's
  range, where `INT_MAX` was no value of it.
- `components/stereo/multiview.cpp`'s `sMultiview`, written and never read, goes: Clang 23's `-Wunused-but-set-global`
  stopped the build. The call in `configureExtensions` that logs multiview stays.

**One standard for the tree**, upstream's code held as the fork's:

- One set of warnings on every compiler (`cmake/Checks.cmake`), with the hunks that keep upstream's code clean, the
  `extern/sol3` patches included. `-Wsuggest-override` and
  `-Wzero-as-null-pointer-constant` hold everywhere, so eleven sources and six headers in OSG's style carry edits
  (2026-11-14). GCC's own asks (`-Wdouble-promotion`'s casts, the 34 `Crash::notNull`) left with GCC on 2026-11-12, but
  for an ESM4 group label's cast, which `-Wcast-qual` asks.
- `-Wconversion` and `-Wimplicit-fallthrough` on Clang, MSVC's `4244` and `4267` back on (2026-11-16): each implicit
  conversion is a `static_cast` to its destination, spelled `decltype` of the member where a template's record types
  differ, and a mixed comparison template (`ConstInfoSelectWrapper`'s ranges, the dialogue's `selectCompareImp`)
  compares in `std::common_type_t`; a braced list a range-`for` converts is a `std::array` of the element's type.
  `ESM::Class::CLDTstruct::getSkill` takes a `std::size_t` index, as `ESM::Faction`'s does; `Platform::File::tell`
  tests `lseek`'s `-1` before the conversion, where the comparison after it never held. The five cases that fell into
  `default: break;` end in a `break`. `extern/Base64` comes in as `SYSTEM`, as every other vendored project does.
- clang-format 23.1.5 with upstream's `.clang-format`, C++ LF on every clone: 294 sources reformatted, and a table in
  rows loses its trailing comma. `omw merge-file`, the merge driver, takes the layout out of each merge.
- Every CMake file laid out by gersemi 0.29.2 (`.gersemirc`), and upstream's tab check goes. Cost: a conflict with every
  upstream change to a CMake file, which the rewrite already costs.
- `.clang-tidy` (`omw tidy`) over upstream's code as over the fork's, each check whole or refused with its reason in the
  file, its fixes a commit each; upstream's `.clang-tidy` is replaced, its naming kept at the kinds upstream enforces.
  Fixes beyond the mechanical: members a constructor left unset given values, and enums given a zero (`ESM::AI_None`,
  `ESM::ActiveSpells::Flag_None`); `#if 0` deleted and `#if 1` unwrapped (`mwshadowtechnique.cpp`, `optimizer.cpp`, the
  ESM4 loaders); `pthread_sigmask` for `sigprocmask` in the crash image's handlers; `Files::LinuxPath` through
  `Platform::userHome` for `getpwuid`; `Crash::runMonitorIfAsked` returning the monitor's exit code for `std::exit`;
  `Misc::ScalableIcon`'s copy registered as the original is, which `updateAllIcons` missed. On Windows: flag enums
  holding `0x80000000` typed `std::uint32_t`; `LinuxPath` and `WindowsPath` compiled on their own system;
  `openmw-iniimporter` through `Platform::Process::commandLine` for its own `wmain`; `CS::Editor`'s pid file removed
  through an error code; `windowspath.cpp`'s registry read held to the string's size. Cost: a conflict with an upstream
  edit to the lines the fixes touched, and upstream code a merge brings that the checks refuse, which `omw tidy` names.
- Every header of `apps/` and `components/` guarded by `#pragma once`, upstream's 1,400 guards among them, held by `omw
  lint pragmas`; the headers GLSL reads keep their guards (`portable.h`). Cost: also a header upstream adds with a
  guard, which the lint names.

**The build** (`docs/build.md`). Cost: a conflict with every upstream change to a CMake list, an option, a found package
or the install, which the merge places in this layout:

- `CMakePresets.json` and CMake 4.4 at least, every check for an older one gone; the raised Boost minimum; no module
  scan; the ccache fallback; MSVC's embedded debug information; `$<COMPILE_LANGUAGE:C,CXX>`, which keeps the C++ flags
  off Crashpad's MASM.
- Every library from the static vcpkg set (`vcpkg.json`), in place of the system's on Linux and upstream's prebuilt set
  on Windows (`CI/deps_versions.msvc.sh` goes): packages found by their own CMake files or pkg-config, no version
  floors, fetched projects from their pinned archives alone. The finders the set replaces go (`FindFFmpeg`, `FindLZ4`,
  `FindMyGUI`, `FindOSGPlugins`, `FindLIBUNSHIELD`, `FindLuaJit` with `LibFindMacros` and `COPYING-CMAKE-SCRIPTS`,
  `FindGMock`, `FindSphinx`), with `CheckBulletPrecision` and `CheckLuaCustomAllocator`. OSG's plugins link static,
  registered in one generated file (`osgplugins.cpp.in`), so `Misc::checkRequiredOSGPluginsArePresent` goes. libunshield
  found by header and archive, Boost in one `find_package`, Google Test and Benchmark under one `BUILD_TESTING`.
- AlmaLinux 8 with gcc-toolset-15 for CI's Linux builds, the set and the release image
  (`env/install_almalinux_deps.sh`), so the AppImage runs on glibc 2.28; Ubuntu 26.04 for a desk and the tsan job
  (`env/install_ubuntu_deps.sh`, with `install_debian_deps.sh` folded in). Both build with the pinned Clang over GCC
  15's libstdc++.
- The top-level `CMakeLists.txt` split by responsibility (`cmake/Options.cmake`, `Dependencies.cmake`, `Install.cmake`,
  `Flags.cmake`, `extern/`, `files/`, `files/lang/`), the version from `project(VERSION)`; CPack, NSIS, `vcredist` and
  the branches below the compiler floor go.
- `openmw-components` and `openmw-components-qt`, every app, test and benchmark naming its files in `target_sources`
  lists with headers as file sets, in place of the stem macros (`add_component_dir`, `add_openmw_dir`, `opencs_units`,
  `openmw_add_test`), `file(GLOB)` and the hand-made unity build (`enable_unity_build`, `OPENMW_UNITY_BUILD`); keyword
  links; Qt's resources through `AUTORCC`; `openmw-navmeshtool` without its library; the platform folder's own list for
  Windows and Linux alone. `add_subdirectory(rtx)` adds the fork's benchmarks beside upstream's.
- Every setting on a target: imported targets carrying headers and definitions, sol3 a target, Windows's trims and
  MSVC's options one interface target, in place of directory-wide includes, definitions and options.
- Every option and cache entry named `OPENMW_*` (`BUILD_OPENMW` is `OPENMW_BUILD_GAME`): GitLab's pipeline, the names'
  only outside reader, is gone.
- One build in place of upstream's switches: each configuration's flags in `cmake/Flags.cmake`
  (`CMAKE_USER_MAKE_RULES_OVERRIDE`) with the sanitizers, MSVC's runtime and position-independent programs; only
  `RelWithDebInfo` and `Release`; line tables and frame pointers in Release; warnings as errors by
  `CMAKE_COMPILE_WARNING_AS_ERROR` for `OPENMW_CXX_FLAGS` and `OPENMW_MSVC_WERROR`; LTO by
  `CMAKE_INTERPROCEDURAL_OPTIMIZATION` for `OPENMW_LTO`. Gone: every `OPENMW_BUILD_*`, plain Lua (`OPENMW_LUAJIT`; the
  sources' `NO_LUAJIT` branches stay), the system's TinyXML (`OPENMW_SYSTEM_TINYXML`), coverage, precompiled headers
  (`OPENMW_MSVC_PRECOMPILED_HEADERS`), `OPENMW_MP_BUILD`, the console option, `/cgthreads1` (the fork's Windows runners
  have 16 GB), and the system-install layout (`OPENMW_LOCAL_INSTALL`, `GLOBAL_*`, the global `files/openmw.cfg`): one
  portable layout everywhere, and single-config generators (`get_generator_is_multi_config` goes). One Windows manifest
  for every program (`files/windows/openmw.manifest`).
- Dead or constant CMake gone: `escape_regex_control_characters.cmake`; the configure-time commit date; the
  `OPENMW_RESOURCES_ROOT` guards; the install components; `extern/`'s empty `CMAKE_CXX_CLANG_TIDY`; GCC's
  `-Wno-maybe-uninitialized`; `CheckOsgMultiview.cmake` (the pinned OSG has none); `files/data-mw`'s non-Ninja configure
  dependency; `OPENMW_USE_UNSHIELD`; `copy_resource_file` and its kin for `configure_file`; one runtime folder name;
  `cmake/OpenMWMacros.cmake` and `CMAKE_MODULE_PATH`, its last helpers (`pack_resource_file`, `openmw_qt_platforms`)
  written out at their one caller.
- The version filled once: a build needs a Git clone, so `GIT_CHECKOUT` and the empty-hash fallback go; the templates
  read `PROJECT_VERSION*`, and `cmake/GitVersion.cmake` fills the commit and the tag at every build.
- `extern/`'s vendored projects (`osgQt`, `oics`, `osg-ffmpeg-videoplayer`, `smhasher`, `rgbcx`) as the tree's own
  targets, their warnings off from the directory's flags.
- The test binaries' `-Og` an interface target; the system's test support one object library; CTest runs each
  benchmark's dry run in place of CI's step.
- `omw translations [--purge]` in place of the `translations` and `purge-obsolete-translations` targets; `.qm` files by
  `qt_add_lrelease` for the deprecated `qt_add_translation`; Qt's own translations from `QT6_INSTALL_TRANSLATIONS`,
  `qtbase_` alone.
- The install: programs named once in `cmake/Install.cmake`; each program's own PDB (`$<TARGET_PDB_FILE>`); the game's
  and the CS's console by `WIN32_EXECUTABLE` on `$<CONFIG:Release>`; `install_fork_licenses` and `files/licenses/`;
  `InstallRequiredSystemLibraries`, as the fork ships a portable folder and no installer. An archive carries the
  programs, the licences, the configuration, the settings' defaults, the controller mappings and the run-time resources,
  without `README.md`, `AUTHORS.md`, the changelog, the rasterizer's `resources/shaders`, the post-processing shaders,
  the water's normal map, `resources/lua_api`, `defaultfilters`, the PDBs (published apart) and the AppStream metadata
  (`files/openmw.appdata.xml`, kept deleted), which an AppImage does not need.
- `openmw-lib` an object library: under MSVC's LTCG its archive reached 3.6 of the 4 GB `lib.exe` writes (LNK1248).

**Out of the tree**:

- GitLab's pipeline, macOS and Android: `.gitlab-ci.yml`, `.gitlab/`, the `CI/` scripts only it ran,
  `docker/generic-linux`, macOS's bundle (`files/mac`, `SignMacApplications.cmake`, `Files::MacOsPath`, every `APPLE`,
  `__APPLE__` and `Q_OS_MAC` branch), Android's entry (`androidmain.cpp`), `Files::AndroidPath`, its ICU cross-build,
  gl4es's manual start (`OPENMW_GL4ES_MANUAL_INIT`) and every `ANDROID` branch; with them the from-source and static
  builds of OSG, MyGUI, Bullet and ICU (`OSG_STATIC`, `MYGUI_STATIC`, `BULLET_STATIC`, `QT_STATIC`,
  `WholeArchive.cmake`) and the FreeBSD, OpenBSD and MinGW branches. `omw integration` pins the example suite's revision
  (`EXAMPLE_SUITE_REVISION`), which `upstream.yml` copies from upstream's pipeline. Cost: also a conflict with every
  upstream change inside a macOS or Android branch of a kept file.
- `docker/`, upstream's Ubuntu build, which cannot build through `omw`; the release container is
  `env/release-base.Dockerfile`.
- Upstream's project files with no use here: `.resubmitted_merge_requests.txt`, `.readthedocs.yaml`, `CONTRIBUTING.md`
  (the fork's rules are `AGENTS.md`), the Teal declarations and size scripts GitLab ran, `osg_stats.py` and
  `HOWTO-benchmark.md`; upstream's `docs/` (the user manual, its Docker build, the Doxygen reference and the `doc.hpp`
  files, `openmw-stage1.md`) and `manual/`. `docs/` holds the fork's own, the manual's `[RTX]` part in
  `docs/settings.md`; `.editorconfig` covers the fork's shaders and loses stages with no file.

**The fork's own files**: `README.md`; `env/`, upstream's `CI/` renamed for what it holds; the workflows; the root
tooling (`CMakePresets.json`, `.clangd`, `.zed/`, `omw`, `omw.cmd`, `.claude/skills/`, `.gitattributes`, `.gitignore`);
and `AUTHORS.md` rewritten as Markdown, every list sorted, the fork's section first, no name dropped. Cost: a conflict
with every upstream edit to `AUTHORS.md`, which the merge resolves by adding the name to its list.
