# CI with no warnings: a clean success is silent

Plan to clear every warning a green CI run prints, found in the last green CI run
([42985566605](https://github.com/xorza/openmw-rtx/actions/runs/42985566605)) and the last dependency-set run
that built every port ([42939211492](https://github.com/xorza/openmw-rtx/actions/runs/42939211492)) on
2026-11-11. Neither run had a GitHub annotation, and no warning came from the tree's own code. Delete each step
when it lands. When the last one lands, move the lasting decisions into `docs/rtx/build.md` and delete this
file.

## Decisions

- **A third-party project's warnings are switched off, not fixed.** The tree does not patch fetched or vendored
  code for its compiler's opinions, and a warning nobody acts on teaches every reader to skip warnings. The
  tree's own targets keep every check, with warnings as errors.
- **A tool the build uses is built quietly.** zsync and `wayland-scanner` are built in the env script for what
  they do, not for their source's state.
- **Output appears on failure only.** A smoke run (benchmarks, integration tests) prints its detail when it
  fails, and its logs are kept either way.
- **Left as they are, and not to be proposed again without a new reason:** the Qt ports' own messages from
  vcpkg (`qtbase currently requires packages from the system package manager`, `qtwayland requires
  libwayland-dev`). They print on purpose, only when the set rebuilds Qt, and only a copy of Qt's biggest port
  of our own removes them, which means maintaining it at every registry bump and rebuilding all of Qt on every
  change to it.

## Steps

Each step is checked by the log of the run it changes: no line matching `warning`, `WARNING` or
`##[warning]` in that step, matched case-insensitively, with one known hit from before the step shown to match
the same search. The full CI run is the verdict, and `./omw gate` runs once at the end.

### 1. zsync built quietly (`env/install_almalinux_deps.sh`)

- `CFLAGS="-O2 -std=gnu17 -fpermissive"` becomes `CFLAGS="-O2 -w"`. AlmaLinux 8's system GCC 8.5 builds zsync,
  since the script enables no gcc-toolset. That GCC rejects `-fpermissive` for C (25 warnings a Linux job),
  needs no `-std=gnu17`, its default, and has none of GCC 14's new errors, which is what the comment cites.
  `-w` also takes the warnings of zsync's own code: `_BSD_SOURCE` deprecated (15), `inflate.c`, `client.c` and
  `zsync.c`.
- Fix the comment: GCC 8.5 builds it; the flags quiet a tool the build only runs.

### 2. `wayland-scanner` that knows SDL's protocols (`env/install_almalinux_deps.sh`)

- SDL3 runs the system's `wayland-scanner`, which is 1.21 on AlmaLinux 8. SDL's protocol files use the
  `deprecated-since` attribute, which the scanner's built-in schema gains in wayland 1.23 (checked in
  `protocol/wayland.dtd` at 1.21.0, 1.22.0, 1.23.0 and 1.24.0), so each run prints "XML failed validation
  against built-in DTD" and the validity errors after it.
- Build `wayland-scanner` 1.24.0 from its release archive, pinned by SHA-512, into `/usr/local`, as autoconf
  and zsync are built: `meson setup -Dlibraries=false -Ddocumentation=false -Dtests=false`, then the scanner
  alone. Meson must be at least 0.57 (`meson_version` in wayland's `meson.build`). Find out whether AlmaLinux
  8's repositories carry one; otherwise use a pinned meson in a Python 3.12 venv.
- Check that SDL finds `/usr/local/bin/wayland-scanner` first, that the generated protocol code compiles and
  links against the system's libwayland 1.21, and that the game still opens a window under Wayland. The system
  keeps libwayland (`build.md`, Dependencies), and only the generator changes.

### 3. The fetched projects compile quietly (`cmake/Dependencies.cmake`)

- Inside the `block()` that fetches SDL3, Recast, yaml-cpp, Google Test, Google Benchmark, volk, VMA and
  Crashpad, append `-w` to `CMAKE_C_FLAGS` and `CMAKE_CXX_FLAGS` (`/w` under MSVC). Each fetched folder takes
  the value in force when CMake finishes it, which is inside the block, so the tree's own folders never see it.
  This removes Clang's `-Wnontrivial-memcall` in Recast and `-Wunused-but-set-global` ×9 in SDL, MSVC's C4267
  ×6 in yaml-cpp, and the asan job's `-Wmaybe-uninitialized` inside libstdc++'s `std::function`, which Google
  Benchmark's `std::regex` instantiates.
- Check that the flags reach no target of the tree's (`compile_commands.json`: no `-w` on a file under
  `components/` or `apps/`), and that the sanitizers still instrument the fetched code.

### 4. The benchmarks as dry-run tests (`cmake/Tests.cmake`, `.github/workflows/ci.yml`)

- The CI step "Benchmarks, run once each as upstream runs them" prints `***WARNING*** ASLR is enabled`, because
  Docker's seccomp profile refuses the `personality` call `BENCHMARK_MAIN` makes to switch it off, and
  `***WARNING*** Library was built as DEBUG`, because the debug flavour builds Google Benchmark without
  `NDEBUG`. Both concern timings, and the step measures nothing.
- Register the three benchmarks as CTest tests with `--benchmark_dry_run` (Google Benchmark 1.10: one
  iteration, no measurement), so every `omw test` and every CI test run executes them and shows their output
  only on failure. Delete the CI step.

### 5. collada-dom's config looks for no other Boost (`vcpkg/ports/collada-dom`)

- The config the overlay installs runs, on Windows alone, `find_package(Boost 1.92 EXACT COMPONENTS thread
  date_time)` with dynamic-Boost settings. `1.92` does not exactly match the set's 1.92.0, which is the
  Windows configure's "Could not find a configuration file for package Boost that exactly matches requested
  version" warning, and the set is static. Drop the block from the config template in the overlay. The tree
  links Boost through its own targets (`osgdb_dae_calls` in `cmake/Dependencies.cmake`).
- This changes the set's digest, so it lands with the next set rebuild (step 7's, or the next one for another
  reason).

### 6. Integration tests print on failure (`.github/workflows/ci.yml`)

- `omw debug integration --verbose` prints the game's whole log, including the example suite's own content
  warnings (`addAnimSource: can't find bone ... in meshes/land_racer.dae`, dozens a run). Drop `--verbose`:
  `scripts/integration_tests.py` still prints every line when a run ends in a fatal error, and copies
  `openmw.log` into its output folder, which the job keeps.

### 7. The set rebuilds quietly (`vcpkg/ports/openmw-osg`, a vcpkg PR)

- `openmw-osg` passes `OSGVIEWER_USE_XRANDR` and `_OPENTHREADS_ATOMIC_USE_*` on Windows, where OSG's CMake reads
  none of them ("variables are not used"). Pass the X11 one on Linux alone, and delete the Windows atomics
  options. Check that the Windows build log has no unused-variable warning for the port.
- vcpkg's `qtbase` port maps its `xrender` feature onto `FEATURE_xrender` and `FEATURE_xcb_native_painting`,
  which Qt 6.11 removed (`src/gui/configure.cmake` has them at 6.8.0 and not at 6.11.0), and passes them
  whether or not the feature is chosen. vcpkg's master has the same lines. The fix is a pull request to
  microsoft/vcpkg that drops the two mappings and the matching `CMAKE_DISABLE_FIND_PACKAGE_XRender`, which the
  user opens, then the registry pin that carries it.
- `vcpkg.json` asks qtbase for `xrender` on Linux, a feature Qt 6.11 does not have. Remove it at the next set
  rebuild that rebuilds Qt anyway (a registry bump), as on its own it forces all of Qt to rebuild for nothing
  that shows.
