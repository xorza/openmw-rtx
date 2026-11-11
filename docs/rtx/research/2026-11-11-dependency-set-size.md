# What the dependency set carries, and what it needs to

Measured 2026-11-11 on the set `deps-5e394e368d7fabb7`. The Linux side is measured from the export on a desk,
which is byte-for-byte what was packed: repacking it the way `pack_set` does (tar inside gzip level 6, mtime 0)
gives 831 MiB, the size of the published asset. The Windows side is measured from the published parts, listed
and repacked as a stream. "Used" means a path into the set that some `build.ninja` names, across the `release`,
`debug` and `asan` builds, which were configured against `deps-a41c0e8b02a43c55` (the same ports). That covers
what is linked and what is run. Configure-time reads, such as package configs and headers, are counted
separately. There are no Windows build trees on the desk, so Windows usage is taken from Linux by library name.

## Linux: 3,460 MiB unpacked, 831 MiB packed

| folder | size | used by a build |
|---|---|---|
| `lib/`, static archives | 1,549 MiB | 1,096 MiB |
| `tools/`, host programs | 1,569 MiB | 237 MiB |
| `include/` | 169 MiB | (headers) |
| `bin/` (`kmap2qmap`, `unshield`) | 84 MiB | 0 |
| `share/` | 43 MiB | (package configs, licences) |

The build runs eight of the host programs: `moc`, `rcc`, `uic`, `lrelease`, `lupdate`, `glslc`, `spirv-opt`
and `spirv-val`. Every Qt program is linked statically against Qt and keeps its line tables and symbols.
`designer` is 191 MiB, `qtdiag` and `qtdiag6` are 131 MiB each (two identical files, not links), and
`pixeltool` is 119 MiB. `strip --strip-all` takes every ELF file under `tools/` and `bin/` from 1,652 MiB to
418 MiB.

What the archives are made of, summed over every section of every `lib/**/*.a` (`readelf -S`):

| sections | size |
|---|---|
| relocations of the debug sections, `.rela.debug_*` | 570 MiB |
| `.debug_str` | 199 MiB |
| code and data (`.text*`, `.rodata*`, `.data*`, `.bss*`) | 174 MiB |
| `.debug_info` | 159 MiB |
| `.debug_ranges` | 99 MiB |
| `.debug_line` | 85 MiB |
| relocations of code and data | 68 MiB |
| `.symtab` and `.strtab` | 55 MiB |
| `.debug_loc` | 43 MiB |
| the rest | 44 MiB |

