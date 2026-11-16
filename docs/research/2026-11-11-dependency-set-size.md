# What the dependency set carries, and what it needs to — 2026-11-11

Measured on the vcpkg set of that day. "Used" means a path into the set that some `build.ninja` names, across the
`release`, `debug` and `asan` builds; Windows usage is taken from Linux by library name.

## Linux: 3,460 MiB unpacked, 831 MiB packed

- **Debug information is 1,165 of the archives' 1,496 MiB**, most of it relocations of the debug sections. The triplet
  compiles with `-g1`, which is function descriptions and line tables in GCC and `-gline-tables-only` in Clang
  ([GCC](https://gcc.gnu.org/onlinedocs/gcc/Debugging-Options.html), Chromium's `minimal_symbols`), so these bytes are
  what line tables cost inside relocatable objects.
- **FFmpeg built at `-g2`**: unless `--disable-debug` is given, its `configure` appends `-g` after the caller's flags
  and the last level wins; on MSVC the same line becomes `-Z7`. It also builds every encoder and muxer, which nothing in
  the tree uses; decoders and demuxers stay, since a mod's audio or video can be in any format FFmpeg reads.
- **The host programs keep their symbols**: every Qt program is linked statically and unstripped (`designer` 191 MiB);
  the build runs eight of them. `strip --strip-all` takes `tools/` and `bin/` from 1,652 MiB to 418 MiB.
- **Archives no build links**: shaderc, glslang and SPIRV-Tools (the build runs their programs, never `find_package`s
  them), Qt Designer (the qttools port's `linguist` feature drags it in), six OSG libraries and 13 serializer plugins
  OSG builds unconditionally. Kept: `libharfbuzz-subset.a`, which `harfbuzzConfig.cmake` declares and Qt finds harfbuzz
  through; `libQt6WaylandCompositor.a`; archives their ports build with no switch.

Rebuilt with the qttools, FFmpeg and OSG overlays, then pruned and stripped (`prune_set`): **1,492 MiB unpacked and 339
MiB packed**. `libavcodec.a` 220 → 42 MiB at `-g1` without encoders.

## Windows: 8,291 MiB unpacked, 1,730 MiB packed

vcpkg's Windows toolchain hard-codes `/Z7` for release, so every object carries full CodeView, every type it uses
repeated in each object: the same ports are 4.9 times their Linux size. Pruned as on Linux, **5,548 MiB unpacked and
1,217 MiB packed**, still two parts at 1 GiB a part, 4.8 GiB of it archives with `/Z7`.

MSVC has no line-tables-only mode: `/Zd` was one, `cl.exe` from 2017 on refuses it, and clang-cl dropped its alias
([D114632](https://reviews.llvm.org/D114632)); every `/Z7`, `/Zi` and `/ZI` format carries types. A Windows set with
line tables only needs clang-cl with `-gline-tables-only`, as Chromium builds.

## The set's own flags

`vcpkg/flags.cmake` (x86-64-v2, per-function sections, the stack protector, no semantic interposition; MSVC's
`/arch:SSE4.2 /Gw /Zc:inline`) grew the Linux set 13 MiB and shrank the Windows set 135 MiB; which flag moved each was
not measured apart.
