# Proposal: one build, pinned and static

The goal, as asked: a build with less to configure and fewer branches, the same compile and link options in every
build, every dependency at a version the tree pins and not the system's, linked statically where its licence
permits, and newer versions of all of it. Investigated 2026-11-11 against `master` at `97b8298598`.

## Where the build stands

- **27 options and 11 more cache entries** (`cmake/Options.cmake`, `cmake/Tests.cmake`,
  `apps/components_tests`), plus `OPENMW_CXX_FLAGS` and `OPENMW_MSVC_WERROR`, which only presets set. Of the
  27, 16 choose programs and suites (`OPENMW_BUILD_*`), 2 are never turned off by any preset
  (`OPENMW_CRASHPAD`, `OPENMW_RTX_DLSS`) and so keep dead branches (`crashunsupported.cpp`, the DLSS stubs), and 1
  has no user (`OPENMW_COVERAGE`).
- **Six flavours on two systems**, 10 visible configure presets over 11 hidden parts. `full` differs from
  `debug` only by which programs it builds, and `package` from `release` only by programs and the install
  layout.
- **Compile and link options live in nine places**: `-O2 -g1` and `-O2 -Z7` in the two system presets, frame
  pointers in `release-linux`, `-Z7 -Oy-` and `-DEBUG` in `release-windows`, `-g1` in `package-linux`, `-Z7`
  and `-DEBUG` in `package-windows`, link-time optimisation in `full-windows` alone, `-Og` for the tests in
  `debug-linux` alone, mold and thin archives in `linux`, warnings as errors through two variables of the tree's
  own. A Linux release and a Windows release are not built alike, and neither is built like its package.
- **Branches**: 190 `if`/`elseif` across the tree's CMake files. 35 are `if(WIN32)`, 16 of them adding the same
  `other-apps.manifest` to one program each, 12 for that alone; 17 are `if(MSVC AND OPENMW_MSVC_PRECOMPILED_HEADERS)`
  around precompiled headers the Windows preset turns off; 28 test an `OPENMW_BUILD_*`; the multi-config machinery
  (`get_generator_is_multi_config` and its users) serves generators no preset uses.
- **Four routes for third-party code**: the distribution's packages and OpenMW's PPA on Linux
  (`CI/install_ubuntu_deps.sh`), upstream's prebuilt vcpkg set on Windows (`VCPKG_TAG=2026-11-05`), FetchContent
  with a system fallback for eight projects (`cmake/Dependencies.cmake`), and vendored copies in `extern/`. So a
  desk on a rolling distribution, CI on Ubuntu 26.04 and Windows each build against different versions: Qt is
  6.6.3 on Windows and 6.10 on Ubuntu, and the launcher, the wizard and the CS are not built by Windows' debug
  flavour at all, which has no Qt.
- **Everything found is linked dynamically**, and the AppImage carries the libraries `linuxdeploy` collects. The
  release floor is the build system's glibc, 2.43.

## Established practice

