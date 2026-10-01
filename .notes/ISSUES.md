# Open issues

Defects the review of 2026-10-02 found or rewrote. `.notes/REVIEW.md` holds each one's evidence under
the same title, beside the defects of the first review.

- `--day` writes the calendar's day of the month (the `day` global, clamped to at least 1), and the
  moons read days passed (`TimeStamp(mGameHour, mDaysPassed)`), so no value of `--day` moves a moon.
  Every staged stop stands on 1 Last Seed, and a window's note prints days passed back as `--day=N`.
  `apps/rtxtool/stager.cpp:110-111`, `apps/openmw/mwworld/datetimemanager.cpp:79-82`,
  `apps/openmw/mwworld/weather.cpp:336-342`.
- Under the ray tracer, `capture` reads the presented target with the interface blended in, so every
  save thumbnail shows the HUD or the save dialog. It maps the whole frame onto 518×266 by nearest
  sample, about 9.5% too wide at 16:9. The seam promises "the frame without the GUI".
  `apps/openmw/mwrender/rtx/rtxrenderer.cpp:542-571`, `apps/openmw/mwrender/renderer.hpp:370-372`.
- Indoors, and where the supply has no reader, `CellRing::getCellsToStand` keeps the last exterior
  ask's shortfall: `mBandCells` is written only in `ask`, and `walkRings` returns before it. An
  interior stop after a moving exterior stop never stands whole, and the measurer fails it.
  `components/rtx/mirror/cells/cellring.cpp:95-101`, `:319-333`.
- A reach that grows while the eye stands still asks for no new cell: `WorldMirror::setReach` sets
  no `mAskStale`. The distance stays short until the player moves, and `getCellsToStand` says nought.
  `components/rtx/mirror/cells/cellring.cpp:150-156`, `apps/openmw/mwrender/rtx/worldmirror.cpp:302`.
- An arrival that partly fits the holes of old structure blocks makes a new block the size of the
  whole arrival: `BottomLevelStore::build` passes the full `wanted` to every `take`. The empty part
  counts against video memory and the texture ceiling.
  `components/rtxvulkan/scene/bottomlevelstore.cpp:98-101`, `:224`, `:264-265`.
- The display chain hears a lost past two ways. `setScene` and `createTargets` do not set
  `mExposureStale`, so a first frame with a held or fixed exposure spends the reset. Every `resize`
  and upscale-mode change resets the measured exposure and the glare share from nothing, against the
  comment at `components/rtxvulkan/vulkanrenderer.cpp:111-113`.
- The medium and additive walks ignore the camera's class mask, so a local map tile draws an actor's
  additive spell sheet with no actor under it, and every map pixel pays three traversals.
  `components/rtxvulkan/shaders/lib/medium.glsl:165-252`, `components/rtx/scene/instancerecord.cpp:84-85`.
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
- A measured run reads the player's `object paging min size`, `viewing distance`, `specular map
  layout` and the `[Shaders]` auto normal and specular map switches, so two machines bench different
  scenes under one command line. `apps/rtxtool/main.cpp:310-315`, `apps/openmw/mwrender/renderer.cpp:46-53`.
- The harness's float options accept `nan` and `inf` through Boost; `--fps=inf` makes the step 0.
  Stated ranges (`--delight`) are not enforced, and `--size=1920x1080abc` reads as 1920x1080.
  `apps/rtxtool/options.cpp`, `apps/rtxtool/main.cpp:101-115`.
- `--distant-cells` skips the game's 0–10 clamp, so a bench can measure a reach the game never builds,
  and `--fov` reaches the film's pacing without the game's clamp. `apps/rtxtool/main.cpp:307`, `:1119`.
- The card watch reads NVML device 0 or the first AMD card in sysfs, not the device the renderer
  chose: on a Ryzen APU with a Radeon card the clock line describes the idle integrated GPU.
  `apps/rtxtool/instruments/nvml.cpp:74-82`, `apps/rtxtool/instruments/amdgpu.cpp:53-80`.
- `toNumeric<float>` reads by `from_chars` on GCC and MSVC and by a stream on clang, and the two
  disagree on `"+1.5"` and `" 1.5"`: a setting reads differently on macOS.
  `components/misc/strings/conversion.hpp:63-97`.
- The traced debug lines are drawn after the gamma, and the rasterizer's are raised by it.
  `components/rtxvulkan/shaders/display/tone.comp:141-143`, `components/rtx/shaders/line.h:15-16`.
- The seam and `architecture.md` §10 call a time skip a cut, and nothing calls `notifyCut` for one
  (`set gamehour`, a rest), so the histories carry the old light. `apps/openmw/mwrender/renderer.hpp:305-309`.
- Water strikes under `tws` pile up in `mStrikes` on the frame path and land in one step when the
  world comes back. `apps/openmw/mwrender/rtx/rtxrenderer.cpp:342-345`, `:776-777`.
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
- `omw repeat --pairs=0` prints "identical over every one" having compared nothing, and the verdict is
  read off the harness's wording. `tools/omw/repeat.py:54-60`, `:89-106`.
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
