# The build

How the tree is configured, built, formatted and shipped, as decisions. The CMake files, `CMakePresets.json` and
`tools/omw` hold how; `.notes/UPSTREAM.md` holds what each decision changed of upstream's.

## CMake

- **Targets own every setting.** Each target states what it and its users need through `PRIVATE`, `PUBLIC` and
  `INTERFACE`; no `include_directories`, `add_compile_options` or `add_compile_definitions` anywhere. Refused:
  directory scope, whose reach followed the order folders were added in
  ([Pfeifer, "Effective CMake"](https://www.slideshare.net/DanielPfeifer1/effective-cmake);
  [Effective Modern CMake](https://gist.github.com/mbinna/c61dbb39bca0e4fb7d1f73b0d66a4fd1/)).
- **Two interface targets for what every target compiles with.** `openmw-system`, the system's header trims and
  MSVC's options, reaches every target under `components/`, `apps/` and `extern/`; `openmw-checks`, the warnings,
  the tree's own alone; `openmw_link_tree` links both after every folder is added. Fetched projects take neither
  and build as their own CI does.
- **A found package is a target.** A find module that defines none gets an imported one in
  `cmake/Dependencies.cmake`, carrying the definitions its headers need (`BT_USE_DOUBLE_PRECISION`, `NO_LUAJIT`).
  Refused: `${…_LIBRARIES}` and `${…_INCLUDE_DIRS}` at each use, which every user had to repeat.
- **Sources by name, headers as a file set.** `target_sources` in the folder's own `CMakeLists.txt`, every file
  named. Refused: globs, `CONFIGURE_DEPENDS` included, which CMake discourages
  ([CMake Discourse](https://discourse.cmake.org/t/is-glob-still-considered-harmful-with-configure-depends/808));
  stem macros, which dropped a name with no file without a word; a unity build, hand-made or CMake's, where a file
  sees another's includes and unnamed namespace. `omw`'s listing check holds the rule.
- **A library a layer for the ray tracer, one for upstream's components.** The core is `openmw-rtx-<layer>`, each
  linking the layer below, in `architecture.md`'s order. Upstream's component folders include each other in one
  cycle, so `openmw-components` stays whole. The link graph does not hold the order on every linker — mold and
  MSVC's resolve an archive in any order — so `RtxSourceTreeTest` holds it too.
- **CMake's own names for the system, the compiler and the configuration.** `if (WIN32)`, `if (LINUX)` and
  `if (MSVC)` where a list of files differs, a generator expression where a flag does; never `CMAKE_BUILD_TYPE` in
  an `if`, which a multi-config generator leaves empty. Refused: variables of the tree's own (`OPENMW_GCC`), a
  second spelling no generator expression reads.
- **Paths from the project.** `PROJECT_SOURCE_DIR` and `CMAKE_CURRENT_*_DIR`, never `CMAKE_SOURCE_DIR`; what the
  driver reads of a build's folders, `omw-folders.json`, written at configure. Refused: `CACHE INTERNAL`, a second
  route between folders that outlives the variable it copied.
- **One prefix.** Targets `openmw-<name>`, options and cache entries `OPENMW_<NAME>`. Upstream's `BUILD_*` and
  `USE_*` went with GitLab's pipeline, their only outside reader.
- **Third-party code in one file.** `cmake/Dependencies.cmake` declares what is fetched, with
  `FIND_PACKAGE_ARGS` so a system's package is taken where installed, and finds the rest; Windows sets
  `FETCHCONTENT_TRY_FIND_PACKAGE_MODE` to `NEVER`, as upstream's prebuilt set has none
  ([FetchContent](https://cmake.org/cmake/help/v3.24/module/FetchContent.html)). Recast is always the pinned
  commit: `NavMeshTileStore::sTileVersion` keys the navigator's tiles by it.
- **A short root.** The project, its version, the standard and the folders; options in `cmake/Options.cmake`,
  install rules in `cmake/Install.cmake`, and what a folder owns in its own file. Refused: CPack, which nothing
  packaged through — the fork ships with `cmake --install`.
- **A sanitizer over the whole build.** `OPENMW_SANITIZER=address|thread` sets the compiler's flags for every
  target, the fetched ones included. Refused: on `openmw-checks` alone, since ThreadSanitizer sees a lock only in
  code it instrumented.
- **CMake 4.4 at least, 4.4.5 exactly where `omw` builds.** `omw` fetches the pinned release into `deps/` on every
  desk, in CI and in the release image, so one CMake configures every build. Refused: the runners' 3.31 beside the
  desks' own.

## Presets and the driver

- **A preset for every step.** Each visible configure preset has a build preset and a workflow preset of its name,
  so `cmake --workflow --preset debug-linux` configures, builds and tests a clone without `omw`
  ([cmake-presets(7)](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html)). `omw` configures, builds
  and tests through the same presets and adds what CMake has no step for: formatting, the listing check, the
  harness's verbs.
- **One compile database for the editor.** `.clangd` names `build-full`'s, which is GCC's, and removes the four
  flags of GCC's alone that Clang reads otherwise, passing over the rest as unknown, so clangd checks what CI's Clang
  build checks ([clangd configuration](https://clangd.llvm.org/config)). Refused: a database copied or linked to the
  root, which follows the last flavour configured.

## Formatting

- **clang-format 23.1.5, exactly, everywhere.** Its layout moves between versions and LLVM does not hold it
  stable, so the version is pinned for every desk and CI
  ([cfe-dev](https://lists.llvm.org/pipermail/cfe-dev/2017-June/054351.html)); `omw` takes the binary out of the
  PyPI wheel ([clang-format-wheel](https://github.com/ssciwr/clang-format-wheel)). Refused: the path's, which
  differs by desk; LLVM's packages, a gigabyte for one program.
- **Upstream's `.clang-format`.** A merge meets no layout of the fork's own. 23 lays a braced list ending in a comma
  out one element a line, so a table written in rows ends without one. Tried and reverted: an LLVM style, which laid
  out nearly every source anew.
- **C++ and GLSL text is LF on every clone**, in `.gitattributes` and `.clang-format`, so Windows formats byte for
  byte as Linux.
- **gersemi 0.29.2, exactly, for the CMake files**, the C++'s width and indent, one item a line where a call wraps,
  run and checked by `omw format`. Its layout is Rust built into a wheel for one CPython, so `omw` pins a wheel for
  each interpreter it runs under and PyYAML's pure-Python source beside it; nothing is installed. It took over the
  tab check upstream's lint made.
- **A merge across a reformat.** `omw merge-file`, git's merge driver for C++, lays out base, ours and theirs with
  the pin before merging, so an upstream edit conflicts only where it does. Refused: formatting the fork's files
  alone, two layouts in one tree.

## Systems and compilers

- **Linux and Windows alone.** GitLab's pipeline, macOS and Android are gone, files and branches both;
  `upstream.yml` keeps each deleted path deleted at every merge.
- **Ubuntu 26.04 for every Linux build**, CI's runners and the release image alike, installed by
  `CI/install_ubuntu_deps.sh`: one system compiles, tests and ships. The cost: the AppImage needs glibc 2.43.
  Refused: the image published for a job's `container:`, a registry and a tag to keep.
- **Fresh compilers, and none older.** GCC 15.2, Ubuntu 26.04's; Clang 23 from apt.llvm.org in CI, the editor's
  clangd's major; MSVC from Visual Studio 2026. `cmake/Checks.cmake` stops an older one, rather than a build failing
  inside a bug a workaround was dropped for.
