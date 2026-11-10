# Proposal: one canonical, modern build — CMake, the formatter, and only the platforms that ship

Temporary. When it lands, its lasting decisions go into `docs/rtx/architecture.md` ("The build"), each upstream
change it makes is recorded in `.notes/UPSTREAM.md`, and this file is deleted. Read 2026-11-10 at `b11a39ec26`.

Taken by the user on 2026-11-10: every CMake file is rewritten, upstream's included (scope B); GitLab's CI and
everything only it uses go; macOS and Android go, since neither build is planned; `gersemi` is approved as a dependency; the
minimum CMake moves to a recent release and the code for older ones goes.

## Investigation

### CMake

- **64 CMake files**, `docs/Doxyfile*.cmake` and the vendored projects under `extern/*/` left out. The large ones:
  `CMakeLists.txt` 1,379 lines, `components/CMakeLists.txt` 935, `components/rtxvulkan/CMakeLists.txt` 600,
  `apps/components_tests/rtx/tests.cmake` 333, `apps/opencs` 293, `apps/openmw` 291, `extern/CMakeLists.txt` 282.
- **Who owns them.** 10 are the fork's own: `components/rtxvulkan`, `apps/rtxtool`, the two `tests.cmake` of
  `apps/components_tests`, `components/crashcatcher/{sources,link}.cmake`, `extern/crashpad*.cmake`,
  `cmake/FindVulkanHeaders.cmake` and `cmake/Tests.cmake`. The rest are upstream's, and the fork's diff in them
  against the merge base is 173 lines of the root, 261 of `components`, 49 of `components_tests`, 38 of
  `openmw_tests`, 33 of `apps/openmw`, 21 of `extern` and a few lines elsewhere.
- **Upstream's churn.** 165 upstream commits touched a CMake file in the six months to this note: the merge cost B
  accepts.
- **Directory scope against target scope.** `include_directories` 23 times, `add_compile_options` 17,
  `add_compile_definitions` 11, `link_directories` once and writes to `CMAKE_CXX_FLAGS`/`CMAKE_C_FLAGS` 5, against
  `target_include_directories` 3, `target_compile_options` 28 and `target_compile_definitions` 13. The warnings are
  `add_compile_options` at the root after `extern/` is added, so whether a third-party target gets them follows the
  order of the `add_subdirectory` calls.
- **Three ways to list sources.** Stem macros that glob each stem (`add_component_dir`, `add_openmw_dir`,
  `opencs_units`); explicit lists (`rtxvulkan`, `rtxtool`); and `file(GLOB UNITTEST_SRC_FILES …)` holding explicit
  names (`components_tests`). 14 globs, a hand-made unity build (`enable_unity_build`) beside CMake's own
  `UNITY_BUILD`, and `omw`'s listing check to catch a file no list names.
- **42 macros and 36 functions**; 49 `endif(<condition>)` that repeat their `if`; the source root spelled three
  ways: `${CMAKE_SOURCE_DIR}` 50 times, `${OpenMW_SOURCE_DIR}` 59, `${PROJECT_SOURCE_DIR}` 7.
- **56 options in five styles**: `BUILD_*`, `OPENMW_*`, `USE_*`, `*_STATIC` and bare names (`QT_STATIC`,
  `PRECOMPILE_HEADERS_WITH_MSVC`, `DEPLOY_QT_TRANSLATIONS`); `USE_SYSTEM_TINYXML` beside `OPENMW_USE_SYSTEM_*`.
- **Third-party code fetched in four places**: SDL3 at the root, seven projects in `extern/CMakeLists.txt`, volk
  and VMA in `components/rtxvulkan`, three in `extern/crashpad.cmake`; each system-or-fetched choice a hand-made
  `OPENMW_USE_SYSTEM_*` switch. 13 hand-written find modules, `LibFindMacros.cmake` among them.
- **Libraries.** `components` is one static library of every component folder, the ray tracer's core included,
  beside the fork's `openmw-platform-io`, `openmw-rtx-spirv`, `openmw-rtx-vulkan` and `openmw-rtx-tablemakers`.
  Target names mix `components`, `components_qt`, `openmw-platform-io`, `openmw_crashpad_threads` and `openmw-lib`.
  Outside the ray tracer, 28 component folders include one another in one cycle (`esm`, `misc`, `resource`,
  `sceneutil`, `settings`, `vfs`, …), counted from their `#include <components/…>` lines.
