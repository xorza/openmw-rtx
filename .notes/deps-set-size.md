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

All the steps change the set, so they land together and the set is rebuilt once. vcpkg's binary cache keeps
every port they don't touch.

### 1. `pack_set` prunes and strips the export in place (`tools/omw/depsset.py`)

- **The digest reads `depsset.py`.** `deps.SET_INPUTS` names `vcpkg.json`, `vcpkg-configuration.json`,
  `vcpkg/*` and `env/install_almalinux_deps.sh`, but not the code that decides what the set carries. A change
  to packing alone therefore keeps the set's name, and the workflow finds it already published and skips it.
  Add `tools/omw/depsset.py` to `SET_INPUTS`, and extend `SetDigestTest` with it.
- **A `prune_set(exported, triplet)` step runs before `pack_set` packs anything**, and `pack_set` no longer
  filters. It does three things:
  - removes `installed/<triplet>/tools/**/*.pdb`, which is today's archive filter applied to the folder;
  - removes every file outside `tools/` that `installed/vcpkg/info/<port>_*_<triplet>.list` names for
    `shaderc`, `glslang` and `spirv-tools`, the ports the set carries for `glslc`, `spirv-opt` and `spirv-val`.
    The list is vcpkg's own record, not a list of names kept by hand. The presets configure with
    `VCPKG_MANIFEST_MODE=OFF`, so vcpkg never reads the stale `.list` files;
  - on Linux, runs `strip --strip-all` on every ELF file (`\x7fELF`) under `installed/<triplet>/tools/` and
    `installed/<triplet>/bin/`, using the active gcc-toolset's binutils. Scripts and links stay as they are.
- `deps_set` moves the pruned export to `deps.vcpkg_set_dir(digest)` as it does now, so the desk keeps
  what was packed.
- Tests in `tools/omw/tests/test_depsset.py`, on a synthetic export:
  - exactly the files outside `tools/` that the listed ports name are gone, and another port's are not;
  - a tool's PDB is gone and its `.exe` stays;
  - an ELF file under `tools/` loses its `.symtab` while a shell script beside it is byte-identical;
  - packing the pruned folder twice gives identical parts.
- Update `pack_set`'s docstring, and the part-size comment that cites Windows' debug information.

### 2. A qttools overlay (`vcpkg/ports/qttools`)

- Copy the port from the pinned registry, change nothing else, and keep the diff small so the registry's next
  version compares line for line (`build.md`, Dependencies).
- `vcpkg.json`: the `linguist` feature no longer depends on `qttools[designer]`. Qt's Linguist GUI, the
  reason for that dependency, also needs Qt Quick, which the set does not have
  (`src/linguist/CMakeLists.txt`), so it is never built.
- `portfile.cmake`, in `qt_cmake_configure`'s options: `-DFEATURE_pixeltool=OFF -DFEATURE_qtdiag=OFF
  -DFEATURE_kmap2qmap=OFF -DFEATURE_qtplugininfo=OFF -DFEATURE_qtattributionsscanner=OFF -DFEATURE_qev=OFF`.
  `TOOL_NAMES` stays as it is: it already names programs that are never built (`assistant`, `qdoc`), and
  `qt_fixup_and_cleanup` passes over a missing one.
- Check: the set has no `designer`, `pixeltool`, `qtdiag*`, `kmap2qmap`, `qtplugininfo` or
  `qtattributionsscanner`, and no `libQt6Designer*`; `lrelease` and `lupdate` stay, and qttranslations,
  qtsvg and qtwayland still build.

### 3. `openmw-osg` builds what the tree registers (`vcpkg/ports/openmw-osg`)

- A patch applied through `vcpkg_from_github(... PATCHES ...)` drops two sets of entries:
  - from `src/CMakeLists.txt`'s library loop: osgManipulator, osgUI, osgVolume, osgTerrain, osgWidget and
    osgPresentation;
  - from `src/osgWrappers/serializers/CMakeLists.txt`: every `ADD_SUBDIRECTORY` but `osg`. The tree registers
    only `osgdb_serializers_osg` (`USED_OSG_PLUGINS`, `cmake/Dependencies.cmake`).
- `-DBUILD_OSG_PLUGIN_KTX=OFF`: `USED_OSG_PLUGINS` does not name it.
- Check: the set's `lib/` holds the twelve libraries `find_package(OpenSceneGraph ...)` and the
  `OpenSceneGraph::` foreach name, plus OpenThreads, and nine plugins.

### 4. An FFmpeg overlay (`vcpkg/ports/ffmpeg`)

- Copy the port from the pinned registry. In `portfile.cmake`, append `--disable-debug` to `OPTIONS_RELEASE`
  and `--disable-encoders --disable-muxers` to `OPTIONS`. With `--disable-debug`, FFmpeg's `configure` stops
  appending `-g` (or `-Z7` on MSVC) after the triplet's flags, so the triplet alone sets the debug level.
- Check: no `.debug_loc` section in `libavcodec.a`, `libavformat.a` or `libswscale.a`
  (`readelf -S`, and the same command showing `.debug_line` proves it reads the archive), and no `ff_*_encoder`
  or `ff_*_muxer` symbol (`nm`). Both legs' tests that decode Bink, MP3 and WAV pass.

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
