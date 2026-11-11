# The build: one way to do each thing, and a green run that prints no warning

Plan to simplify the CMake files and clear every warning a green run prints. Sources:
- a reading on 2026-11-11 of every `CMakeLists.txt` and `cmake/*.cmake` outside `vcpkg/ports/` and the
  Doxyfiles;
- the last green CI run ([42985566605](https://github.com/xorza/openmw-rtx/actions/runs/42985566605)) and the
  last dependency-set run that built every port
  ([42939211492](https://github.com/xorza/openmw-rtx/actions/runs/42939211492)).

Neither run had a GitHub annotation, and no warning came from the tree's own code.

Delete each step when it lands. When the last one lands, move the lasting decisions into `docs/rtx/build.md`
and delete this file.

**Where it runs:** on `deps-set-size`, after that branch's own work, as one branch with one merge. Steps 1 to 10
leave the set's inputs alone, so they build against `deps-8948c4c640db7edd`, which this desk built. Before
the first CI run of this plan (step 6's Windows check), the Dependency sets workflow must have published that
set and `pins.py` must name it. Step 11 then replaces it with the set its own inputs name.

## Decisions

- **Kept, and not to be proposed again without a new reason:**
  - `cmake/Flags.cmake` read through `CMAKE_USER_MAKE_RULES_OVERRIDE`, and `openmw_link_tree`'s walk that
    links `openmw-system` and `openmw-checks`: each is a decision `build.md` records.
  - `rtx_compile_shader`, `openmw_rtx_layer`, `rtx_make_table` and `pack_resource_file`, which do real work.
  - The platform branches that choose a file per system (platform, crashcatcher, DLSS, NGX, the Windows `.rc`
    files), as `AGENTS.md`'s conventions ask.
  - The GCC-version-gated warnings in `cmake/Checks.cmake`: CI uses gcc-toolset-15 and a desk uses GCC 16.
  - The Doxygen branch in `docs/`: a desk's own tool, which no build needs.
  - The Qt ports' own messages from vcpkg (`qtbase currently requires packages from the system package
    manager`, `qtwayland requires libwayland-dev`). They print on purpose, only when the set rebuilds Qt, and
    only a copy of Qt's biggest port of our own removes them, which means maintaining it at every registry bump
    and rebuilding all of Qt on every change to it.
- **A third party's warnings are switched off, not fixed.** The tree does not patch fetched or vendored code
  for its compiler's opinions, and a warning nobody acts on teaches every reader to skip warnings. The tree's
  own targets keep every check, with warnings as errors.
  - On GCC and Clang, `-w` in the third-party folders' `CMAKE_C_FLAGS` and `CMAKE_CXX_FLAGS`. `-w` wins over
    any `-W` flag a project adds after it: checked with GCC 16 and Clang 23, `-w -Wall -Wextra` and
    `-Wall -w` both print nothing where `-Wall` alone warns.
  - On MSVC, `/w` cannot do the same. The projects set their own levels after the directory's flags, through
    genexes, `add_compile_options` and `CMAKE_CXX_FLAGS` (yaml-cpp `/W3`, SDL3 `/W3`, Benchmark and Crashpad
    `/W4`, VMA `/W3`), so `/w` lands before or after them, and cl prints D9025 ("overriding") for each file.
    MSVC therefore takes `/wd<N>` on the target that raises warning N, which conflicts with no level. Today
    that is C4267 on yaml-cpp alone.
- **One compiler for everything built on Linux, tools included** (decided 2026-11-11). The set and the tree
  already build with gcc-toolset-15, as the programs link libstdc++ statically. The env script's tools
  (autoconf, zsync, `wayland-scanner`) ship nothing, but building them with the same GCC leaves nothing
  standing on a GCC only a package dependency installs. Refused: AlmaLinux 8's system GCC 8.5 for them.
- **Output appears on failure only.** A smoke run (benchmarks, integration tests) prints its detail when it
  fails, and its logs are kept either way.
- **CMake's own warnings for authors are errors** (decided 2026-11-11). The presets turn CMake's dev and deprecation warnings into
  errors (`"errors": {"dev": true, "deprecated": true}` in `common`), CMake's canonical switch
  ([cmake-presets(7)](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html)), so a new one stops
  the configure rather than scrolling past. The green run prints none today (no `CMake Warning (dev)` or
  `CMake Deprecation Warning` in run 42985566605).
  - Refused: failing a job on any line its log matches `warning`, which tests named after warnings, and the
    content's own log, would trip.
- **`files/data-mw` registers no configure dependency of its own on `files/data/builtin.omwscripts`.**
  `files/data`'s `configure_file` of that file already puts it in the regeneration rule's inputs (checked in
  `build-release/build.ninja`). Registering it a second time under another spelling is the Ninja error
  "multiple rules generate" ([CMake issue 20181](https://gitlab.kitware.com/cmake/cmake/-/issues/20181)),
  which is why upstream added the guard. Every preset uses Ninja, so the only side that runs is the one that
  registers nothing, and the block goes.
- **The driver owns the translations' source list** (decided 2026-11-11). `tools/omw` holds the folders
  lupdate scans. One verb updates the `.ts` files and the lint checks them, so CI's checks job stays free of
  any build, and the `.ts` files keep the strings they have. Extraction outside the build is how KDE works
  (`Messages.sh`). Refused:
  - Qt's target-based `qt_add_translations(SOURCE_TARGETS ...)`, which moves the lint into a configured build
    and scans every source of `openmw-components-qt`, adding strings to upstream's `.ts` files that conflict at
    every merge;
  - a data file read by both CMake and the driver, which keeps two routes.
- **Upstream's files: every change approved** (2026-11-11). This covers every CMake step below, the version
  templates reading `PROJECT_VERSION` among them, and `/cgthreads1` on the condition its CI runs hold. When
  each step lands, add a line to `.notes/UPSTREAM.md` naming what it changed of upstream's and why
  (`AGENTS.md`, Rules). The build rewrite already made these files differ from upstream's, so each step adds
  little cost to merges. The warnings steps touch only the fork's files: `env/`, `.github/`,
  `cmake/Dependencies.cmake`, `cmake/Tests.cmake` and the overlays.
## Order and checks

The steps run cheapest and widest first:
- dead code first, so later steps rewrite less;
- the steps that touch one file family follow, each in a single pass;
- the `/cgthreads1` measurement comes next to last, on the build every earlier step has shaped;
- the set's inputs come last and land together, so the set rebuilds once.

Each step is a configure, `./omw build`, and `./omw test`. Two kinds of step check more:
- **A step that changes Windows, a sanitizer, or what is installed** builds what it changed: the Windows CI leg
  (`gh workflow run ci.yml --ref <branch>`, batched at the end of steps 6 to 9), the asan flavour, or
  `omw archive` with its file list compared against the one before.
- **A step that removes a warning** shows it gone from the log of the run it changes. That means no line
  matching `warning` in that step's log (case-insensitive), with a known hit from before the step shown to
  match the same search.

All four flavours and `./omw gate` run once, at the end.

## Steps

### 1. Dead code and stale words

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
- `-Wno-maybe-uninitialized` on `apps/openmw/mwmechanics/character.cpp`: `cmake/Checks.cmake` already turns
  it off for every GCC target of the tree.
- `cmake/Options.cmake`'s sanitizer flags repeat `-fno-omit-frame-pointer`, which `cmake/Flags.cmake` sets in
  every configuration. MSVC's Release repeats `/Ob2`, which `/O2` includes.
- The comment on the compiler minimums in `cmake/Checks.cmake` names Ubuntu 26.04's GCC and VS 2022 17.14.
  Name what builds now: AlmaLinux 8's gcc-toolset-15, Clang 23 and Visual Studio 2026.

### 2. What the pinned set makes constant

- `cmake/CheckOsgMultiview.cmake`: a `try_compile` against the pinned OSG fork, which always has multiview.
  Define `OSG_HAS_MULTIVIEW` on `OpenSceneGraph::headers` unconditionally, and delete the module with its
  `try-compile` folder. An OSG without it would fail to compile at the first use of
  `FACE_CONTROLLED_BY_MULTIVIEW_SHADER`, not quietly lose the feature.
- `files/data-mw`'s `if(NOT CMAKE_GENERATOR MATCHES "Ninja")` block around `CMAKE_CONFIGURE_DEPENDS` goes
  whole (see Decisions). Check that an edit to `files/data/builtin.omwscripts` still regenerates
  `vfs-mw/builtin.omwscripts`.
- `OPENMW_USE_UNSHIELD` (`cmake/Options.cmake`): the tree's own spelling of `NOT WIN32`, which `build.md`
  refuses ("variables of the tree's own"). Use `if(NOT WIN32)` in `apps/wizard/CMakeLists.txt` and
  `cmake/Dependencies.cmake`. The wizard's own `OPENMW_USE_UNSHIELD` compile definition stays, since
  upstream's source reads it.

### 3. Resources: `configure_file` and one folder name

One pass over `files/`, `components/`, `apps/rtxtool` and `cmake/OpenMWMacros.cmake`.
- `copy_resource_file` and `configure_resource_file`: `configure_file` with a path joined on. Call
  `configure_file` at the 16 call sites. `copy_all_resource_files` becomes a `foreach` at its two call sites.
- The build's runtime folder has three names: `PROJECT_BINARY_DIR`, `OPENMW_RESOURCES_ROOT` (9 uses) and
  `CMAKE_RUNTIME_OUTPUT_DIRECTORY` (6 uses), all one value in `CMakeLists.txt`. Keep
  `CMAKE_RUNTIME_OUTPUT_DIRECTORY`, CMake's own, for what lands beside the programs, and drop
  `OPENMW_RESOURCES_ROOT`.
- Check that the build's `resources/` tree is byte-for-byte what it was (`diff -r` against a copy taken
  before).

### 4. The version, filled once

- `get-version` (`components/CMakeLists.txt`) passes 12 `-D` values and `MACROSFILE` to
  `cmake/GitVersion.cmake` at every build. Instead:
  - Configure `version.cpp.in` and `version.in` at configure time, with the commit and tag placeholders set to
    their own `@...@` text so that they survive.
  - The build-time script fills in only those two, from `git rev-parse HEAD` and
    `git rev-list --tags --max-count=1`, and writes through `copy_if_different` as now.
  - Its `string(COMPARE)` chains become `if(... EQUAL 0)`.
  - The templates read `PROJECT_VERSION*`, and the `OPENMW_VERSION_*` aliases go.
- Check that a commit without a reconfigure moves the built version's hash, and that a tree without `.git`
  configures with empty hashes as now.

### 5. Dependencies, each found one way

- unshield: found the way MyGUI and LuaJIT are, ports with no CMake config: `find_path`, `find_library` and an
  imported target linking zlib, instead of pkg-config. `find_package(PkgConfig)` goes with it.
- One `find_package(Boost CONFIG REQUIRED COMPONENTS iostreams program_options filesystem)` instead of two
  calls, and googletest and benchmark in one `if(BUILD_TESTING)`.

### 6. Third-party code, in one style and quiet

- Fetched projects (`cmake/Dependencies.cmake`): inside the `block()` that fetches SDL3, Recast, yaml-cpp,
  Google Test, Google Benchmark, volk, VMA and Crashpad, append `-w` to `CMAKE_C_FLAGS` and `CMAKE_CXX_FLAGS`
  when the compiler is GCC or Clang. Each fetched folder takes the value in force when CMake finishes it,
  which is inside the block, so the tree's own folders never see it. On MSVC, `/wd4267` on `yaml-cpp` (see
  Decisions). This removes:
  - Clang's `-Wnontrivial-memcall` in Recast and `-Wunused-but-set-global` ×9 in SDL;
  - MSVC's C4267 ×6 in yaml-cpp;
  - the asan job's `-Wmaybe-uninitialized` inside libstdc++'s `std::function`, which Google Benchmark's
    `std::regex` instantiates.
- Vendored projects (`extern/`): the same `-w` set in `extern/CMakeLists.txt`'s own directory scope. Then each
  is rewritten as the tree's own targets are:
  - `osgQt`, `oics`, `osg-ffmpeg-videoplayer` and `smhasher` hold their target names in variables and call
    `target_link_libraries` without `PRIVATE`/`PUBLIC`: the name written out, `target_sources`, and keyword
    links;
  - `osgQt`'s `qt_wrap_cpp` becomes `AUTOMOC`, as every other Qt target has;
  - `rgbcx`'s `if(MSVC) /w else -w` goes into the directory's flags like the rest.
- Check that no `-w` reaches a file under `components/` or `apps/` (`compile_commands.json`), that the
  sanitizers still instrument the fetched code (their flags stay in the same variables), and that the Windows
  leg prints no D9025.

### 7. Tests, benchmarks and smoke runs (`cmake/Tests.cmake`, `.github/workflows/ci.yml`)

- `openmw_add_test` returns silently when its target is missing, and guards `-Og` with an
  `OPENMW_TEST_OPTIONED` property. Replace both:
  - an `INTERFACE` target `openmw-test-options` holds the test compile options, and each test executable
    links it where it is defined;
  - `cmake/Tests.cmake` is included only under `BUILD_TESTING`, and a missing target is a fatal error.
- The test platform file (`testplatformwin32.cpp` or `testplatformposix.cpp`) is chosen in both
  `apps/openmw_tests/CMakeLists.txt` and `apps/components_tests/rtx/tests.cmake`. Make it one small library
  that both link.
- The three benchmarks become CTest tests in `cmake/Tests.cmake`, run with `--benchmark_dry_run` (Google
  Benchmark 1.10: one iteration and no measurement; checked on `openmw_esm_refid_benchmark`). Each
  `omw test` and each CI test run then executes them, with output only on failure. Delete CI's step
  "Benchmarks, run once each as upstream runs them", which prints `***WARNING*** ASLR is enabled`, as Docker's
  seccomp profile refuses the `personality` call `BENCHMARK_MAIN` makes, and `***WARNING*** Library was built
  as DEBUG`. Both concern timings a smoke run never takes.
- `omw debug integration --verbose` (`ci.yml`) prints the game's whole log, including the example suite's
  own content warnings (`addAnimSource: can't find bone ... in meshes/land_racer.dae`, dozens a run). Drop
  `--verbose`: `scripts/integration_tests.py` still prints every line when a run ends in a fatal error, and
  copies `openmw.log` into its output folder, which the job keeps.

### 8. Translations (`files/lang/CMakeLists.txt`, `tools/omw/lint.py`)

- The lupdate source lists exist twice, in the `translations` and `purge-obsolete-translations` targets and in
  `TRANSLATIONS` in `lint.py`. The driver keeps the one list (see Decisions): an
  `omw translations [--purge]` verb rewrites the `.ts` files beside the lint that checks them, both from
  `TRANSLATIONS`, and the lint's message names the verb. The two CMake targets go.
- `qt_add_translation` is deprecated: Qt's docs say to use `qt_add_lrelease()` or `qt_add_translations()`
  ([Qt](https://doc.qt.io/qt-6/qtlinguist-cmake-qt-add-translation.html)). `qt_add_lrelease` builds the `.qm`
  files.
- Qt's translations folder comes from `${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}`, which Qt's own
  config defines (`share/Qt6/QtInstallPaths.cmake` in the set), instead of `qmake -query` at configure.
- The fallback to `qt_<lang>.qm` goes: every language the programs have (bg, de, en, fr, pl, ru, sv) has
  `qtbase_<lang>.qm` in the pinned Qt. Keep `qtbase_` and the fatal error.
- Check that the launcher's and the wizard's `.qm` files are byte-for-byte what they were, and that
  `omw translations` on an unchanged tree changes no file.

### 9. Install and Windows properties

- The Windows PDB install (`cmake/Install.cmake`): a walk of the build folder with 14 exclude patterns naming
  build-tree folders, so any new build folder installs silently. Use
  `install(FILES $<TARGET_PDB_FILE:target> TYPE BIN OPTIONAL CONFIGURATIONS Debug RelWithDebInfo)` beside
  each program's `install(TARGETS)`. The configurations are the ones the walk names.
- The game's console window (`apps/openmw/CMakeLists.txt`): `if(MSVC)` plus a `/SUBSYSTEM` generator
  expression becomes `WIN32_EXECUTABLE` set to `$<CONFIG:Release>`. The property takes generator expressions
  and is ignored off Windows (`cmake --help-property WIN32_EXECUTABLE`).
- The presets turn CMake's dev and deprecation warnings into errors (see Decisions).
- Check the Windows archive's file list against the one before, and that the debug build opens its console
  while the release build does not.

### 10. `/cgthreads1`, measured

- `/cgthreads1` sits on 8 files (`components/CMakeLists.txt`, `apps/openmw/CMakeLists.txt`,
  `apps/openmw_tests/CMakeLists.txt`). It is upstream's since 2025-12-24, for "CI's 8 GB". This repository is
  public, and GitHub's standard Windows runners for public repositories have 16 GB.
- Run the Windows leg three times without it. Actions reports no peak memory, so the measure is three passes
  in a row without `C1060` (out of heap space) or a lost runner, together with the leg's build time against
  the run before. Drop it if all three pass. If one fails, keep it and correct the comment's figure.

### 11. The set's inputs, in one rebuild

Everything here changes the set's digest (`deps.SET_INPUTS`): it lands together, `omw deps-set` builds the
Linux set to check it, and the Dependency sets workflow publishes it, which `pins.py` then names.
- **The env's tools built with GCC 15, quietly** (`env/install_almalinux_deps.sh`):
  - `source /opt/rh/gcc-toolset-15/enable` right after the `dnf install`, so autoconf, zsync and
    `wayland-scanner` build with the GCC everything else does. Today `gcc` there is AlmaLinux 8's system 8.5,
    installed only because `libtool`'s package depends on it. `release-base.Dockerfile` sets the toolset's
    `PATH` only after running the script, so the script enables it itself.
  - zsync's `CFLAGS="-O2 -std=gnu17 -fpermissive"` becomes `CFLAGS="-O2 -fpermissive -w"`. GCC 14 and later
    make its old pointer code an error, which `-fpermissive` turns back into a warning (it applies to C from
    GCC 14 on), and `-w` hides that warning. `-w` alone hides warnings and not errors (checked with GCC 16).
    This removes the 25 `-fpermissive is valid for C++` warnings a Linux job prints today, GCC 8.5 not taking
    the flag for C, and the warnings of zsync's own code (`_BSD_SOURCE` deprecated ×15, `inflate.c`,
    `client.c`, `zsync.c`). `-std=gnu17` goes, as it is GCC 15's default. The comment says GCC 15 builds it
    and the flags quiet a tool the build only runs.
- **A `wayland-scanner` that knows SDL's protocols** (`env/install_almalinux_deps.sh`):
  - SDL3 runs the system's `wayland-scanner`, which is 1.21 on AlmaLinux 8. SDL's protocol files use the
    `deprecated-since` attribute, which the scanner's built-in schema gains in wayland 1.23 (checked in
    `protocol/wayland.dtd` at 1.21.0, 1.22.0, 1.23.0 and 1.24.0). So each run prints "XML failed validation
    against built-in DTD" and the validity errors after it.
  - Build `wayland-scanner` 1.24.0 from its release archive, pinned by SHA-512, into `/usr/local`, as autoconf
    and zsync are built. That is `meson setup -Dlibraries=false -Ddocumentation=false -Dtests=false`, then
    the scanner alone. Meson must be at least 0.57 (`meson_version` in wayland's `meson.build`): find out
    whether AlmaLinux 8's repositories carry one, otherwise use a pinned meson in a Python 3.12 venv.
  - Check:
    - SDL takes `/usr/local/bin/wayland-scanner`;
    - the code it generates compiles and links against the system's libwayland 1.21, which the system keeps
      (`build.md`, Dependencies), as only the generator changes;
    - the game opens a window under Wayland;
    - the vcpkg set's qtwayland, whose build runs the same scanner, still builds.
- **collada-dom's config looks for no other Boost** (`vcpkg/ports/collada-dom`). On Windows alone, the config
  the overlay installs runs `find_package(Boost 1.92 EXACT COMPONENTS thread date_time)` with dynamic-Boost
  settings. `1.92` does not exactly match the set's 1.92.0, which is the Windows configure's "Could not find a
  configuration file for package Boost that exactly matches requested version" warning, and the set is
  static. Drop the block from the config template in the overlay. The tree links Boost through its own
  targets (`osgdb_dae_calls` in `cmake/Dependencies.cmake`).
- **`openmw-osg` passes only what OSG reads** (`vcpkg/ports/openmw-osg`). On Windows it passes
  `OSGVIEWER_USE_XRANDR` and `_OPENTHREADS_ATOMIC_USE_*`, which OSG's CMake reads none of there ("variables
  are not used"). Pass the X11 one on Linux alone, and delete the Windows atomics options.
- **The vcpkg tool at its newest release** (`tools/omw/pins.py`, `VCPKG_TOOL`): 2026-09-26 becomes
  2026-10-29, the newest of `microsoft/vcpkg-tool`'s releases on 2026-11-11, both systems' binaries with the
  SHA-256 each release lists. The tool's pins are in the digest (`deps.set_digest`), so it rides this rebuild.
  Read the releases between the two for a change to `export`, binary caching or the overlay handling, which
  `omw deps-set` uses.
- **One AlmaLinux 8 image, pinned by digest, in the set's name too.**
  - Today `ci.yml`, `deps.yml` and `rtx-release.yml` run in `almalinux:8` by tag, while
    `env/release-base.Dockerfile` pins `almalinux:8@sha256:2d0f2eb2…`. The two agree today (Docker Hub's
    `almalinux:8` digest on 2026-11-11), but when the tag moves, CI, the published set and a release each
    build on another image than the desk does.
  - The Dockerfile's `FROM` stays the one place the digest is written. The workflows' `container:` entries
    name the same `almalinux:8@sha256:…`, and `omw lint workflows` fails when any of them differs from the
    Dockerfile's.
  - The digest is part of what the set is built from, so `deps.SET_INPUTS` gains `env/release-base.Dockerfile`,
    and `SetDigestTest` covers it. A new image then names a new set rather than reusing one built on the old.
  - Dependabot watches only GitHub Actions here and updates neither. Moving the image is a manual pin with its
    own set rebuild, like any other pin.
- **qtbase without `xrender`** (`vcpkg.json`). Qt 6.11 removed X11 native painting (qtbase
  [`93fe512d05`](https://github.com/qt/qtbase/commit/93fe512d053ee57e090e4b150501659e793bc91a)) and with it the
  `xrender` and `xcb_native_painting` features. vcpkg's qtbase port still maps its `xrender` feature onto both,
  so the Linux set's qtbase build warns that `FEATURE_xrender` and `FEATURE_xcb_native_painting` are unused.
  - The port lists a feature turned off as "maybe unused" (`qt_install_submodule.cmake`, "Disable unused
    warnings for disabled features"), so the warning comes only from `vcpkg.json` asking for `xrender`. Drop
    it from qtbase's Linux features.
  - Nothing is lost. Qt draws its cursors and tray icons through xcb-render (`xcb_render_*`, undefined in
    `libQt6XcbQpa.a`), and no archive of the set calls libXrender (no undefined `XRender*` symbol).
  - It rebuilds qtbase and every Qt port once, which this step's set rebuild pays for anyway.
- **vulkan-loader takes no option it does not read** (`vcpkg/ports/vulkan-loader`, an overlay). The registry's
  port finds Python and passes `-DPython3_EXECUTABLE`, which the loader's CMake at 1.4.357.0 never reads, so
  the Linux set's build warns that it is unused (run 43016471135, job 130508789485). vcpkg's master passes it
  the same way.
  - Copy the port from the pinned registry as written, bump `port-version` by 100 as the other overlays do,
    and list `Python3_EXECUTABLE` under `MAYBE_UNUSED_VARIABLES`, the remedy vcpkg's warning names. Python
    stays acquired, in case a later loader reads it.
  - Nothing in the set links the loader. It is the `libvulkan.so.1` the AppImage carries, so only the loader
    rebuilds.
  - When the registry bump brings vulkan-loader 1.4.363.0, the overlay is copied anew from that port with the
    same one-line change, or deleted if that port reads or no longer passes the option.
- **libjpeg-turbo's `NEON_INTRINSICS` waits for the registry bump.** The registry's port passes the ARM-only
  `-DNEON_INTRINSICS=ON` on every system with a `VCPKG_CMAKE_SYSTEM_NAME`, x64 Linux included, so the Linux
  set warns that it is unused (run 43016471135). vcpkg's master already lists it under
  `MAYBE_UNUSED_VARIABLES`, so the next vcpkg release tag clears it with no overlay of ours. Until then it
  warns only when the set rebuilds libjpeg-turbo, which is a cold cache or a change to its inputs.
- Check the published run's logs:
  - no unused-variable warning for `openmw-osg`, qtbase or vulkan-loader (libjpeg-turbo's stays until the
    registry bump);
  - no Boost warning in the Windows configure;
  - each Linux job's container named by the Dockerfile's digest.

  `omw lint workflows` fails on a workflow edited back to the bare tag (the known hit).