- **State between directories.** 6 `CACHE INTERNAL` variables carry paths from the root to the backend and the
  tests (`RTX_SPIRV_DIR`, `RTX_TEST_OUTPUT_DIR`, …).
- **Linking.** The plain `target_link_libraries` signature beside the keyword one (`components-tests`),
  `${CMAKE_THREAD_LIBS_INIT}` where `Threads::Threads` is the target, `gcov` by name.
- **The root.** 1,379 lines: options, version, Git, dependencies, warnings, the ray tracer's folders, every app,
  the tests, and about 400 lines of install and packaging, macOS's bundles among them.
- **Presets.** Version 10: 11 hidden and 10 visible configure presets, 9 test presets, no build or workflow
  presets, so `cmake --build --preset` and `cmake --workflow` do nothing and `omw` is the only way in. Sanitizer
  and per-OS flags travel as `OPENMW_CXX_FLAGS` and `CMAKE_*_FLAGS_INIT`.
- **clangd.** No `compile_commands.json` stands where clangd looks (beside a file or in `build/`), so it parsed
  every file with no include paths; `.clangd` now names `build-full` and translates GCC's flags to Clang's
  (`8b7ce6aa6e`). A header no source beside it includes takes a neighbour's flags (`utf8qtextstream.hpp`).

### GitLab and macOS

- **GitLab.** `.gitlab-ci.yml`, `.gitlab/` (issue and merge request templates), and 20 of the 30 files of `CI/`
  that only the GitLab pipeline runs or that only those files call: the Android, macOS, Linux and MSVC
  `before_*` scripts, `ActivateMSVC.ps1`, `activate_msvc.sh`, `{linux,macos}/deps_versions.sh`,
  `linux/before_install_for_vcpkg.sh`, `macos/build.sh`, `check_package.osx.sh`, `compute_comparison_commit.sh`,
  `detect_and_report_ninja_status.sh`, `trigger_ninja_if_necessary.sh`, `run_integration_tests.sh`,
  `store-symbols.sh`, `Store-Symbols.ps1`, `teal_ci.sh` and `ubuntu_gcc_preprocess.sh`.
- **What reads GitLab's files.** `omw integration` takes the example suite's revision out of `.gitlab-ci.yml`
  (`EXAMPLE_SUITE_REVISION`), so an upstream merge that moves the suite moves the run with it.
  `CI/deps_versions.msvc.sh`, `CI/install_{debian,ubuntu}_deps.sh` and `CI/release-base.Dockerfile` are read by
  GitHub's workflows and `omw`, and stay. The `CI/check_*.sh` scripts are named by `omw`'s docstrings as the rules
  it took over, and run by nothing.
- **What stays as text.** Upstream's user documentation names upstream's GitLab as its tracker (`docs/source`,
  `CONTRIBUTING.md`): that is upstream's project, not the fork's CI.
- **macOS.** `files/mac/` (plists, icons, `qt.conf`), `CI/macos/`, `cmake/SignMacApplications.cmake`,
  `components/files/macospath.{cpp,hpp}`, 90 lines of `APPLE`, `OSX` or `Darwin` branches across 27 CMake files (31
  in the root, 16 in `apps/opencs`, 11 in `OpenMWMacros.cmake`), and 11 lines of `__APPLE__` in 8 C++ files. GitHub's
  CI has no macOS leg already (`ci.yml`), and `upstream.yml` names a `macos` workflow in one loop.
- **Android.** `apps/openmw/androidmain.cpp`, `components/files/androidpath.{cpp,hpp}`, gl4es's manual start
  (`components/sdlutil/gl4esinit.*`, `OPENMW_GL4ES_MANUAL_INIT`), ICU's Android cross-build in `extern/`, and `ANDROID`
  branches in 8 CMake and 6 C++ files.

### clang-format

- **The pin.** clang-format 14.0.6 (2022): upstream's GitLab job installs `clang-format-14`, the fork's CI the
  same from apt, `omw format` takes `clang-format-14` from the path on Linux and `bin/clang-format.exe` out of
  LLVM's 14.0.6 Windows installer (pinned by SHA-256) on Windows; `upstream.yml` formats with it as well.
