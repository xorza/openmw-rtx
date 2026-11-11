# The dependency set: only what the tree links or runs

Plan to shrink the vcpkg set (`omw deps-set`, published as `deps-<digest>`). Today it is 831 MiB packed on
Linux and 1,730 MiB on Windows. Repacking the published set with these cuts gives 392 MiB and 1,217 MiB. The
measurements behind every item are in `docs/rtx/research/2026-11-11-dependency-set-size.md`. Delete each step
when it lands. When the last one lands, move the decisions into `docs/rtx/build.md` (step 6) and delete this
file.

## Decisions

- **Debug information: line tables on Linux, `/Z7` on Windows.** Linux already builds with `-g1`, GCC's
  equivalent of `-gline-tables-only`; FFmpeg alone goes past it, and step 4 fixes that. MSVC has no
  line-tables-only mode (Chromium's `minimal_symbols`: "MSVC doesn't have an equivalent option"), so Windows
  keeps vcpkg's `/Z7`. Refused: clang-cl with `-gline-tables-only` for the Windows set, which brings in a
  second compiler and leaves every port depending on its build working under clang-cl. Also refused: no
  debug information in the Windows archives, because a crash frame inside a dependency would get the name of
  the nearest public symbol above it, which is wrong for every internal function.
- **Build less rather than delete afterwards**, wherever a port has a switch: qttools features, OSG's library
  lists, FFmpeg's `configure`. `pack_set` removes files only where no switch exists: the libraries of ports
  the set carries only for their programs, and the debug information of host programs.
- **The set a desk keeps is the set that is published.** `pack_set` changes the export in place before
  packing it, instead of filtering only the archive. Today a Windows desk keeps the PDBs the archive drops.
- **Kept, and not to be proposed again without a new reason:**
  - qtbase's host programs (qmake, androiddeployqt, qtpaths and the like): Qt's package configs import them
    and check that the files exist, so they are stripped instead.
  - `harfbuzz-subset`: `harfbuzzConfig.cmake` declares it, and Qt finds harfbuzz through that config.
  - `Qt6WaylandCompositor`: 19 MiB, Linux only, and removing it takes an overlay of qtwayland.
  - `Qt6UiTools`, Bullet's Dynamics and SoftBody, Boost graph and serialization: no switch turns them off.
  - FFmpeg's decoders and demuxers: a mod's audio or video can be in any format FFmpeg reads.
- **Not pursued:** compressed debug sections (`-gz`), which leave the gzip'd download and the relocations as
  they are; DWARF 5, unmeasured; another compressor than gzip, which is a separate change.

## Steps

### 5. Rebuild, verify, publish

- `omw deps-set` on the desk, which builds in the AlmaLinux 8 container; then every flavour (`debug`,
  `release`, `asan`, `tsan`) configured and built against the new set, `./omw test`, `omw archive`, and the
  AppImage's programs started once each. The launcher and wizard still find Qt's translations.
- Measure the new parts against the research: Linux at or under 392 MiB, Windows at or under 1,217 MiB. Add
  the measured sizes to the research file.
- Push. The Dependency sets workflow builds and publishes both systems with `--latest=false` and without
  `--prerelease`, so the game's release stays the latest. Then point `pins.py`'s `VCPKG_SET` at the new
  digest and `SHA256SUMS`, and let the Windows CI leg build and test against it.
- `./omw gate` once at the end.

### 6. Fold into `docs/rtx/build.md`

- Under Dependencies, two bullets: **the set carries what the tree links or runs** (build less where a port
  has a switch, prune in `pack_set` where it has none, and the desk keeps what is published), and **debug
  information is line tables on Linux and `/Z7` on Windows**, with the refused options and sources from the
  Decisions above. Then delete this file.
