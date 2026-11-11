# The CMake build: one way to do each thing, nothing a pinned set makes constant

Plan to simplify the CMake files, found by reading every `CMakeLists.txt` and `cmake/*.cmake` outside
`vcpkg/ports/` and the Doxyfiles on 2026-11-11. Delete each step when it lands. When the last one lands, move
the lasting decisions into `docs/rtx/build.md` and delete this file.

## Decisions

- **Kept, and not to be proposed again without a new reason:**
  - `cmake/Flags.cmake` read through `CMAKE_USER_MAKE_RULES_OVERRIDE`, and `openmw_link_tree`'s walk that
    links `openmw-system` and `openmw-checks`: each is a decision `build.md` records.
  - `rtx_compile_shader`, `openmw_rtx_layer` and `rtx_make_table`, which do real work.
  - `pack_resource_file`, which base64-encodes a file and records it as a configure dependency.
  - The platform branches that choose a file per system (platform, crashcatcher, DLSS, NGX, the Windows `.rc`
    files), as `AGENTS.md`'s conventions ask.
  - The GCC-version-gated warnings in `cmake/Checks.cmake`: CI uses gcc-toolset-15 and a desk uses GCC 16.
  - The Doxygen branch in `docs/`: a desk's own tool, which no build needs.
- **`files/data-mw` registers no configure dependency of its own on `files/data/builtin.omwscripts`.**
  `files/data`'s `configure_file` of that file already puts it in the regeneration rule's inputs (checked in
  `build-release/build.ninja`). Registering it a second time under another spelling is the Ninja error
  "multiple rules generate" ([CMake issue 20181](https://gitlab.kitware.com/cmake/cmake/-/issues/20181)),
  which is why upstream added the guard. Every preset uses Ninja, so the only side that runs is the one that
  registers nothing, and the block goes.
- **The driver owns the translations' source list** (decided 2026-11-11). `tools/omw` holds the folders
  lupdate scans. One verb updates the `.ts` files and the lint checks them, so CI's checks job stays free of
  any build, and the `.ts` files keep the strings they have. Extraction outside the build is how KDE works
  (`Messages.sh`). Refused: Qt's target-based `qt_add_translations(SOURCE_TARGETS ...)`, which moves the lint
  into a configured build and scans every source of `openmw-components-qt`, adding strings to upstream's
  `.ts` files that conflict at every merge; a data file read by both CMake and the driver, which keeps two
  routes.
- **Upstream's files: every step approved** (2026-11-11), steps 1 to 6, the version templates reading
  `PROJECT_VERSION` among them, and step 5 on the condition its CI measurement holds. When each step lands,
  add a line to `.notes/UPSTREAM.md` naming what it changed of upstream's and why (`AGENTS.md`, Rules). The
  build rewrite already made these files differ from upstream's, so each step adds little cost to merges.

## Steps

Each step is a configure, a build of every flavour this desk builds, and `./omw test`; the last one ends with
`./omw gate`. A step that changes what is installed also runs `omw archive` and compares the archive's file
list against the one before.

### 1. Dead code

- `cmake/escape_regex_control_characters.cmake`: `CMakeLists.txt:36` includes it and nothing calls it. Delete
  the module and the include. If it is ever needed, CMake 4.4 has `string(REGEX QUOTE)` (checked in the
  pinned 4.4.5's `--help-command string`).
- `OPENMW_VERSION_COMMITDATE` (`CMakeLists.txt:49-68`): runs `git log` at every configure, and no template or
  source reads it. Delete it with its `execute_process`.
- `set(CMAKE_CXX_CLANG_TIDY "")` in `extern/CMakeLists.txt` and `cmake/Dependencies.cmake`: nothing turns
  clang-tidy on.
- `if(NOT DEFINED OPENMW_RESOURCES_ROOT) return()` at the top of `files/data`, `files/data-mw`,
  `files/shaders` and `files/lua_api`'s `CMakeLists.txt`: the root sets it before `add_subdirectory(files)`.
- `COMPONENT "openmw"` and `COMPONENT "opencs"` in `cmake/Install.cmake`: nothing installs by component, as
  CPack is refused.
- `files/lang`'s fallback to `qt_<lang>.qm`: every language the programs have (bg, de, en, fr, pl, ru, sv)
  has `qtbase_<lang>.qm` in the pinned Qt. Keep `qtbase_` and the fatal error.
- `-Wno-maybe-uninitialized` on `apps/openmw/mwmechanics/character.cpp`: `cmake/Checks.cmake` already turns
  it off for every GCC target of the tree.

### 2. What the pinned set makes constant

- `cmake/CheckOsgMultiview.cmake`: a `try_compile` against the pinned OSG fork, which always has multiview.
  Define `OSG_HAS_MULTIVIEW` on `OpenSceneGraph::headers` unconditionally and delete the module with its
  `try-compile` folder.
- `files/data-mw`'s `if(NOT CMAKE_GENERATOR MATCHES "Ninja")` block around `CMAKE_CONFIGURE_DEPENDS` goes
  whole (see Decisions). Verify that an edit to `files/data/builtin.omwscripts` still regenerates
  `vfs-mw/builtin.omwscripts`.
- `OPENMW_USE_UNSHIELD` (`cmake/Options.cmake`): the tree's own spelling of `NOT WIN32`, which `build.md`
  refuses ("variables of the tree's own"). Use `if(NOT WIN32)` in `apps/wizard/CMakeLists.txt` and
  `cmake/Dependencies.cmake`. The wizard's own `OPENMW_USE_UNSHIELD` compile definition stays, since
  upstream's source reads it.

### 3. Helpers removed or reshaped

- `copy_resource_file` and `configure_resource_file` (`cmake/OpenMWMacros.cmake`): `configure_file` with a
  path joined on. Call `configure_file` at the 16 call sites. `copy_all_resource_files` becomes a `foreach`
  at its two call sites.
- The version step (`get-version` in `components/CMakeLists.txt`, `cmake/GitVersion.cmake`): it passes 12
  `-D` values and `MACROSFILE` to a build-time script. Instead, configure `version.cpp.in` and `version.in`
  at configure time with everything but the commit and tag hashes, and let the build-time script fill in
  only those two. Its `string(COMPARE)` chains become `if(... EQUAL 0)`. The templates read
  `PROJECT_VERSION*`, and the `OPENMW_VERSION_*` aliases go.
- `openmw_add_test` (`cmake/Tests.cmake`):
  - It returns silently when its target is missing, and guards `-Og` with an `OPENMW_TEST_OPTIONED`
    property. Replace that with an `INTERFACE` target holding the test compile options, linked by each test
    executable where it is defined.
  - `cmake/Tests.cmake` is included only under `BUILD_TESTING`, and a missing target is a fatal error.
- The test platform file (`testplatformwin32.cpp` or `testplatformposix.cpp`) is chosen in both
  `apps/openmw_tests/CMakeLists.txt` and `apps/components_tests/rtx/tests.cmake`. Make it one small library
  that both link.

### 4. One route per job

- `extern/osgQt`: `qt_wrap_cpp` becomes `AUTOMOC`, as every other Qt target has.
- Translations (`files/lang/CMakeLists.txt`, `tools/omw/lint.py`):
  - `qt_add_translation` is deprecated: Qt's docs say to use `qt_add_lrelease()` or `qt_add_translations()`
    ([Qt](https://doc.qt.io/qt-6/qtlinguist-cmake-qt-add-translation.html)).
  - The lupdate source lists exist twice, in the `translations` and `purge-obsolete-translations` targets
    and in `TRANSLATIONS` in `lint.py`. The driver keeps the one list (see Decisions): an
    `omw translations [--purge]` verb rewrites the `.ts` files beside the lint that checks them, both
    from `TRANSLATIONS`. The two CMake targets go, `qt_add_lrelease` builds the `.qm` files, and the
    lint's message names the new verb.
  - Qt's translations folder comes from `${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}`, which Qt's own
    config defines (`share/Qt6/QtInstallPaths.cmake` in the set), instead of `qmake -query` at configure.
- unshield: found the way MyGUI and LuaJIT are, ports with no CMake config: `find_path`, `find_library` and
  an imported target linking zlib, instead of pkg-config. `find_package(PkgConfig)` goes with it.
- `cmake/Dependencies.cmake`: one `find_package(Boost ... COMPONENTS iostreams program_options filesystem)`
  instead of two calls, and googletest and benchmark in one `if(BUILD_TESTING)`.
- The build's runtime folder has three names, `PROJECT_BINARY_DIR`, `OPENMW_RESOURCES_ROOT` (9 uses) and
  `CMAKE_RUNTIME_OUTPUT_DIRECTORY` (6 uses), all set to one value in `CMakeLists.txt`. Keep
  `CMAKE_RUNTIME_OUTPUT_DIRECTORY`, CMake's own, for what lands beside the programs, and drop
  `OPENMW_RESOURCES_ROOT`.
- The Windows PDB install (`cmake/Install.cmake`): a walk of the build folder with 14 exclude patterns naming
  build-tree folders, so any new build folder installs silently. Use
  `install(FILES $<TARGET_PDB_FILE:target> OPTIONAL ...)` beside each program's `install(TARGETS)`, in the
  configurations the walk names.
- The game's console window (`apps/openmw/CMakeLists.txt`): `if(MSVC)` plus a `/SUBSYSTEM` generator
  expression becomes `WIN32_EXECUTABLE` set to `$<CONFIG:Release>`. The property takes generator
  expressions and is ignored off Windows (`cmake --help-property WIN32_EXECUTABLE`).
- `cmake/Options.cmake`'s sanitizer flags drop `-fno-omit-frame-pointer`, which `cmake/Flags.cmake` already
  sets in every configuration. MSVC's Release drops `/Ob2`, which `/O2` already includes.

### 5. Every file compiled alike

- `/cgthreads1` on 8 files (`components/CMakeLists.txt`, `apps/openmw/CMakeLists.txt`,
  `apps/openmw_tests/CMakeLists.txt`), upstream's since 2025-12-24, for "CI's 8 GB". This repository is
  public, and GitHub's standard Windows runners for public repositories have 16 GB. Run the Windows CI leg
  without it, read the job's peak memory, and drop it if the job holds.
- The comment on the compiler minimums in `cmake/Checks.cmake` still names Ubuntu 26.04's GCC and VS 2022
  17.14. Name what builds now: AlmaLinux 8's gcc-toolset-15, Clang 23 and Visual Studio 2026.

### 6. `extern/` in the tree's style

- `osgQt`, `oics`, `osg-ffmpeg-videoplayer` and `smhasher` hold their target names in variables and call
  `target_link_libraries` without `PRIVATE`/`PUBLIC`. `rgbcx` silences warnings with
  `if(MSVC) /w else -w`. Rewrite them as the tree's own targets are: the name written out, `target_sources`,
  keyword links, and `$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>`.
