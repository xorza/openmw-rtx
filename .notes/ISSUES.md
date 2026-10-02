# Open issues

Open defects that give a wrong or missing result for an input the tree can produce. Most come from
the reviews of 2026-10-02, and `.notes/REVIEW.md` holds their evidence under the same title.

- A GPU stall of about 1.5 ms hits some frames of every bench place, in master as in the
  `refactor` branch, and lands in whichever pass runs at the time: upscale, shadow, filter, trace or
  accumulate, a different one from run to run. It adds 0.3–0.5 ms to the summed zone means at
  each place of the default suite, and a headless run (`--window=0`) still has about three
  quarters of it. `~/.cache/omw-refactor/merge-bench/` holds the records.
- An alpha-blended surface whose material alpha is one and whose texture reaches solid is cut at
  alpha 0.5, where the rasterizer blends its soft texels by the texture's alpha. Every DXT3 leaf,
  banner, rope and sail has a hard edge where the rasterizer's is soft, and a cobweb, a Telvanni
  crystal or Bloodmoon ice whose texture reaches 255 anywhere loses the coverage under the cut.
  `components/rtx/scene/material.hpp` (`Material::isTranslucent`).
- `shadow.h` admits a 32-pixel classification square, where `shadowtiles.comp:206` would shift by 32,
  which is undefined. The width is 24 today. `components/rtx/shaders/shadow.h:111`.
- The driver takes macOS for Linux: `bootstrap` downloads the Linux SDK, `build` asks for a disabled
  preset, and `setup` writes `openmw.cfg` to a folder the game never reads.
  `tools/omw/system.py:10-13`, `:125-137`.
- On Apple, `cmake/Tests.cmake` reads `RUNTIME_OUTPUT_DIRECTORY`, which the top level never sets
  there: every test's working directory is empty and the crash matrix writes to `/crash-matrix`.
  `cmake/Tests.cmake:20`, `:32`.
- The harness's driver cache path goes through `path.string()`, which throws on Windows for a
  character outside the ANSI code page, so the harness fails at start from such a checkout.
  `apps/rtxtool/instruments/drivercache.cpp:78`, `:85`.
- Every install ships the harness's `views.cfg`, `benches.cfg` and `rtx/vfs/` scripts, under a
  comment that says no install carries the harness. `CMakeLists.txt:1143-1149`,
  `apps/rtxtool/CMakeLists.txt:91-98`.
- Night-Eye's lift goes into the ambient, which geometry occludes and the exposure meter adapts to, so
  a cave lifted 133 times shows about 3.4 times brighter. `components/rtx/environment/skylight.cpp:210`.
- Groundcover is never drawn under the ray tracer; only a log line says so.
  `apps/openmw/mwrender/rtx/rtxrenderer.cpp:214-219`.
- Textures in BC4, BC6H, BC7 and other formats outside the list draw as the grey
  stand-in, and a sky deck in one is left out. `components/rtx/image/texels.cpp:145-204`.
- The harness starts with the crash catcher off (`OPENMW_DISABLE_CRASH_CATCHER` defaulted to `1` in
  `apps/rtxtool/main.cpp`'s `main`), so a harness run that crashes or hangs writes no report unless
  a shell asks for one. The comment's reason, a dialog waiting for a click, no longer holds: the
  same `main` defaults `OPENMW_CRASH_DIALOG` to `0`.
- The tracer reads `SceneUtil::VertexColorModes::Ambient` as `VertexColour::Tint`, which replaces
  the diffuse colour with the vertex colour. The rasterizer's `getDiffuseColor` keeps the material's
  diffuse under that mode, and only the ambient takes the vertex colour.
  `components/rtx/scene/surface.cpp` `vertexColourOf`.
- The crash matrix's `report-under-hang` mode can hang: on CI's Linux GCC build (run 36963270110) the
  report's dump was written, and the process then stood still until CTest's 300 s timeout killed it,
  without "crash-tests lived on". `apps/components_tests/crashcatcher/crashtestsposix.cpp:46-60`,
  `components/crashcatcher/crashpadclientposix.cpp:25-33`.
- A measured run's host rows move as a whole between runs of one build: at `one-cell-walk` the walk
  median of one build read 0.92, 1.34 and 0.97 ms in three legs back to back, with `update` moving
  beside it, and holding the run to the performance cores (`taskset -c 0-15`) did not settle it.
  Nothing in the report says which state a leg ran in. `apps/rtxtool/instruments/`.
- A fault on one thread while another thread's `Crash::report` or hang report is being written
  leaves two dumps on Linux, and the report's dump is summarised as the fault: Crashpad's Linux client
  keeps one exception record, which `DumpWithoutCrash` and the crash signal handler both write
  (`extern/fetched/crashpad/client/crashpad_client_linux.cc:170-180`), so the report's dump reads
  "Crash: SIGSEGV at 0x10 in thread <the reporting thread>, which crashed". Seen in 8 of 8 runs of a
  thread that faults once `Crash::isReporting()` holds. `components/crashcatcher/crashpadclient.cpp:47-54`.
- Under `tws` the ray tracer still lights the sea and the player from the lamps of the statics and the
  objects it hides. The rasterizer's light manager collects no light from a culled node, so under the
  rasterizer those lamps go dark.
- Under `tws` the ray tracer still draws the cell borders. The rasterizer hangs them under the terrain root,
  whose `Mask_Terrain` `tws` culls, so under it they go with the ground.
- Past the loaded cells, a rendering ray under the ray tracer meets the ground the ring stands and none
  of the ring's statics. The rasterizer meets its paged statics there, with their reference numbers.
- A content file's own clockwise `NiStencilProperty` shows, under the ray tracer, the face the rasterizer
  culls. The material reader cannot tell it from the clockwise front `SceneUtil::attach` states over a
  mirrored body part, whose mirror lives in the skinning and not in the placement.
- `./omw gate` does not build `openmw-rtx-spirv-digest` or `openmw-rtx-spirv-pin` as linked programs in the flavours it builds, so a link error in them passes the gate and fails every CI platform (CI run 37011621948: `Rtx::HashState::add` undefined in `openmw-rtx-spirv-digest`).