- **Pinned binary dependencies for a CMake project: vcpkg in manifest mode.** A `vcpkg.json` names the
  dependencies, `builtin-baseline` fixes every port's version to one commit of the registry, `overrides` pin a
  port off that baseline, and overlay ports carry a fork ([vcpkg versioning](https://learn.microsoft.com/vcpkg/users/versioning);
  [overlay ports](https://learn.microsoft.com/vcpkg/concepts/overlay-ports)). A triplet sets the linkage and the
  configurations built: `VCPKG_LIBRARY_LINKAGE static`, `VCPKG_CRT_LINKAGE static`, `VCPKG_BUILD_TYPE release`
  ([triplets](https://learn.microsoft.com/vcpkg/users/triplets)). Built binaries are cached and shared
  ([binary caching](https://learn.microsoft.com/vcpkg/users/binarycaching)). Upstream builds its Windows, Linux
  and macOS sets exactly so, with overlay ports for its OpenSceneGraph fork and unshield
  ([OpenMW/openmw-deps-build](https://github.com/OpenMW/openmw-deps-build)).
- **Static linking and the licences.** OpenMW is GPL-3.0. LGPL libraries (Qt, FFmpeg, OpenAL Soft, OpenSceneGraph's
  OSGPL) may be linked statically into a GPL program, whose source the user already receives
  ([FSF, GPL FAQ](https://www.gnu.org/licenses/gpl-faq.html#LGPLStaticVsDynamic);
  [Qt, LGPL obligations](https://www.qt.io/licensing/open-source-lgpl-obligations);
  [FFmpeg, legal](https://ffmpeg.org/legal.html)). MIT, BSD, zlib, Boost, Apache-2.0 and Unicode's licence permit
  it with their notices shipped. GCC's runtime may be linked statically under its exception
  ([GCC Runtime Library Exception FAQ](https://www.gnu.org/licenses/gcc-exception-3.1-faq.html)), and MSVC's CRT
  statically (`/MT`). What cannot be static: the Vulkan loader, which the renderer loads at start by design;
  NVIDIA's DLSS library, which NVIDIA ships and the game downloads; the GPU drivers; glibc; and what SDL and Qt open
  at run time to reach the display and the sound server (X11, Wayland, xkbcommon, ALSA, PulseAudio, D-Bus).
- **Flags in one place, per compiler and configuration.** `CMAKE_USER_MAKE_RULES_OVERRIDE` names a file CMake reads
  when it initialises each language, where `CMAKE_<LANG>_FLAGS_<CONFIG>_INIT` are set once for every build
  ([CMake](https://cmake.org/cmake/help/latest/variable/CMAKE_USER_MAKE_RULES_OVERRIDE.html)). The linker by
  `CMAKE_LINKER_TYPE` ([CMake 3.29](https://cmake.org/cmake/help/latest/variable/CMAKE_LINKER_TYPE.html)), warnings
  as errors by `CMAKE_COMPILE_WARNING_AS_ERROR`
  ([CMake 3.24](https://cmake.org/cmake/help/latest/variable/CMAKE_COMPILE_WARNING_AS_ERROR.html)), the test
  binaries by CTest's `BUILD_TESTING` ([CTest](https://cmake.org/cmake/help/latest/module/CTest.html)): CMake's
  own switches, where the tree now has its own.

## Decisions

Each with its options, my pick first.

1. **How the dependencies are pinned.**
   - **vcpkg manifest mode, one `vcpkg.json` for Linux and Windows (pick).** One baseline pins every version on both
     systems; overlay ports carry what the tree needs off the registry (OpenMW's OpenSceneGraph fork and its Recast
     fork, Sentry's Crashpad with its CMake build, unshield, Google Benchmark 1.10, whose port is at 1.9.5). It is
     what upstream does. Cost: vcpkg itself, fetched and pinned by `omw` as CMake is; a dependency build of an hour
     or two per triplet when the baseline moves.
   - FetchContent for everything: no new tool, but OpenSceneGraph, Qt and FFmpeg have no build FetchContent can
     drive well (FFmpeg is no CMake project), and every build directory would build them again.
   - Conan 2 with a lockfile: equivalent pinning, a second ecosystem upstream does not use.
   - Keep the system's: what the tree does now; refused by the goal.
2. **Who builds them.**
   - **CI builds each triplet's set when `vcpkg.json` or an overlay changes and publishes it as a release asset,
     pinned by SHA-256 in `pins.py`; `omw` fetches it into `deps/` (pick).** A desk and CI then build against the
     same bytes, and a fresh clone needs no hour of dependency builds: the route Windows already takes, from the
     fork's own set rather than upstream's.
   - Every desk builds through vcpkg with a binary cache: the same versions, but the first build of a desk takes
     the hours, and two desks' compilers may produce different binaries.
3. **Linkage.** **Static for every library the licences permit (pick)**, by custom triplets `x64-linux-openmw` and
   `x64-windows-openmw` (`static` libraries, release only, since the tree's flavours all use the release
   runtime). On Windows the CRT static too (`/MT`), which ends shipping MSVC's runtime DLLs and the fault of an
   older system-wide runtime `cmake/Install.cmake` works around. On Linux `-static-libstdc++ -static-libgcc`.
   Refused: dynamic with every library copied beside the programs, which is today's AppImage and Windows folder.
   OpenSceneGraph's plugins become static as well, which needs their registration (`USE_OSGPLUGIN`) back in one
   file: upstream's static support went with macOS and Android in this tree.
4. **Qt.** **Static Qt from the same vcpkg set (pick)**: one version on both systems, every flavour builds the
   launcher, the wizard and the CS, and `linuxdeploy-plugin-qt`, aqt and `QT_VER` go. Cost: Qt is the longest port
   to build, and its static platform plugins (xcb, wayland, windows) are imported through Qt's CMake. Refused: aqt's
   prebuilt dynamic Qt on both, pinned but not static.
5. **Flavours.** **Four: `debug`, `release`, `asan`, `tsan`, each building every program, the tests and the
   benchmarks (pick).** `full` folds into `debug` and `package` into `release`: with Qt in the set, nothing is
   left that only they build. The release archive is installed from `release`. Cost: `release` builds the tools
   and tests too, a few minutes more. `BUILD_TESTING` stays as CMake's switch for a build that wants none.
6. **Options.** **Kept: `OPENMW_SANITIZER` and CMake's own (`CMAKE_BUILD_TYPE`, `CMAKE_COMPILE_WARNING_AS_ERROR`,
   `BUILD_TESTING`). Removed (pick):** the 16 `OPENMW_BUILD_*`; `OPENMW_CRASHPAD` and `OPENMW_RTX_DLSS` with their
   unused fallbacks; `OPENMW_LUAJIT` (LuaJIT always) and `OPENMW_SYSTEM_TINYXML` (the vendored one always);
   `OPENMW_MSVC_PRECOMPILED_HEADERS` and every precompiled header block, since ccache runs on both systems;
   `OPENMW_LTO`, for CMake's own switch (decision 11); `OPENMW_COVERAGE`, which nothing runs; `OPENMW_DEBUG_CONSOLE`,
   fixed to its default; `OPENMW_DEPLOY_*` and `OPENMW_LOCAL_INSTALL` with the system-install layout and its five
   path entries (decision 8); `OPENMW_TEST_COMPILE_OPTIONS`, decided below; `OPENMW_CXX_FLAGS` and
   `OPENMW_MSVC_WERROR` for `CMAKE_COMPILE_WARNING_AS_ERROR`. From 2²⁷ combinations to the 4 flavours.
7. **Flags.** **One file, `cmake/Flags.cmake` through `CMAKE_USER_MAKE_RULES_OVERRIDE`, sets every configuration's
   flags for GCC, Clang and MSVC alike; presets set none (pick).** `RelWithDebInfo` (debug, asan, tsan):
   `-O2 -g1`, asserts on; MSVC `/O2 /Z7`. `Release`: `-O2 -g1 -DNDEBUG`; MSVC `/O2 /Z7 /DNDEBUG`. Frame pointers
   and line tables in every configuration on both systems (`-fno-omit-frame-pointer
   -mno-omit-leaf-frame-pointer`, `/Oy-`), so every build can be profiled and every crash symbolised; `-DEBUG`
   on every MSVC link. mold through `CMAKE_LINKER_TYPE`, thin archives in the same file. Link-time optimisation
   (decision 11) and the tests' `-Og` (decision 12) are set here too.
8. **Install layout.** **One portable layout on both systems, every file beside the programs and found relative to
   them (pick)**, which the Windows folder and the AppImage both are already. The Linux system-install branch goes:
   `GLOBAL_DATA_PATH`, `GLOBAL_CONFIG_PATH`, the suffixes, `ICONDIR`, the RPATH arithmetic. Upstream's
   `Files::LinuxPath` keeps reading `GLOBAL_*` macros, given fixed values. Refused: keeping both for a distribution
   package the fork does not make.
9. **Windows manifest.** **One manifest for every program (pick)**, `other-apps.manifest` with the game's
   `asInvoker` and description folded in, added to every executable by `openmw_link_tree`, where `openmw-system` is
   added: 12 `if(WIN32)` blocks go, and the game gains per-monitor DPI awareness, which the others have and it
   lacks today. The `.rc` icons stay per program.
10. **Multi-config machinery.** **Removed (pick)**: every preset is single-config Ninja, so
    `get_generator_is_multi_config`, the `$<CONFIG>` runtime folder and the per-configuration resource loops go.

## Dependency versions

Current, newest, and what the plan takes. Newest checked on 2026-11-11.

| Dependency | Now | Newest | Plan |
|---|---|---|---|
| OpenSceneGraph | OpenMW's PPA (Linux), upstream's set (Windows) | OpenMW fork `e568aace` (upstream's set of 2026-11-05) | that commit, overlay port |
| Qt | 6.10 (Ubuntu), 6.6.3 (Windows) | 6.12.0 tagged; vcpkg 6.11.3 | vcpkg's 6.11.3, static |
| MyGUI | 3.4.3 floor, system | 3.5.4 (vcpkg) | 3.5.4: a minor bump with API removals, built first in its own step |
| Bullet | 2.87 floor, system, double precision | 3.25 (vcpkg, `double-precision`) | 3.25 |
| Boost | 1.83 floor, system | 1.92.0 (vcpkg) | 1.92.0 |
| FFmpeg | 4.0 floor, system | 9.0.2 (vcpkg) | 9.0.2, LGPL configuration; `osg-ffmpeg-videoplayer` checked against its removed APIs |
| ICU | system | 79.1 | 79.1 |
| LuaJIT | system | rolling, vcpkg `2026-10-30` | that |
| OpenAL Soft | system | 1.25.2 | 1.25.2 |
| LZ4 / zlib | system | 1.11.0 / 1.3.2 (vcpkg) | those |
| unshield | system (Linux) | upstream's overlay | upstream's overlay |
| SDL3 | 3.4.16, fetched | 3.4.20 | 3.4.20 |
| yaml-cpp | 0.8.0 + 1 commit, fetched | 0.9.0 | 0.9.0 |
| Google Test | 1.17.0 | 1.18.0 | 1.18.0 |
| Google Benchmark | 1.10.0 | 1.10.0 (vcpkg 1.9.5) | 1.10.0, overlay |
| Recast | OpenMW fork `03259f32` (2026-01-29) | the same | the same, overlay (the tile version keys on it) |
| Crashpad | Sentry `000e3ad7` (2026-09-21) | Sentry `702c3d5a` (2026-11-09) | the newest, overlay |
| volk / Vulkan SDK | 1.4.357 | 1.4.363 (SDK 1.4.363.1) | 1.4.363 |
| Vulkan Memory Allocator | 3.4.0 | 3.5.0 | 3.5.0 |
| FidelityFX (vendored) | FSR 3.1.4, SDK 1.1.4 | SDK 2.4.0 | read 2.4.0's FSR 3.1 for a newer 3.1.x; FSR 4 is Direct3D 12 and RDNA 4 only |
| sol (vendored) | 3.5 (a development snapshot) | 3.3.0 is the last release | unchanged |
| DLSS | 310.9.16 | 310.9.16 | unchanged |
| CMake | 4.4.5 | 4.4.5 | unchanged |
| clang-format / gersemi | 23.1.5 / 0.29.2 | the same | unchanged |
| ruff / mypy / zizmor | 0.16.9 / 2.3.1 / 1.30.1 | 0.17.4 / 2.4.1 / 1.32.0 | the newest |
| aqtinstall | 3.1.15 | 3.3.0 | gone with decision 4 |
| linuxdeploy, its plugins, the AppImage runtime | 2025 builds | the same | `linuxdeploy-plugin-qt` gone with decision 4 |
| dump_syms / minidump-stackwalk | 2.3.9 / 0.27.0 | the same | unchanged |
| GitHub Actions | newest, pinned by SHA | the same | unchanged |

## What it removes

- Options: 27 + 11 cache entries + 2 preset-only variables → 1 of the tree's (`OPENMW_SANITIZER`) and CMake's own.
- Presets: 10 visible configure presets over 11 parts → 4 flavours × 2 systems over 3 parts (`common`, `linux`,
  `windows`); their `cacheVariables` carry no flags.
- Branches: of 190, roughly 95 go — 28 `OPENMW_BUILD_*`, 17 precompiled-header blocks, 12 manifest blocks, the
  multi-config 8, the install layout's 10, the dependency checks the pin makes moot (FFmpeg's version floors,
  Bullet's float64 probe, the LuaJIT/Lua choice, TinyXML's, the find fallbacks), the Crashpad and DLSS fallbacks.
- `CI/install_ubuntu_deps.sh` shrinks to the compilers, the tools and the display and sound libraries SDL and Qt
  load; OpenMW's PPA goes.

## Decisions taken with the user

11. **Link-time optimisation: off where a build is made on a desk, on for every tagged release (taken).** Off by
    default in `Release` on both systems; the release workflow, which a pushed `rtx-` tag starts, configures with
    CMake's own `CMAKE_INTERPROCEDURAL_OPTIMIZATION=ON` (checked by `check_ipo_supported`), and a desk turns it on
    the same way when it wants to measure it. `OPENMW_LTO` goes. The distributions that build their packages with
    LTO by default — Fedora since 32, Ubuntu since 21.04, openSUSE — report single-digit gains in size and speed
    for a slower, hungrier link ([Fedora](https://fedoraproject.org/wiki/LTOByDefault);
    [Ubuntu](https://wiki.ubuntu.com/ToolChain/LTO); [Debian's proposal](https://lists.debian.org/debian-bsd/2022/06/msg00002.html)).
    So a measured run on a desk compares against the shipped binary only with the switch on.
12. **The tests' `-Og`: kept if it pays (taken).** Timed: a clean debug build's test binaries at `-Og` and at
    `-O2`. Kept as a setting of the test targets for GCC and Clang where it is clearly faster, MSVC's `/Od`
    only if Windows measures the same way; dropped otherwise. `OPENMW_TEST_COMPILE_OPTIONS` goes either way. GCC
    names `-Og` the level for the edit-compile-debug cycle and publishes no build-time figures
    ([GCC](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html)).
13. **The release built on AlmaLinux 8 with gcc-toolset-15 (taken).** The floor becomes glibc 2.28: Ubuntu 20.04
    and later, Debian 11 and later, SteamOS and the Steam Deck. The Linux vcpkg set is built in the same image,
    so its static libraries reference no newer glibc either, and serves the Ubuntu 26.04 desks and CI unchanged:
    an older glibc's objects link and run against a newer one, and gcc-toolset-15 and Ubuntu's GCC are the same
    major, one C++ ABI. A release check reads the binaries' highest `GLIBC_` symbol version. It is
    manylinux_2_28's way — an old base, Red Hat's newer toolset
    ([pypa/manylinux](https://github.com/pypa/manylinux)), and RHEL 8 ships gcc-toolset-15
    ([Red Hat](https://access.redhat.com/errata/RHBA-2025:21071)). Refused: Valve's sniper SDK (Debian 11, glibc
    2.31), Valve's target for games, whose compiler is older than the tree's GCC 15 floor; staying on Ubuntu 26.04.
    Red Hat supports a gcc-toolset about two years, so the image moves to the next one in time.

14. **The sets published by a CI workflow (taken).** A workflow builds each triplet's set when `vcpkg.json` or an
    overlay changes — Linux in the AlmaLinux 8 image, Windows with MSVC — and attaches it to a release
    `deps-<digest>` in xorza/openmw-rtx with the workflow's own token; `pins.py` names the published archives.
15. **Executed unattended, the whole plan, on its own branch (taken)**, gated locally at each step and, where a
    step touches Windows, built and tested on the laptop over SSH; master untouched until reviewed.

16. **CI's Linux jobs in AlmaLinux 8 too (taken, option 1).** The gcc, asan and tsan jobs run in the stock
    `almalinux:8` container with `CI/install_almalinux_deps.sh`, the environment the set and the release are
    built in, so one Linux environment builds, tests and ships; no image of the fork's own is published. The
    clang job takes LLVM's official release tarball, pinned in `pins.py`, as AlmaLinux 8 carries no Clang 23.

## Implementation plan

Each step builds every flavour on Linux, passes the gate, and passes CI on every job; the Windows laptop builds
`debug` after steps 3, 5 and 8.

3. **vcpkg in.** `vcpkg.json` with the baseline and the overrides, the overlay ports (OpenSceneGraph fork, Recast
   fork, Crashpad, Benchmark, unshield), the two static triplets; `omw` fetches the pinned vcpkg tool. A CI
   workflow builds each triplet's set and publishes it; `pins.py` names the published archives and `omw` fetches
   them. Nothing consumes the set yet.
4. **Linux builds against the set**, statically: the presets' toolchain file, `cmake/Dependencies.cmake`'s finds
   reduced to the set's packages, FetchContent gone, OpenSceneGraph's plugins registered in one file, static
   libstdc++; `install_ubuntu_deps.sh` down to compilers, tools and display libraries; the AppImage without the
   libraries `linuxdeploy` collected.
4b. **The release on AlmaLinux 8** (decision 13): the release image `FROM almalinux:8` with gcc-toolset-15, the
   Linux vcpkg set built in it, and the release check on the `GLIBC_` versions.
5. **Windows builds against the same set**, the CRT static, upstream's set, `VCPKG_TAG`, aqt and `QT_VER` gone.
6. **The libraries to their newest through the baseline**: Boost, Bullet 3.25, ICU, LuaJIT, OpenAL Soft, LZ4,
   zlib in one step; FFmpeg 9 and MyGUI 3.5 each in their own, with what their removed APIs ask of upstream's code
   recorded in `.notes/UPSTREAM.md`.
7. **The tests' `-Og`** timed and decided (decision 12), `OPENMW_TEST_COMPILE_OPTIONS` gone either way. The
   flags' file itself is in (`99c1324a71`).
8. **Four flavours**: every program in every flavour, `full` and `package` folded, `OPENMW_BUILD_*` removed;
   `omw`'s flavour list, `QT_FLAVOURS`, the listing check's excuses and CI's legs with them. After step 5, when
   Windows has Qt in every flavour. The other options went with step 7's file (`99c1324a71`).
10. **Licences**: every statically linked library's notice in `files/licenses` and installed, and the release
    notes naming the vcpkg baseline and overlays as the libraries' source.
11. **The proposal folds into `docs/rtx/build.md`** and goes.

## Decisions

- **The registry pins the versions, without vcpkg's version database.** `pins.py` pins GitHub's archive of
  microsoft/vcpkg at release 2026.10.16's commit and the tool that commit names (`scripts/vcpkg-tool-metadata.txt`),
  and the manifest names no `builtin-baseline`: a baseline and `overrides` need the registry's Git history, a
  clone of hundreds of megabytes on every desk, for nothing a pinned archive does not already fix. A port held
  off the registry's version is an overlay. ([vcpkg versioning](https://learn.microsoft.com/vcpkg/users/versioning);
  [vcpkg's bootstrap](https://github.com/microsoft/vcpkg/blob/master/scripts/bootstrap.sh))
- **MyGUI at the registry's 3.5.3**, not held at 3.4.3 by an overlay: the tree defines `MYGUI_DONT_USE_OBSOLETE`, so
  3.5's removals of obsolete API reach nothing it calls; step 6's MyGUI bump is this one.
- **The system's audio and display stay the system's.** OpenAL Soft opens ALSA by `dlopen` (`ALSOFT_DLOPEN`, its
  default), so no ALSA is linked and the system's plugins serve PipeWire and PulseAudio; Qt's xcb and Wayland
  plugins link the system's libxcb, xkbcommon and libwayland, which `CI/install_almalinux_deps.sh` brings the
  headers of. ([OpenAL Soft's CMake options](https://github.com/kcat/openal-soft/blob/master/CMakeLists.txt))
- **A set is vcpkg's own export, packed with gzip in parts of 1 GiB.** `vcpkg export --raw` carries the
  toolchain file a build configures with beside the libraries, so a build needs no registry. gzip, not xz:
  gigabytes of static libraries, and Python's xz runs on one core. Parts, because GitHub refuses a release asset
  of 2 GiB ([About releases](https://docs.github.com/repositories/releasing-projects-on-github/about-releases))
  and Windows' set, whose libraries carry their debug information (`/Z7`), packed to 1.9 GiB; the host tools'
  PDBs, a gigabyte of Qt's that no crash report reads, are left out.
  ([vcpkg export](https://learn.microsoft.com/vcpkg/commands/export))
- **The shader tools and the Vulkan headers from the set**, not LunarG's SDK on Linux: its Linux binaries need
  glibc 2.34 and do not run on AlmaLinux 8, where the set and the release are built. Windows takes the same
  ports, so both systems compile shaders with one shaderc. ([LunarG's Linux SDK](https://vulkan.lunarg.com/doc/sdk/latest/linux/getting_started.html))
- **A port's fix is an overlay of that port, never a triplet flag**: a triplet's contents are in every port's ABI
  hash, so a flag for one port rebuilds all of them
  ([binary caching, ABI hash](https://learn.microsoft.com/vcpkg/reference/binarycaching#abi-hash)). The overlays
  beside the OpenSceneGraph fork: uriparser with `_GNU_SOURCE`, as glibc 2.28 declares `reallocarray` under it
  alone; unshield in C17 (its MD5's K&R prototypes are no C23) and linking the zlib it calls; OpenAL Soft
  1.25.2 with its `operator*` projection as a lambda, which MSVC 14.52 refuses to take the address of
  (C3889) and upstream's master has replaced; and fontconfig as an empty port, so Qt links the system's, whose
  configuration matches the system's fonts.
