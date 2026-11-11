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
- **A found package is a target.** Each package by its own CMake files, or by its header and archive where it
  installs none; one whose files define no target, or none that states what a static archive of it calls, gets an
  imported one in `cmake/Dependencies.cmake`, carrying its links and the definitions its headers need
  (`BT_USE_DOUBLE_PRECISION`, `MYGUI_STATIC`). Refused: `${…_LIBRARIES}` and `${…_INCLUDE_DIRS}` at each use, which
  every user had to repeat.
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
- **One prefix, and CMake's own switches before the tree's.** Targets `openmw-<name>`, the tree's cache entries
  `OPENMW_<NAME>`, of which `OPENMW_SANITIZER` alone is left: warnings as errors by
  `CMAKE_COMPILE_WARNING_AS_ERROR`, link-time optimisation by `CMAKE_INTERPROCEDURAL_OPTIMIZATION`, the tests by
  `BUILD_TESTING` ([CMake](https://cmake.org/cmake/help/latest/variable/CMAKE_COMPILE_WARNING_AS_ERROR.html)).
  Refused: a switch per program, which made 2²⁷ configurations of which four were built.
- **Third-party code in one file.** `cmake/Dependencies.cmake` finds the vcpkg set's packages and declares what the
  tree compiles itself from a pinned archive (SDL3, Recast, yaml-cpp, Google Test and Benchmark, volk, VMA,
  Crashpad), never a system's in its place: those build with the tree's flags and sanitizer, as ThreadSanitizer
  sees a lock only in code it instrumented ([ThreadSanitizer](https://clang.llvm.org/docs/ThreadSanitizer.html)).
  Recast is the pinned commit, as `NavMeshTileStore::sTileVersion` keys the navigator's tiles by it. Refused:
  `FIND_PACKAGE_ARGS`, which took a desk's own versions; an overlay port for each, a set rebuilt for a bump the tree
  already pins exactly.
- **A third party's warnings are off, not fixed**: the tree does not patch fetched or vendored code for its
  compiler's opinions, and a warning nobody acts on teaches every reader to skip them. On GCC and Clang, `-w` in
  the fetched block's and `extern/`'s flags, which wins over any `-W` a project adds after it (checked with GCC
  16 and Clang 23). On MSVC, `/w` in `extern/`, whose projects set no level; the fetched ones set their own
  (yaml-cpp and SDL3 `/W3`, Benchmark and Crashpad `/W4`), which `/w` would override with a D9025 a file, so
  MSVC turns off the one warning a project raises on its target (`/wd4267` on yaml-cpp).
- **CMake's own warnings are errors**: the presets make its dev and deprecation warnings errors
  ([cmake-presets(7)](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html)), so a new one stops the
  configure, a third party's after a bump included. Refused: failing a job on any log line that says `warning`,
  which tests named after warnings, and the content's own log, would trip.
- **A short root.** The project, its version, the standard and the folders; options in `cmake/Options.cmake`,
  install rules in `cmake/Install.cmake`, and what a folder owns in its own file. Refused: CPack, which nothing
  packaged through — the fork ships with `cmake --install`.
- **A build is of a Git clone**, which a configure without `.git` refuses: every build names its set from Git
  (`omw`), and the version names its commit. The version is configured once but for the commit, its date and
  the latest tag, which `get-version` fills in at every build, rewriting a file only where it moved. Refused:
  empty hashes for a tree without `.git`, a version that names no commit.
- **CMake's commands before the tree's helpers**: a resource is copied or configured by `configure_file`
  itself, into `CMAKE_RUNTIME_OUTPUT_DIRECTORY`, the runtime folder's one name. A function stays where it does
  work of its own (`pack_resource_file`, `rtx_compile_shader`, `openmw_rtx_layer`, `rtx_make_table`).
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
- **One compile database for the editor.** `.clangd` names `build-debug`'s, which is GCC's, and removes the four
  flags of GCC's alone that Clang reads otherwise, passing over the rest as unknown, so clangd checks what CI's Clang
  build checks ([clangd configuration](https://clangd.llvm.org/config)). Refused: a database copied or linked to the
  root, which follows the last flavour configured.
- **Four flavours, each building every program, the tests and the benchmarks**: `debug` (`RelWithDebInfo`, every
  assert, CI's and the gate's), `release` (the measured build and the archive's), `asan` and `tsan`. Refused: a
  flavour per set of programs, which left the CS and the wizard to CI alone.
- **Every flag in one file, the code generation shared with the set.** `cmake/Flags.cmake`, read through
  `CMAKE_USER_MAKE_RULES_OVERRIDE` as CMake sets each language's defaults, the same for GCC, Clang and MSVC;
  presets and folders set none ([CMake](https://cmake.org/cmake/help/latest/variable/CMAKE_USER_MAKE_RULES_OVERRIDE.html)).
  What every object is compiled with, the set's and the tree's alike (line tables, frame pointers, x86-64-v2,
  per-function sections, the stack protector), is `vcpkg/flags.cmake`, which both triplets include and name in
  `VCPKG_HASH_ADDITIONAL_FILES` so an edit rebuilds every port
  ([vcpkg](https://learn.microsoft.com/vcpkg/users/triplets#vcpkg_hash_additional_files)). Refused: the triplets
  reading `cmake/Flags.cmake` itself, which would rebuild the whole set for a link flag or a sanitizer that never
  reaches it. `omw` configures a build directory afresh when either file moves, as CMake reads a language's
  `_INIT` flags into the cache at a directory's first configure alone (`presets.CACHE_SEEDS`). A PDB on every MSVC link, its information embedded in the objects for ccache; mold by
  `CMAKE_LINKER_TYPE`, thin archives on Linux; position-independent programs everywhere, as MSVC's always are and
  the desk's GCC defaults to, where the release base's GCC links a fixed address.
- **Link-time optimisation for what ships alone.** The release workflow sets `OMW_LTO=ON` for a tagged release, which
  the release preset passes to `CMAKE_INTERPROCEDURAL_OPTIMIZATION`; a desk's release build links without it, and a
  desk that measures what ships turns it on the same way. The distributions that build with it report single-digit
  gains for a slower, hungrier link ([Fedora](https://fedoraproject.org/wiki/LTOByDefault);
  [Ubuntu](https://wiki.ubuntu.com/ToolChain/LTO)).
- **The test binaries' own files at `-Og` on GCC and Clang**: they build 30% faster for runs 10% slower
  (`research/2026-11-11-test-binaries-at-og.md`); the code they test keeps the build's flags, and MSVC's tests too,
  its `/Od` unmeasured.
- **A smoke run prints on failure alone**, its logs kept either way: the benchmarks are CTest tests running
  Google Benchmark's dry run, one iteration and no timing to warn about, and CI's integration run prints the
  game's log only when it fails. `omw release bench` is what measures.
- **The driver owns the translations' source list**: `omw translations` regenerates the `.ts` files and
  `omw lint translations` checks them from the one list, so CI's checks job builds nothing; the build only
  compiles them (`qt_add_lrelease`). Extraction outside the build is how KDE works (`Messages.sh`). Refused:
  `qt_add_translations(SOURCE_TARGETS …)`, which needs a configured build to check and scans every source of
  `openmw-components-qt`, adding strings to upstream's `.ts` files.
- **One portable install layout**, every file beside the programs and found relative to them, as the Windows folder
  and the AppImage both are. Refused: the system-install branch, for a distribution package the fork does not make.

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

## Dependencies

- **Every library from one vcpkg set, built static.** `vcpkg.json` names them; `pins.py` pins GitHub's archive of
  the registry at a release commit and the tool it names, with no `builtin-baseline`, whose version database needs
  the registry's history ([vcpkg versioning](https://learn.microsoft.com/vcpkg/users/versioning)); overlay ports in
  `vcpkg/ports` carry what the tree needs off the registry ([overlay ports](https://learn.microsoft.com/vcpkg/concepts/overlay-ports)),
  kept as their authors wrote them and outside the formatter's reach, so the registry's next version compares line
  for line. Two triplets, `x64-linux-openmw` and `x64-windows-openmw`, static and release only, Windows' runtime
  static too ([triplets](https://learn.microsoft.com/vcpkg/users/triplets)). It is what upstream does for its
  Windows set ([OpenMW/openmw-deps-build](https://github.com/OpenMW/openmw-deps-build)). Refused: the system's
  packages, a version per desk; FetchContent for everything, which builds Qt and FFmpeg in every build directory;
  Conan, a second ecosystem.
- **A set built once for its inputs and published.** `omw deps-set` builds the system's set and packs vcpkg's
  export, toolchain file included, in parts of 1 GiB, as GitHub refuses a release asset of 2 GiB
  ([About releases](https://docs.github.com/repositories/releasing-projects-on-github/about-releases)); the
  Dependency sets workflow builds both and publishes them as `deps-<digest>`, the digest of everything a set is
  built from, and `pins.py` names the parts by SHA-256. A build fetches the pinned set, or the one `omw deps-set`
  built on the desk for inputs that moved, through the toolchain file the presets read from `OMW_VCPKG_SET`.
  Refused: every desk building the set, an hour of Qt each and binaries that differ by compiler.
- **The set carries what the tree links or runs.** Where a port has a switch, it builds less: qttools without
  Designer and the programs no build runs, OpenSceneGraph without the libraries and serializers the tree does
  not register, FFmpeg without encoders and muxers. Where none exists, `omw deps-set` prunes vcpkg's export
  before packing it: the host programs' debug information (PDBs dropped, ELF programs stripped), and everything
  but the programs of the ports carried for them, shaderc, glslang and SPIRV-Tools. The desk keeps the pruned
  export, so it builds against what is published. On Linux this took the set from 831 to 339 MiB packed
  (`research/2026-11-11-dependency-set-size.md`). Kept: what Qt's package configs import and check for
  (qtbase's host programs, stripped instead), `harfbuzz-subset`, which harfbuzz's config declares, and what
  no port lets be turned off.
- **Line tables on Linux, full CodeView on Windows.** Linux's triplet compiles with `-g1`, GCC's line tables, as
  the tree's own code does, and FFmpeg is held to it by `--disable-debug`, without which its `configure`
  appends `-g` after the triplet's flags. MSVC has no line-tables-only mode, as Chromium's `minimal_symbols`
  notes ("MSVC doesn't have an equivalent option",
  [BUILD.gn](https://chromium.googlesource.com/chromium/src/+/main/build/config/compiler/BUILD.gn); `/Zd` gone
  from `cl.exe`, [D114632](https://reviews.llvm.org/D114632)), so Windows keeps vcpkg's `/Z7`. Refused: clang-cl
  with `-gline-tables-only` for Windows' set, a second compiler on which every port's build then depends; no
  debug information in Windows' archives, which names a crash frame inside a dependency after the nearest
  public symbol above it.
- **Static where the licences permit, which is everywhere the tree links.** The LGPL's libraries (Qt, FFmpeg, OpenAL
  Soft, OpenSceneGraph's OSGPL) may be linked into a GPL program whose source its users receive
  ([FSF](https://www.gnu.org/licenses/gpl-faq.html#LGPLStaticVsDynamic);
  [Qt](https://www.qt.io/licensing/open-source-lgpl-obligations); [FFmpeg](https://ffmpeg.org/legal.html)); GCC's
  runtime under its exception ([FAQ](https://www.gnu.org/licenses/gcc-exception-3.1-faq.html)), outside the
  sanitizer builds, whose runtime replaces the allocator in a shared libstdc++ alone; MSVC's by `/MT`. Every
  port's licence is installed under `licenses/vcpkg`, and a release's notes name the pins as the libraries'
  source. Refused: dynamic libraries copied beside the programs, a runtime of each library's on every system.
- **The system keeps what it opens for the programs**: glibc, the GL stack, X11, Wayland, fontconfig and ALSA, by
  empty ports where a registry port would build them (`alsa`, `fontconfig`), as a copy linked in reads its
  configuration and plugins at the build system's paths. The AppImage exclude list names the same
  ([excludelist](https://github.com/AppImageCommunity/pkg2appimage/blob/master/excludelist)). The Vulkan loader is
  the one library shipped beside the programs, the set's on Linux, as a host's may be older than the renderer is
  written against; Windows' tests take LunarG's redistributable, as a desk's driver installs one.
- **What a static program must ask for by name**: OpenSceneGraph's plugins, registered in one generated file every
  program linking the components pulls in by symbol, as upstream's static builds registered them; Qt's Wayland
  platform on Linux beside its default, and its OpenSSL backend, which reads the system's certificates itself
  ([Qt](https://doc.qt.io/qt-6/qt-import-plugins.html)).
- **A port's fix is an overlay of that port, never a triplet flag**: a triplet's contents enter every port's ABI
  hash ([binary caching](https://learn.microsoft.com/vcpkg/reference/binarycaching#abi-hash)). Inner static
  libraries become object libraries a static build carries (collada-dom, unshield); OpenThreads yields by
  `sched_yield`, as glibc 2.34 took `pthread_yield` out of the link-time API; MyGUI stays at upstream's 3.4.3.

## Systems and compilers

- **A port passes only what it reads**, so a set rebuilds without vcpkg's "variables are not used": qtbase
  without the `xrender` feature Qt 6.11 removed, `openmw-osg` without its dead atomics options and with XRandR's
  on Linux alone, and a `vulkan-loader` overlay that marks Python maybe unused. libjpeg-turbo's ARM option
  waits for the registry release that marks it. Kept: the Qt ports' own messages about system packages, which
  only a copy of Qt's biggest port of our own would remove.
- **Linux and Windows alone.** GitLab's pipeline, macOS and Android are gone, files and branches both;
  `upstream.yml` keeps each deleted path deleted at every merge.
- **AlmaLinux 8 with gcc-toolset-15 for CI's Linux legs, the set and the release**, the stock `almalinux:8`
  container and `env/install_almalinux_deps.sh`: one environment builds, tests and ships, and the AppImage runs on
  glibc 2.28 — Ubuntu 20.04 and later, Debian 11, SteamOS — as `omw archive` checks of every program. It is
  manylinux_2_28's way, an old base with Red Hat's newer toolset ([pypa/manylinux](https://github.com/pypa/manylinux);
  [Red Hat](https://access.redhat.com/errata/RHBA-2025:21071)). A set built there serves newer desks unchanged: an
  older glibc's objects link and run against a newer one. Refused: Valve's sniper SDK, whose compiler is older than
  the floor; Ubuntu 26.04, a glibc 2.43 floor; an image of the fork's own, a registry and a tag to keep. Red Hat
  supports a gcc-toolset about two years, so the base moves to the next one in time. The tsan leg alone runs on the
  runner itself, with Ubuntu's GCC, against the same set: ThreadSanitizer turns address randomisation off through
  `personality`, which Docker's default seccomp profile refuses.
- **One AlmaLinux 8 image, by digest, and one GCC on it.** `env/release-base.Dockerfile`'s `FROM` is the one place
  the digest is written; every workflow's `container:` names the same, which `omw lint workflows` holds, and the
  set's digest reads the Dockerfile, so a new image names a new set. The env script builds its tools (autoconf,
  zsync, `wayland-scanner`) with gcc-toolset-15 as well, quietly, though none ships. Refused: the bare tag,
  which moves under CI and the set; AlmaLinux's own GCC 8.5, there only as a package's dependency.
- **Fresh compilers, and none older.** GCC 15, gcc-toolset-15's in CI and a desk's own; Clang 23, the editor's
  clangd's major, from kernel.org's build of LLVM's release in CI, made on Debian 10 to run on glibc 2.28, where
  LLVM's own needs a newer one ([kernel.org](https://www.kernel.org/pub/tools/llvm/)); MSVC from Visual Studio 2026.
  `cmake/Checks.cmake` stops an older one, rather than a build failing inside a bug a workaround was dropped for.