- **The latest.** LLVM 23.1.5, released 2026-11-03; the 23 line started with 23.1.0 on 2026-08-25.
- **What 23 changes.** 11 of the 92 keys of `.clang-format` are deprecated names that 23 still reads but no longer
  writes (`AlwaysBreakTemplateDeclarations` → `BreakTemplateDeclarations`, `SpacesInParentheses` →
  `SpacesInParens`, `KeepEmptyLinesAtTheStartOfBlocks` → `KeepEmptyLines`, `BinPackParameters` an enum, …).
  23.1.4 over the 3,877 C++ files git tracks outside `extern/` reports 2,099 differences in 274 files: 80 the
  fork added, 198 upstream's (63 in `components/esm4`, 52 in `apps/openmw`, 29 in `apps/opencs`).
- **Line endings.** `.gitattributes` fixes them for the scripts only; a C++ file follows the clone's
  `core.autocrlf`, which Git for Windows turns on, and clang-format keeps what a file has.

## Established practice

- **Targets and their properties, not directories.** A target states what it needs and what its users need with
  `PRIVATE`, `PUBLIC` and `INTERFACE`; directory commands give every target below them the same settings and hide
  dependencies ([Pfeifer, "Effective CMake"](https://www.slideshare.net/DanielPfeifer1/effective-cmake);
  [Effective Modern CMake](https://gist.github.com/mbinna/c61dbb39bca0e4fb7d1f73b0d66a4fd1/);
  [CMake Discourse, "Pitfalls of the include_directories function"](https://discourse.cmake.org/t/pitfalls-of-the-include-directories-function/5734);
  [Arias, "It's Time To Do CMake Right"](https://pabloariasal.github.io/2018/02/19/its-time-to-do-cmake-right)).
- **Functions, not macros**: a function has a scope of its own, and a macro writes into its caller's
  ([notes on Effective CMake](https://gist.github.com/ruipacheco/2d18bb3857e849627f91628f66ed66f5)).
- **Sources listed by name**: CMake's own documentation discourages `file(GLOB)` for sources, `CONFIGURE_DEPENDS`
  included, since a build cannot know a file was added and the check costs every build
  ([CMake Discourse](https://discourse.cmake.org/t/is-glob-still-considered-harmful-with-configure-depends/808));
  headers as a `FILE_SET HEADERS` since 3.23
  ([`target_sources`](https://cmake.org/cmake/help/v4.0/_sources/command/target_sources.rst.txt)).
- **System or fetched, by CMake**: `FetchContent_Declare(… FIND_PACKAGE_ARGS …)` since 3.24 tries `find_package`
  first and fetches what it does not find, and `FETCHCONTENT_TRY_FIND_PACKAGE_MODE` switches it for a whole build
  ([FetchContent](https://cmake.org/cmake/help/v3.24/module/FetchContent.html)).
- **Presets for every step**: configure, build and test presets, and a workflow preset that runs the three in
  order, `cmake --workflow --preset <name>`, since 3.25 ([cmake-presets(7)](https://www.mankier.com/7/cmake-presets);
  [CMake Discourse](https://discourse.cmake.org/t/cmake-preset-best-practices-for-build-types/6554)).
- **One compile database for the editor**: each preset exports its own, and clangd is told which with
  `CompilationDatabase` in `.clangd` ([clangd configuration](https://clangd.llvm.org/config)).
- **One exact clang-format for everyone.** Its layout changes with versions, and LLVM does not hold it stable
  across them, so projects pin the exact version for CI and every desk alike
  ([cfe-dev, "Invariant layout between clang-format versions?"](https://lists.llvm.org/pipermail/cfe-dev/2017-June/054351.html);
  [LLVM Discourse, "clang-format version incompatibility"](https://discourse.llvm.org/t/clang-format-version-incompatibility/56683);
  [ACTS, "Pin clang-format version for format check"](https://gitlab.cern.ch/acts/acts-core/-/merge_requests/350)).
- **The pinned binary out of the PyPI wheels**: `clang-format==23.1.5` is built from LLVM's release for Linux,
  Windows and macOS, one wheel of about 2 MB each, and is what `pre-commit/mirrors-clang-format` runs
  ([ssciwr/clang-format-wheel](https://github.com/ssciwr/clang-format-wheel);
  [PyPI](https://pypi.org/project/clang-format/)). LLVM's own packages carry it too, inside 1.9 GB (Linux) and
  859 MB (Windows) archives.
- **A reformat commit kept out of blame**: `.git-blame-ignore-revs`, which `git blame --ignore-revs-file` and
  GitHub read ([git-blame](https://git-scm.com/docs/git-blame)).
- **A merge across a reformat**: a custom merge driver formats the base, ours and theirs before the three-way merge,
  as `merge.renormalize` does for line endings
  ([gitattributes, "Defining a custom merge driver"](https://git-scm.com/docs/gitattributes#_defining_a_custom_merge_driver)).

## Issues

1. Settings at directory scope: includes, warnings, definitions and flags reach targets by where they stand in the
   tree, and the warnings reach third-party targets or not by the order directories are added.
2. Three ways to list sources, globs among them, and a check in `omw` to catch what they miss.
3. Macros that leak variables, and conditions repeated in `endif`.
4. The source root spelled three ways; `CMAKE_SOURCE_DIR` breaks the tree as a subproject.
5. Five naming styles for 56 options, and four for target names.
6. Third-party code fetched from four files, each choice a hand-made switch; old find modules where the
   libraries ship CMake packages.
7. The ray tracer's core compiled into `components`, so its layering is held by a test and not by its links.
8. Paths passed between directories through the cache.
9. A root file of 1,379 lines that holds everything from options to macOS bundles.
10. Presets that configure only: no build, test-and-build or workflow preset works without `omw`.
11. clangd needs a hand-kept list that turns GCC's flags into Clang's (`.clangd`).
12. A CI the fork does not run (GitLab) and a platform it does not build (macOS), kept in the tree and in every
    CMake file a reader passes through.
13. A formatter four years old, fetched three ways (apt, the path, LLVM's Windows installer), and a
    `.clang-format` in names its version deprecates.
14. C++ line endings that follow each clone's settings.

## Decisions

- **Every CMake file, upstream's included (B, taken).** Upstream's CMake commits then conflict at most merges;
  `upstream.yml` resolves them by hand as it resolves C++, and each rewritten upstream file is one entry in
  `.notes/UPSTREAM.md`. Refused by the user: only the fork's files, which kept upstream's directory-scope form in
  the files the fork reads most.
- **GitLab and macOS out (taken).** Every file only GitLab's pipeline runs, every macOS file, branch and
  `__APPLE__` line go; an `if (APPLE OR WIN32)` becomes `if (WIN32)`. `upstream.yml` keeps a list of the paths the
  fork deleted and resolves an upstream edit to one by keeping the delete, so a deleted file never stops a merge.
  The example suite's revision is `EXAMPLE_SUITE_REVISION` in `tools/omw/integration.py`, and `upstream.yml` copies
  it from upstream's `.gitlab-ci.yml` at each merge, which it reads with `git show` and never checks out. Refused:
  `pins.py`, whose pins carry a SHA-256 and key CI's cache of `deps/`, where GitLab's generated archive has none.
- **Targets own every setting.** No `include_directories`, `add_compile_options`, `add_compile_definitions`,
  `link_directories` or flag variable anywhere. The project's checks are one `INTERFACE` target, `openmw-checks`,
  with each compiler's flags under `$<CXX_COMPILER_ID:…>`, which every first-party target links and no third-party
  one does. Refused: directory scope, whose reach follows the tree's order.
- **One way to list sources**: `target_sources(<target> PRIVATE …)` with every file by name, headers as
  `FILE_SET HEADERS`, in the folder's own `CMakeLists.txt`. No globs, no stem macros, no hand-made unity build:
  `CMAKE_UNITY_BUILD` where a preset wants one. `omw`'s listing check stays, now as the guard of one rule.
  Refused: `CONFIGURE_DEPENDS` globs, which CMake discourages.
- **A library a layer for the ray tracer; `components` stays one library.** The core leaves `components` for
  libraries of its own, one a folder group in `architecture.md`'s order, each linking only the layers before it, so
  a wrong include is a link error. Upstream's 28 component folders in one include cycle cannot be libraries of
  their own without C++ changes, so `openmw-components` stays whole, its sources listed by folder. Refused: one
  library for the core, which holds the layering by a test alone.
- **Functions only**, their arguments through `cmake_parse_arguments`; bare `else()`/`endif()`; lowercase commands.
- **Paths from the current folder**: `${CMAKE_CURRENT_SOURCE_DIR}`, `${CMAKE_CURRENT_BINARY_DIR}` and
  `${PROJECT_SOURCE_DIR}`; never `${CMAKE_SOURCE_DIR}` or `${OpenMW_SOURCE_DIR}`. Values between folders travel as
  target properties or a generated header, never `CACHE INTERNAL`.
- **Names.** Targets `openmw-<name>`, kebab case, with an `OpenMW::<Name>` alias that every link names; every
  option `OPENMW_<NAME>`, upstream's renamed, since GitLab's pipeline, their only outside user, goes.
- **Third-party code in one file**, `cmake/Dependencies.cmake`: every `FetchContent_Declare` with
  `FIND_PACKAGE_ARGS`, so a system package is used where found and fetched otherwise, and the presets that must be
  hermetic (Windows, CI) set `FETCHCONTENT_TRY_FIND_PACKAGE_MODE` to `NEVER`. Config packages where the libraries ship
  them (SDL3, yaml-cpp, lz4, recastnavigation) in place of find modules. Refused: a switch a dependency, the
  hand-made form of what CMake provides.
- **A short root.** `CMakeLists.txt` holds the project, the standard, `include()` of `cmake/Options.cmake`,
  `Dependencies.cmake`, `Checks.cmake`, `Tests.cmake`, `Install.cmake` and `Package.cmake`, and the
  `add_subdirectory` calls.
- **Presets for every step.** A build preset and a workflow preset for each visible configure preset, so
  `cmake --workflow --preset debug-linux` configures, builds and tests a clone with no `omw`; `omw` calls the same
  presets and keeps what CMake has no step for (formatting, the listing check, the harness's verbs). Sanitizers as
  an option of the checks target (`OPENMW_SANITIZER=address|thread`) in place of flags in the presets.
- **clangd from the presets.** `.clangd` names `build-full`, as now, and its list of GCC's flags goes: GCC's own
  checks move under `$<CXX_COMPILER_ID:GNU>` in `openmw-checks`, which a compile database still writes, so the list
  becomes the four flags `$<CXX_COMPILER_ID:GNU>` adds. Refused: a compile database copied or linked to the root,
  which follows the last flavour configured.
- **clang-format 23.1.5, exactly, on every desk and in CI.** `omw` fetches the wheel for the system it runs on into
  `deps/`, pinned by SHA-256 in `pins.py`, and unpacks the one binary out of it, as it does LLVM's Windows
  installer today; nothing is installed, and apt's and the path's formatters are no longer asked. Refused: the
  newest on the path, which differs by desk; LLVM's packages, 1.9 GB for one binary; pinning only the major
  version, since a point release can move the layout too.
- **`.clang-format` in 23's names, on `BasedOnStyle`.** The file states a base style and only what differs from it,
  in names 23 writes; the exact pin keeps the base style fixed. Held by a test: 23.1.5's `--dump-config` of the
  new file equals its dump of the old one, so the rename changes no layout.
- **C++ text is LF everywhere**: `*.cpp`, `*.hpp`, `*.h` and `*.inl` `text eol=lf` in `.gitattributes`, and
  `LineEnding: LF` in `.clang-format`, so a Windows clone formats byte for byte as a Linux one.
- **One reformat commit, out of blame, and a merge driver across it.** The whole tree is reformatted once, by the
  pinned binary alone, and the commit goes into `.git-blame-ignore-revs`. A merge driver for C++ formats base, ours
  and theirs with the pin before `git merge-file`, so an upstream edit to one of the 198 reformatted upstream files
  conflicts only where the edit does. Refused: formatting only the fork's files, which leaves two formatters in one
  tree.
- **One CMake formatter (approved).** `gersemi` keeps the CMake files in one layout as `clang-format` keeps the
  C++, run by `omw format` and checked by the gate.
- **CMake 4.4 at least, and 4.4.5 exactly where `omw` builds (taken).** `cmake_minimum_required(VERSION 4.4)`, and
  every check for an older CMake goes: three `CMAKE_VERSION` tests for 3.20 to 3.24 and `cmake/base64.cmake`'s own
  3.13. `omw` fetches CMake 4.4.5 for Linux and Windows into `deps/`, pinned by SHA-256 in `pins.py` as the release
  image's CMake is today, and builds with it on every desk, in CI and in the release image, so one CMake configures
  every build. GitHub's runners ship 3.31.6. Refused: the floor at 3.31, two years old; the runners' CMake in CI
  beside the desks' own, two versions building one tree.

## Implementation plan

Each step builds every preset on Linux, passes the gate, and passes CI on every job; the Windows laptop builds the
`full` flavour and runs `omw format --check` after steps 2, 3, 4, 7, 9 and 11; a clangd sweep of 200 files finds no
diagnostic after steps 9 and 12.

1. **GitLab out.** Done: `.gitlab-ci.yml`, `.gitlab/`, the GitLab-only `CI/` files and `docker/generic-linux` go;
   the example suite's revision is `EXAMPLE_SUITE_REVISION`; `upstream.yml` copies it at each merge and keeps the
   deletes.
2. **macOS and Android out.** Done: `files/mac/`, `CI/macos/`, `SignMacApplications.cmake`, `macospath.{cpp,hpp}`,
   every `APPLE` branch and `__APPLE__`/`Q_OS_MAC` line; `androidmain.cpp`, `androidpath.{cpp,hpp}`, gl4es's manual
   start, ICU's Android cross-build and every `ANDROID` branch. Upstream's user manual keeps its macOS and Android
   sections.
3. **CMake 4.4.** Done: the 4.4.5 pins in `pins.py` for Linux and Windows, which `omw` fetches and puts first on
   every command's PATH (`deps.cmake`), the release image's own CMake and apt's gone, `cmake_minimum_required(VERSION
   4.4)` in the tree and the presets, and every check for an older CMake gone. The steps after it count on 4.4.
4. **clang-format 23.1.5.** Done: the wheel pins in `pins.py`, which `omw format` reads on every system; CI, the
   merge workflow and the deps script off apt's clang-format 14; `.clang-format` as LLVM's style and the tree's
   changes, whose `--dump-config` under 23.1.5 equals the old file's but for `LineEnding: LF`; C++ LF in
   `.gitattributes`; `omw merge-file`, the merge driver `upstream.yml` sets, tested on a reformat merged with an edit
   made before it. Left: the reformat commit, the three CRLF sources renormalized with it, listed in
   `.git-blame-ignore-revs`.
5. **`openmw-checks`.** The `INTERFACE` target with the warnings each compiler takes, linked by every first-party
   target; the root's `add_compile_options` for warnings go. Test: a third-party target's compile command holds none
   of them, and a first-party one holds all.
6. **Dependencies in one file.** `cmake/Dependencies.cmake` with `FIND_PACKAGE_ARGS`; the presets set the try-find
   mode; the four fetch sites, the `OPENMW_USE_SYSTEM_*` switches and the find modules a config package replaces go.
7. **The ray tracer.** `components/rtxvulkan` and `apps/rtxtool` as `target_sources` lists, functions,
   current-folder paths, no `CACHE INTERNAL` (the shader folders as properties of `openmw-rtx-vulkan-shaders`); the
   core out of `components` into `components/rtx/CMakeLists.txt`, a library a layer group.
8. **`components`.** `openmw-components` and `openmw-components-qt` with `target_sources` lists by folder, the stem
   macros and the hand-made unity build gone; the crash catcher, platform I/O, table makers and content store in
   their folders' own `CMakeLists.txt`.
9. **The apps.** Each app's `CMakeLists.txt` in the same form, `opencs_units` and `add_openmw_dir` gone.
10. **Tests.** Every test binary with `target_sources` lists in its folder; `cmake/Tests.cmake` registers them.
11. **The root and the presets.** The root split into `cmake/{Options,Checks,Install,Package}.cmake`, the options
    renamed `OPENMW_*`; build and workflow presets; the sanitizer option; `omw` calls the presets.
12. **clangd.** `.clangd`'s flag list shrinks to what `$<CXX_COMPILER_ID:GNU>` adds; the sweep.
13. **CMake formatter**: `gersemi` over every CMake file, its commit in `.git-blame-ignore-revs`.
