# The build: one way to do each thing, and a green run that prints no warning

Plan to simplify the CMake files and clear every warning a green run prints. Sources:
- a reading on 2026-11-11 of every `CMakeLists.txt` and `cmake/*.cmake` outside `vcpkg/ports/` and the
  Doxyfiles;
- the last green CI run ([42985566605](https://github.com/xorza/openmw-rtx/actions/runs/42985566605)) and the
  last dependency-set run that built every port
  ([42939211492](https://github.com/xorza/openmw-rtx/actions/runs/42939211492)).

Neither run had a GitHub annotation, and no warning came from the tree's own code.

It also carries the rest of `deps-set-size`'s own plan, in steps 11 and 12. Delete each step when it lands.
When the last one lands, move the lasting decisions into `docs/rtx/build.md` and delete this file.

**Where it runs:** on `deps-set-size`, after that branch's own work, as one branch with one merge.

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
    that is C4267 on yaml-cpp alone. The vendored projects under `extern/` set no level of their own, so there
    MSVC takes `/w` in the directory's flags like the rest.
- **One compiler for everything built on Linux, tools included** (decided 2026-11-11). The set and the tree
  already build with gcc-toolset-15, as the programs link libstdc++ statically. The env script's tools
  (autoconf, zsync, `wayland-scanner`) ship nothing, but building them with the same GCC leaves nothing
  standing on a GCC only a package dependency installs. Refused: AlmaLinux 8's system GCC 8.5 for them.
- **Output appears on failure only.** A smoke run (benchmarks, integration tests) prints its detail when it
  fails, and its logs are kept either way.
- **CMake's own warnings for authors are errors** (decided 2026-11-11). The presets turn CMake's dev and
  deprecation warnings into errors (`"errors": {"dev": true, "deprecated": true}` in `common`), CMake's
  canonical switch
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

The order makes the longest wait start first and run beside everything else:
- **Step 1 is the only step that changes the set**, and a set build in CI takes hours, Windows's most. So it
  goes first, and its push starts that build at once, while the desk builds the Linux set for itself. Its
  caches are warm from run 43016471135, so it rebuilds little beyond Qt (the `xrender` change),
  vulkan-loader, collada-dom and OSG.
- **Steps 2 to 10 need no CI**, so they run on the desk while that build does, against the desk's own Linux
  set. Dead code goes first, so later
  steps rewrite less; then one step per file family, each in a single pass. Only steps 7 (third-party `-w`)
  and 8 (test options) recompile much, and ccache keeps the rest cheap.
- **Step 11 trims build time and the release binaries** while CI builds the set, its measured items on the
  desk. **Step 12 is the one CI round**, once step 1's set is published. Three dispatched runs at once check
  everything together, `/cgthreads1` included, instead of a round per step. Every step stays a commit of its
  own, so a red run's log still names the step.
- **Step 13 closes** with the one gate.

Each local step is a configure, `./omw build` and `./omw test`. Two kinds of step check more:
- **A step that changes a sanitizer or what is installed** builds what it changed: the asan flavour, or
  `omw archive` with its file list compared against the one before. Windows waits for step 12.
- **A step that removes a warning** shows it gone from the log of the run it changes. That means no line
  matching `warning` in that step's log (case-insensitive), with a known hit from before the step shown to
  match the same search.

## Progress

Steps leave the list below when they land, and are marked here.

- [~] 1. The set's inputs: committed (`29881a0baa`) and pushed; the desk's Linux set built with no unused-variable
  warning. Waits for the Dependency sets workflow (run 43030096407) to publish `deps-1156990a01a7c89d`.
- [x] 2. Dead code and stale words: `03adbdb442`, with `5338c04dfc` laying out `Install.cmake`.
- [x] 3. What the pinned set makes constant: `16efcd3d87`, corrected by `75d690fb89` (the pinned OSG has no
  multiview, so `OSG_HAS_MULTIVIEW` stays undefined).
- [x] 4. Resources through `configure_file` and one folder name: `526b5ea37d`; the resources byte-for-byte as before.
- [x] 5. The version, filled once: `e7979d3c36`. It also fills the AppStream release date step 2 had emptied; a
  tree without `.git` stops at configure.
- [x] 6. Dependencies, each found one way: `a80289914a`.
- [x] 7. Third-party code, in one style and quiet: `12c015d20f`. `-w` reaches 608 of 610 third-party
  files and none of the tree's 1,880 (`compile_commands.json`); the other two are Crashpad's assembly.
- [x] 8. Tests, benchmarks and smoke runs: `6670b513c1`, with `dd158848ff` teaching
  `RtxSourceTreeTest` the call layout gersemi wraps, which it had never read for `rtx-gpu-tests`. The three
  benchmarks run as dry-run tests in 0.01 to 0.64 s.
- [x] 9. Translations: `f08899d134`. It also fixes the build's `.qm` files: `qt_add_translation` had made one
  rule, `wizard_sv.qm`, of the 21, so every build shipped one of the programs' translations; `qt_add_lrelease`
  makes all 21, and those both builds made are byte-identical. `omw translations` on the unchanged tree changes
  no file.
- [x] 10. Install and Windows properties: `a44f4d54eb`. A probe `AUTHOR_WARNING` now stops the configure
  ("CMake Error (author)"), and the configure prints no CMake warning. The PDBs and the console are Windows's,
  checked by step 12's runs.

## Steps

### 1. The set's inputs, built by CI while the rest goes on

Everything here changes the set's digest (`deps.SET_INPUTS`), so it goes first. Pushed, it starts the
Dependency sets workflow, which builds and publishes the set for both systems. The desk builds the Linux set
for the same inputs at once with `omw deps-set`, in the background, so that steps 2 to 10 build against it
without waiting for CI; its binary cache is warm, so it rebuilds little beyond Qt. The env script's riskiest
part, zsync and `wayland-scanner` under GCC 15, is run once in a plain `almalinux:8` container first.
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

### 11. Faster builds, leaner release binaries

Found on 2026-11-11 on the desk: ccache at its 30 GB cap with 3,932 cleanups and a 34.5% hit rate; a set bump
recompiling all 2,747 objects; `omw format` taking 4.6 s at every `omw build`; the release `openmw` 557 MB with
130 MB of code and read-only data. The certain items land here, before the CI round, so step 12's runs check
them on Windows too; the measured ones keep only what a measurement pays for.

- **The set at one path** (`tools/omw/deps.py`). `OMW_VCPKG_SET` names `deps/vcpkg-set-<digest>`, so every
  `-isystem` path moves with the set, and ccache misses every file after a bump. The toolchain reads a fixed
  `deps/vcpkg-set` instead, a link the driver points at the digest's folder (a junction on Windows), so a bump
  recompiles only what includes a header that moved. Check: after a set bump with unchanged headers, a build
  recompiles nothing.
- **`omw build` formats what changed.** It runs the formatter over the whole tree first, 4.6 s a build. It
  formats the files `git` reports changed against `HEAD`, untracked ones included; `omw format` with no files
  keeps formatting the tree, and CI's `format --check` keeps checking it whole.
- **MSVC's release links drop what nothing references** (`cmake/Flags.cmake`). Every MSVC link takes `/DEBUG`,
  which makes the linker's default `/OPT:NOREF,NOICF`
  ([Microsoft](https://learn.microsoft.com/cpp/build/reference/opt-optimizations)), so a release program keeps
  unreferenced functions and identical COMDATs. Release links take `/OPT:REF /OPT:ICF`, and compiles `/Gw`
  for data, as `/O2`'s `/Gy` does for functions. Check: step 12's Windows release leg links, and its programs
  are smaller.
- **ccache's limit** (the desk's own, not the tree's): `CCACHE_MAXSIZE` 30G in the fish config becomes 100G,
  the user's to change. Four flavours of about 2,800 objects, and the A/B worktrees, do not fit 30 GB.
- **Measured, kept only if it pays**, each an A/B of `omw release bench` against the build before it (quiet
  desktop, warm card, medians and p99), with the binary's size:
  - `-ffunction-sections -fdata-sections` and `--gc-sections` on Linux (mold supports it), for the tree's
    code; the set's archives keep their own sections.
  - `-march=x86-64-v2` for the tree: every CPU beside an RTX 20 or an RDNA 2 card has it (SSE4.2, POPCNT). If it
    pays, the set's triplets take it at the next set rebuild, which it would otherwise force. `x86-64-v3`
    (AVX2) is a decision of its own, as it shuts out pre-Haswell CPUs.
  - Precompiled headers for the heaviest headers (OSG, Qt, sol3), with ccache's
    `sloppiness=pch_defines,time_macros` and GCC's `-fpch-preprocess`: `build.md` refused them as ccache caching
    through none, which ccache's manual contradicts. Measured as a clean build's time with a cold and a warm
    cache.
  - Sharing the cache across worktrees: `base_dir` at the projects folder with `-fdebug-prefix-map`, kept only
    if `omw crash` still names every line from a release's symbols.
- **Last, and only if the rest leaves time: profile-guided optimisation**, trained on `omw bench`'s suites,
  measured once for the CPU side (cell loading, scripts). Training needs a GPU, which CI's runners lack, so the
  profiles would come from the desk and age with the code; it lands only if it pays more than 5% of a frame or of
  a load.

### 12. One set, pinned, and every change checked by CI at once

- Run 43016471135, which built `deps-8948c4c640db7edd`, was cancelled when step 1 was pushed, its Windows job
  still building: that set is never pinned, as step 1's replaces it, and the cache it saved on cancelling
  carries every port it had built into step 1's build. So step 1's published set is the one measured: read
  its Windows job's log for a warning the research does not list, and measure its Windows parts against the
  research's 1,217 MiB, writing the size into `docs/rtx/research/2026-11-11-dependency-set-size.md`.
- When the Dependency sets workflow has published step 1's set, point `pins.py`'s `VCPKG_SET` at that digest
  and the `SHA256SUMS` it published.
- `/cgthreads1` (from upstream since 2025-12-24, for "CI's 8 GB") goes from its 8 files
  (`components/CMakeLists.txt`, `apps/openmw/CMakeLists.txt`, `apps/openmw_tests/CMakeLists.txt`). This
  repository is public, and GitHub's standard Windows runners for public repositories have 16 GB.
- Push, then start three CI runs on the branch at once (`gh workflow run ci.yml --ref deps-set-size`). A
  dispatched run has a concurrency group of its own, so the three run side by side. Together they check:
  - Windows against the pruned set, which no build has linked yet;
  - steps 1 to 11;
  - `/cgthreads1`'s absence: Actions reports no peak memory, so the measure is three Windows legs passing
    without `C1060` (out of heap space) or a lost runner, together with the leg's build time against the run
    before.

  If a Windows leg fails for memory, `/cgthreads1` comes back with its comment's figure corrected, and one run
  checks it.

### 13. Close

- On the desk, the desk's own build of the set removed (`deps/vcpkg-set-<digest>`), so `omw` fetches the
  published one that every other build takes.
  Then all four flavours, `./omw test`, `omw archive` with its file list compared against the one before, each
  AppImage program started once (the launcher and the wizard find Qt's translations), and `./omw gate` once.
- Move this plan's lasting decisions into `docs/rtx/build.md`, add `.notes/UPSTREAM.md`'s lines, and delete
  this file.
