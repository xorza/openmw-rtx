# The build

How the tree is configured, built, formatted and shipped, as decisions. The CMake files, `CMakePresets.json` and
`tools/omw` hold how; `.notes/UPSTREAM.md` holds what each decision changed of upstream's.

## CMake

- **Targets own every setting** through `PRIVATE`, `PUBLIC` and `INTERFACE`; no directory-scope command anywhere.
  Refused: directory scope, whose reach followed the order folders were added in
  ([Effective Modern CMake](https://gist.github.com/mbinna/c61dbb39bca0e4fb7d1f73b0d66a4fd1/)).
- **Two interface targets for what every target compiles with**: `openmw-system` (header trims, MSVC's options) on
  every target under `components/`, `apps/` and `extern/`, and `openmw-checks` (the warnings) on the tree's own;
  `openmw_link_tree` links both. Fetched projects take neither.
- **A found package is a target**, by its own CMake files or an imported target in `cmake/Dependencies.cmake` that
  carries its links and definitions. Refused: `${…_LIBRARIES}` at each use.
- **Sources by name.** Refused: globs, `CONFIGURE_DEPENDS` included
  ([CMake Discourse](https://discourse.cmake.org/t/is-glob-still-considered-harmful-with-configure-depends/808)), and a
  unity build, where a file sees another's includes. `omw`'s listing check holds every source to a list.
- **A library a layer for the ray tracer** (`openmw-rtx-<layer>`, in `architecture.md`'s order), and
  `openmw-components` whole, as upstream's folders include each other in one cycle. `RtxSourceTreeTest` holds the
  order, which lld and MSVC do not.
- **CMake's own names**: `WIN32`, `LINUX` and `MSVC` where a file list differs, a generator expression where a flag
  does, never `CMAKE_BUILD_TYPE` in an `if`; paths from `PROJECT_SOURCE_DIR` and `CMAKE_CURRENT_*_DIR`. What the
  driver reads of a build's folders is `omw-folders.json`, written at configure. Refused: `CACHE INTERNAL`.
- **CMake's switches before the tree's**: `CMAKE_COMPILE_WARNING_AS_ERROR`, `CMAKE_INTERPROCEDURAL_OPTIMIZATION`,
  `BUILD_TESTING`; `OPENMW_SANITIZER` is the one cache entry of the tree's. Refused: a switch per program.
- **Third-party code in one file**, `cmake/Dependencies.cmake`: the vcpkg set's packages, and what the tree compiles
  from a pinned archive (SDL3, Recast, yaml-cpp, Google Test and Benchmark, volk, VMA, Crashpad) with the tree's flags
  and sanitizer, as ThreadSanitizer sees a lock only in code it instrumented. Recast is pinned, as
  `NavMeshTileStore::sTileVersion` keys tiles by it. Refused: `FIND_PACKAGE_ARGS`, which took a desk's own versions.
- **A third party's warnings are off, not fixed**: `-w` on Clang for `extern/` and the fetched projects, `/w` on
  MSVC for `extern/`, and the one warning a fetched project raises turned off on its target (`/wd4267` on
  yaml-cpp).
- **CMake's own warnings are errors**, by the presets. Refused: failing a job on any log line saying `warning`.
- **A short root**: options in `cmake/Options.cmake`, install rules in `cmake/Install.cmake`, each folder's in its
  own file. Refused: CPack; the fork ships with `cmake --install`.
- **A build is of a Git clone**: the set is named from Git and the version names its commit, which `get-version`
  rewrites only when it moves.
- **CMake's commands before the tree's helpers**; a function stays where several callers share its work
  (`openmw_rtx_shader`, `openmw_rtx_table`, …), and modules are included by path.
- **A sanitizer over the whole build** (`OPENMW_SANITIZER=address|thread`), with warnings as errors under it too.
  Refused: off under a sanitizer, GCC's advice for its flow warnings
  ([GCC](https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html)); Clang warns before it instruments.
- **The standard library's assertions in every flavour but release**: libstdc++'s `_GLIBCXX_ASSERTIONS` and the MSVC
  STL's `_MSVC_STL_HARDENING`, which change no layout, so the set built without them links beside them. Refused:
  `_GLIBCXX_DEBUG` and `_ITERATOR_DEBUG_LEVEL`, which do; libc++, a set and a runtime of its own.
- **CMake 4.4.5, the driver's own**, fetched into `out/deps/` for every desk, CI and the release image.

## Presets and the driver

- **A preset for every step**: `cmake --workflow --preset debug-linux` builds and tests a clone without `omw`, which
  adds only what CMake has no step for (formatting, the listing check, the harness's verbs).
- **Everything the driver writes is under `out/`**: a build folder a flavour, the release container's, the pinned
  tools and the dependency set (`deps`), the archives (`dist`), as Visual Studio's CMake presets template writes
  `out/build/<preset>` and Cargo its `target/`. Refused: the system's temporary folder, which a reboot empties of an
  8 GB dependency set and the symbols `omw crash` reads.
- **One compile database for the editor**: `.clangd` names `out/debug`'s.
- **Four flavours, each building every program, the tests and the benchmarks**: `debug` (every assert, CI's and the
  gate's), `release` (measured, and the archive's), `asan`, `tsan`.
- **Every flag in one file**, `cmake/Flags.cmake`, through `CMAKE_USER_MAKE_RULES_OVERRIDE`; the code generation the
  set shares is `vcpkg/flags.cmake` and Linux's `vcpkg/linux.cmake`, which the triplets hash, so an edit rebuilds the
  ports it reaches. `omw` configures a folder afresh when either moves, as CMake reads `_INIT` flags once. On MSVC,
  `/Z7` in the objects for ccache, `/Zi` under link-time code generation, whose archives would pass the COFF archive's
  4 GB (LNK1248). lld and LLVM's thin archives on Linux.
- **The game's code as objects** (`openmw-lib`, an object library): under MSVC's link-time code generation its archive
  came to 3.6 GB of 4.
- **As many links at once as the memory holds** (`cmake/Links.cmake`): every release program linking at once swapped
  a 28 GB box.
- **What the link drops and folds**: per-function sections and `--gc-sections` (`/OPT:REF`); identical code folded
  in release alone (`--icf=safe`, `/OPT:ICF`). `-fno-math-errno` for the tree. Refused: `-ffast-math` and `/fp:fast`,
  against the float rules; `-fvisibility=hidden`, `-fno-plt`, `-gsplit-dwarf`; profile-guided optimisation, which
  needs a GPU CI lacks.
- **No precompiled headers**: 14 to 19% off a clean build, 5 s onto a cached rebuild, and an `#include` a file forgot
  compiling anyway (`research/2026-11-11-precompiled-headers.md`).
- **Link-time optimisation for what ships alone** (`OMW_LTO=ON` on a tagged release): ThinLTO on Linux. Refused: full
  LTO, 3% fewer instructions in as many cycles for a link of 200 s and 7.1 GB against 33 s and 3.8 GB
  (`research/2026-11-12-full-lto-against-thinlto.md`).
- **The test binaries' own files at `-Og`** (`-O1` on Clang): 30% faster to build
  (`research/2026-11-11-test-binaries-at-og.md`).
- **A smoke run prints on failure alone**: the benchmarks are CTest dry runs, and CI's integration run prints the
  game's log only when it fails. `omw release bench` measures.
- **The driver owns the translations' source list**: `omw translations` regenerates the `.ts` files and
  `omw lint translations` checks them, as KDE's `Messages.sh` does; the build only compiles them. Refused:
  `qt_add_translations(SOURCE_TARGETS …)`, which needs a configured build and adds strings to upstream's files.

## Archives

- **An archive builds what it installs and nothing else**: `omw archive` builds `openmw-installed`, so a release
  links no test, benchmark, harness or editor.
- **The game and its tools in two archives**, CMake's install components: the game, the launcher, the wizard and the
  two programs they start as an AppImage and a Windows folder; the console tools as a `.tar.gz`, which keeps an
  executable runnable on every extractor, and a Windows folder, with the configuration and file system they read.
  **OpenMW-CS is built and not shipped**: it draws through the rasterizer, and its Qt needs libraries only the
  AppImage carries. Refused: one archive, whose AppImage reaches no program but the launcher.
- **Named `OpenMW-RTX-<version>`**, the tag past its `rtx-v`: `OpenMW-RTX-0.5.1-linux-x86_64.AppImage`,
  `OpenMW-RTX-0.5.1-tools-windows-x64.zip`. Refused: a bare `OpenMW-`, which reads as upstream's.
- **Linux's archives built in the release base, their symbols outside it**: `dump_syms` needs glibc 2.34, so it runs
  on the container's build from the host, over the installed programs alone.
- **An archive carries what a program reads, by name** (`cmake/Install.cmake`): the programs, the licences, the
  configuration and the resources read at run time, and on Linux the menu entry and icon the AppImage needs.
  Nothing for developers, the rasterizer or the editor. Refused: the resources folder whole minus what is known
  unused, which ships whatever upstream adds next; the AppStream metadata, which an AppImage does not need.
- **One portable layout**, every file beside the programs. Refused: the system-install branch.

## Formatting

- **clang-format 23.1.5, exactly**, from its PyPI wheel, as its layout moves between versions
  ([cfe-dev](https://lists.llvm.org/pipermail/cfe-dev/2017-June/054351.html)); with upstream's `.clang-format`, so a
  merge meets no layout of the fork's.
- **C++ and GLSL are LF on every clone**, so Windows formats byte for byte as Linux.
- **gersemi 0.29.2, exactly, for the CMake files**, its wheel per CPython and PyYAML beside it, nothing installed.
- **A merge across a reformat**: `omw merge-file` lays out base, ours and theirs before merging, so an upstream edit
  conflicts only where it does.

## Dependencies

- **Every library from one vcpkg set, built static**: `vcpkg.json`, the registry and tool `pins.py` pins, overlay
  ports in `vcpkg/ports` kept as their authors wrote them, two triplets, static and release only, as upstream's
  Windows set is ([OpenMW/openmw-deps-build](https://github.com/OpenMW/openmw-deps-build)). Refused: the system's
  packages, a version per desk; FetchContent for Qt and FFmpeg; Conan.
- **A set built once for its inputs and published** as `deps-<digest>` in parts of 1 GiB, under GitHub's 2 GiB limit
  per asset; a build fetches the pinned set, or the one `omw deps-set` built for inputs that moved.
- **The set carries what the tree links or runs**: ports with the switches to build less, and the export pruned of
  host programs' debug information and of everything but the programs of shaderc, glslang and SPIRV-Tools: 831 to
  339 MiB packed on Linux (`research/2026-11-11-dependency-set-size.md`).
- **libxkbcommon is the host's on Linux**, as it reads the host's keymaps and Compose files: the AppImage leaves it and
  its X11 half out (`LINUX_HOST_LIBRARIES`).
- **Line tables on Linux (`-g1`), full CodeView on Windows**, where MSVC has none smaller
  ([Chromium](https://chromium.googlesource.com/chromium/src/+/main/build/config/compiler/BUILD.gn)).
- **Static where the licences permit, which is everywhere the tree links**: the LGPL's libraries into a GPL program
  whose source its users receive ([FSF](https://www.gnu.org/licenses/gpl-faq.html#LGPLStaticVsDynamic)), GCC's runtime
  under its exception, MSVC's by `/MT`; every port's licence under `licenses/vcpkg`.
- **The system keeps what it opens for the programs**: glibc, the GL stack, X11, Wayland, fontconfig and ALSA, as the
  AppImage exclude list names them. The Vulkan loader is the one library shipped, the set's 1.4 on Linux.
- **A static program asks by name** for OpenSceneGraph's plugins, Qt's Wayland platform and its OpenSSL backend.
- **A port's fix is an overlay of that port, never a triplet flag**, which every port's ABI hash reads.

## Systems and compilers

- **Linux and Windows alone**: GitLab's pipeline, macOS and Android are gone, and `upstream.yml` keeps each deleted
  path deleted at every merge.
- **AlmaLinux 8 with gcc-toolset-15 for CI's Linux legs, the set and the release**, so the AppImage runs on glibc 2.28,
  as manylinux_2_28 does ([pypa/manylinux](https://github.com/pypa/manylinux)); one image by digest
  (`env/release-base.Dockerfile`), which `omw lint workflows` holds every workflow to. The tsan leg runs on the runner
  itself, as Docker's seccomp refuses the `personality` ThreadSanitizer calls.
- **CI's caches** are saved on master alone and restored on every branch: the compiler cache, the fetched archives,
  the driver's tools by their pinned folders, and vcpkg's binary cache by run.
- **One compiler and linker on Linux: the pinned Clang 23 and its lld**, kernel.org's build for glibc 2.28, over GCC
  15's libstdc++ (`--gcc-install-dir`, as libstdc++'s C++20 half keeps no ABI across majors). No contraction
  (`-ffp-contract=off`), so arithmetic rounds as written. Refused: GCC, whose link-time optimisation broke the Qt
  programs against the static Qt ([GCC PR 105933](https://gcc.gnu.org/bugzilla/show_bug.cgi?id=105933)); mold, which
  reads no bitcode in that build; libc++, which brings no start files or compiler runtime. MSVC from Visual Studio
  2026 on Windows. `cmake/Checks.cmake` refuses any other compiler and an older one.
