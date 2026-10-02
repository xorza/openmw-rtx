# Open issues

Defects the reviews of 2026-10-02 found or rewrote, the parity review against the rasterizer
included. `.notes/REVIEW.md` holds each one's evidence under its title, beside the defects of the
first review.

- An alpha-blended surface whose material alpha is one and whose texture reaches solid is cut at
  alpha 0.5, where the rasterizer blends its soft texels by the texture's alpha. Every DXT3 leaf,
  banner, rope and sail has a hard edge where the rasterizer's is soft, and a cobweb, a Telvanni
  crystal or Bloodmoon ice whose texture reaches 255 anywhere loses the coverage under the cut.
  `components/rtx/scene/material.hpp` (`Material::isTranslucent`).
- The display chain hears a lost past two ways. `setScene` and `createTargets` do not set
  `mExposureStale`, so a first frame with a held or fixed exposure spends the reset. Every `resize`
  and upscale-mode change resets the measured exposure and the glare share from nothing, against the
  comment at `components/rtxvulkan/vulkanrenderer.cpp:111-113`.
- `normalMapSlopes` reads the normal map's spread at the footprint's long-axis level while the map is
  read anisotropically, so a glossy normal-mapped surface goes 1.3 levels too rough at 80° off the
  normal. `components/rtxvulkan/shaders/lib/texturing.glsl:322-329`.
- On macOS the crash monitor's hang dialog deadlocks: the watch thread's `SDL_ShowMessageBox`
  dispatches to the main queue, which Crashpad's Mach loop never drains. No hang box, no crash box,
  no package, and the monitor never exits. On Linux and Windows an unanswered hang box holds the crash
  report box. `components/crashcatcher/crashpadmonitor.cpp:292-313`, `:482-490`.
- A harness run never sets the hang limit: `runHosted` copies `main.cpp`'s configuration without
  `Crash::setHangLimit` or the version annotation, so a hung harness run is never reported as a hang.
  `apps/rtxtool/hosted.cpp:100-160`, `components/crashcatcher/crashpadmonitor.cpp:361`.
- The card watch reads NVML device 0 or the first AMD card in sysfs, not the device the renderer
  chose: on a Ryzen APU with a Radeon card the clock line describes the idle integrated GPU.
  `apps/rtxtool/instruments/nvml.cpp:74-82`, `apps/rtxtool/instruments/amdgpu.cpp:53-80`.
- The traced debug lines are drawn after the gamma, and the rasterizer's are raised by it.
  `components/rtxvulkan/shaders/display/tone.comp:141-143`, `components/rtx/shaders/line.h:15-16`.
- The seam and `architecture.md` §10 call a time skip a cut, and nothing calls `notifyCut` for one
  (`set gamehour`, a rest), so the histories carry the old light. `apps/openmw/mwrender/renderer.hpp:305-309`.
- `RenderingManager::getFieldOfView` returns the override flag (1°) while the field of view is
  overridden, so in werewolf form Lua's `camera.getFieldOfView()` and `viewportToWorldVector` use 1°.
  `apps/openmw/mwrender/renderingmanager.cpp:1087-1090`.
- The SDL3 port answers "which display" two ways: the gyro orientation query reads the setting's
  display, the event filter the window's. `apps/openmw/mwinput/sensormanager.cpp:46-47`,
  `components/sdlutil/sdlinputwrapper.cpp:194-196`.
- `stress.comp` takes `clockRealtimeEXT` ticks as nanoseconds; on RDNA the clock is 100 MHz, so a
  hold runs ten times as long and its readback is wrong. `components/rtxvulkan/shaders/trace/stress.comp:10-14`.
- `shadow.h` admits a 32-pixel classification square, where `shadowtiles.comp:206` would shift by 32,
  which is undefined. The width is 24 today. `components/rtx/shaders/shadow.h:111`.
- The memory clock in the card line is the highest reading, printed beside the mean core clock as if
  it were the same statistic. `apps/rtxtool/instruments/gpuclock.cpp:74`, `:114`.
- `--against` says nothing about a view the reference drew and this run did not, and exits 0.
  `apps/rtxtool/instruments/framehashes.cpp:345-354`.
- An abandoned run never closes its record, and `shot` then judges pictures in its output folder that
  this run never wrote. `apps/rtxtool/session.cpp:50-56`, `apps/rtxtool/main.cpp:764-769`.
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
- The sun glare is up to four times the rasterizer's at sunrise and sunset: the disc's alpha is left
  out of its strength. `apps/openmw/mwrender/rtx/skyreader.cpp:245-253`.
- `tsky` outdoors changes the trace's lighting: the sky's light, the moons, the deck's shadow and the
  fog colour go with the dome. `apps/openmw/mwrender/rtx/skyreader.cpp:130-142`.
- `tcb` and `tcg` draw collision triangles as solid white faces, where the rasterizer draws wireframe.
  `apps/openmw/mwrender/rtx/debugwalk.cpp:129-141`.
- Particles integrate at most 0.2 s a frame, so under a simulation-time scale they fall behind the
  world. `components/rtx/mirror/sceneextractor.cpp:390-405`.
- A node with `distortion` extra data is traced as an ordinary textured surface, where the rasterizer
  draws it only into its distortion buffer. `apps/openmw/mwrender/distortion.cpp:11-38`.
- With `distant land cells = 0` the reach takes `viewing distance` unclamped, past the documented
  ten-cell bound and the trace's 200 000-unit far plane. `components/rtx/mirror/cells/cellgrid.cpp:38-44`.
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