Debug information is 1,165 of the 1,496 MiB. The triplet already compiles with `-g1`. In GCC that means
function descriptions and line tables but no local variables, and Clang treats `-g1` as `-gline-tables-only`
([GCC](https://gcc.gnu.org/onlinedocs/gcc/Debugging-Options.html); Chromium's
[`minimal_symbols`](https://chromium.googlesource.com/chromium/src/+/main/build/config/compiler/BUILD.gn)).
Most of these bytes are therefore what line tables cost inside relocatable objects. They are DWARF 4: the
objects come from GCC 15.2.1 (Red Hat's build), and nothing in the set asks for a different DWARF version.

FFmpeg is the one port with variable information. `.debug_loc` appears only in `libavcodec.a` (34.3 MiB),
`libavformat.a` (4.7 MiB) and `libswscale.a` (2.7 MiB). Unless `--disable-debug` is given, FFmpeg's
`configure` appends `-g` after the caller's flags (`enabled debug && add_cflags -g"$debuglevel"`), and the
last `-g` level wins. The registry port passes only `--enable-optimizations` for release, so FFmpeg builds at
`-g2`. On MSVC the same line becomes `-Z7`.

Archives no build links:

| archives | size | why they are there |
|---|---|---|
| shaderc, glslang, SPIRV-Tools (14 archives) | about 200 MiB | the build runs `glslc`, `spirv-opt` and `spirv-val`, which link them statically; the tree finds the programs with `find_program` and never `find_package`s the libraries |
| `libQt6Designer.a`, `libQt6DesignerComponents.a` | 94 MiB | the qttools port's `linguist` feature depends on `designer`, for the Linguist GUI; that GUI also needs Qt Quick, which the set does not have, so it is never built |
| six OSG libraries (osgPresentation, osgVolume, osgWidget, osgUI, osgManipulator, osgTerrain), 13 serializer plugins, `osgdb_ktx` | 68 MiB | OSG builds every library and serializer unconditionally, and the overlay turns KTX on; `USED_OSG_PLUGINS` registers nine plugins, without KTX |
| `libharfbuzz-subset.a` | 48 MiB | kept: `harfbuzzConfig.cmake` declares it, and Qt finds harfbuzz through that config |
| `libQt6WaylandCompositor.a` | 19 MiB | kept: removing it takes an overlay of qtwayland (`FEATURE_wayland_server`), Linux only |
| `libQt6UiTools.a`, Bullet's Dynamics and SoftBody, Boost graph and serialization | about 30 MiB | kept: built whenever their ports build, with no switch to turn them off |

FFmpeg also builds every encoder and muxer it has, and nothing in the tree encodes or muxes: no
`avcodec_find_encoder`, `avformat_alloc_output_context2` or `av_interleaved_write_frame` appears under
`apps/`, `components/` or `extern/`. By object name, encoders and muxers are 33 of `libavcodec.a`'s 220 MiB
and 13 of `libavformat.a`'s 53 MiB. That is a lower bound, because their shared helpers are named otherwise.
Decoders and demuxers are kept: a mod's audio or video can be in any format FFmpeg reads.

Repacking the export with the prunings above applied and every ELF file under `tools/` and `bin/` stripped,
without rebuilding any port, gives **1,735 MiB unpacked and 392 MiB packed**, down from 3,460 and 831 MiB. The
FFmpeg changes would come on top of that and were not measured.

## Windows: 8,291 MiB unpacked, 1,730 MiB packed

| folder | size |
|---|---|
| `lib/` | 7,514 MiB |
| `tools/` (`pack_set` already drops the PDBs) | 334 MiB |
| `Qt6/plugins` | 210 MiB |
| `include/` | 165 MiB |

vcpkg's Windows toolchain hard-codes `/Z7` for release (`scripts/toolchains/windows.cmake`, the
`CMAKE_C_FLAGS_RELEASE` and `CMAKE_CXX_FLAGS_RELEASE` lines), so every object carries full CodeView,
including every type it uses, repeated in each object. The same ports are 4.9 times their Linux size:
`collada-dom` is 726 MiB against 79 MiB, `osg` 352 MiB against 36 MiB, and `SPIRV-Tools-opt` 709 MiB against
85 MiB. The archives no build links, named as on Linux: the unused OSG libraries and plugins are 1,293 MiB,
the shaderc, glslang and SPIRV-Tools archives 1,194 MiB, and the Designer archives 148 MiB. With those
pruned (404 files), the set is **5,548 MiB unpacked and 1,217 MiB packed**, down from 8,291 and 1,730 MiB.
At 1 GiB a part, that is still two parts. About 4.8 GiB of what remains is archives that still carry `/Z7`.

MSVC has no line-tables-only mode. `/Zd` used to be one, but `cl.exe` from 2017 on does not accept it, and
clang-cl dropped its alias for that reason ([D114632](https://reviews.llvm.org/D114632)). Chromium's
`minimal_symbols` reads: "Enable line tables for clang. MSVC doesn't have an equivalent option." The `/Z7`,
`/Zi` and `/ZI` formats all carry types ([Microsoft](https://learn.microsoft.com/cpp/build/reference/z7-zi-zi-debug-information-format)).
A Windows set with line tables only therefore has to be compiled by clang-cl with `-gline-tables-only`, which
is what Chromium does.

## Plan

Each step changes the set's digest, so the set is rebuilt with `omw deps-set`, every flavour is built and
tested against it, `omw archive` is run, and then CI publishes the set and `pins.py` names it. No step touches
upstream's code: the overlays, `tools/omw` and the workflows all belong to the fork.

1. **Strip the host programs on Linux in `pack_set`**, as Windows already drops their PDBs. Nobody reads
   symbols for programs the build only runs. The pruning and stripping are applied to the export in place,
   before packing, so the set a desk keeps is the set that is published. Today the desk keeps the PDBs the
   archive drops.
2. **Keep only the programs of ports that exist for their programs.** For shaderc, glslang and SPIRV-Tools,
   `pack_set` removes every file outside `tools/`, taking the list from vcpkg's own record of what each port
   installed (`installed/vcpkg/info/<port>_*.list`) rather than from a list of names kept by hand.
3. **A qttools overlay** in which `linguist` no longer depends on `designer`, and `pixeltool`, `qtdiag`,
   `kmap2qmap`, `qtplugininfo`, `qtattributionsscanner` and `qev` are turned off. That stops vcpkg from
   building `designer` and these programs, instead of building them and deleting them afterwards.
4. **The `openmw-osg` overlay builds what the tree registers**: a patch drops the six unused libraries and
   their 13 serializers from OSG's two `CMakeLists.txt` lists, and `BUILD_OSG_PLUGIN_KTX` goes off.
5. **An FFmpeg overlay**: `--disable-debug`, so the triplet alone sets the debug level, plus
   `--disable-encoders --disable-muxers`.
6. **Windows keeps `/Z7`** (decided 2026-11-11), which is 1,217 MiB packed after steps 2 to 5. Two options
   were refused:
   - clang-cl with `-gline-tables-only`, which matches Linux and is what Chromium does: refused because it
     brings a second compiler into the Windows set and leaves every port depending on its build working
     under clang-cl.
   - No debug information in the Windows archives: refused because a crash frame inside a dependency would
     get the name of the nearest public symbol above it, which is wrong for every internal function, and it
     would have no line.

Not taken: deleting qtbase's host programs (qmake, androiddeployqt, qtpaths and the like). Qt's package configs
import them and check that the files exist, so they are stripped instead. Also not taken: compressed debug
sections (`-gz`), which shrink the unpacked set but not the gzip'd download and leave the relocations as they
are; and DWARF 5, which may be smaller but was not measured.
