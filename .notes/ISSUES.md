# Open issues

Defects the reviews of 2026-10-02 found or rewrote, the parity review against the rasterizer
included. `.notes/REVIEW.md` holds each one's evidence under its title, beside the defects of the
first review.

- An alpha-blended surface whose material alpha is one and whose texture reaches solid is cut at
  alpha 0.5, where the rasterizer blends its soft texels by the texture's alpha. Every DXT3 leaf,
  banner, rope and sail has a hard edge where the rasterizer's is soft, and a cobweb, a Telvanni
  crystal or Bloodmoon ice whose texture reaches 255 anywhere loses the coverage under the cut.
  `components/rtx/scene/material.hpp` (`Material::isTranslucent`).
- The crosshair, activation and Lua's `castRenderingRay` meet skinned actors in their bind pose:
  `RigGeometry` poses its CPU copy only in a cull, and the ray tracer never culls the world. A corpse
  on the floor is hard to focus or loot. `components/sceneutil/riggeometry.cpp:137-157`,
  `apps/openmw/mwrender/rtx/rtxrenderer.cpp:404-440`.
- A spell-cast glow never ends: `GlowUpdater` runs on the node and then on the mirror's state set,
  and its end on the first run leaves the mirror's copy with the last sheet. Open, Lock, a trapped
  container and Telekinesis leave a lasting glow. `components/rtx/mirror/materialresolver.cpp:121-148`,
  `components/sceneutil/util.cpp:100-143`.
- The traced sky fades from fog colour to sky colour linearly in the sine of the elevation, where
  `sky_atmosphere.nif` fades between 3.6° and 28.6° and is all sky colour above. The sky is too near
  the fog colour in every exterior frame, and the ambient, fog and deck light read a wrong mean.
  `components/rtx/shaders/sky.h:319-330`, `components/rtx/environment/skylight.cpp:97-109`.
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
- Under the vertex-colour tint the material's alpha still sets the opacity, where the rasterizer
  reads the vertex alpha: Dunmer candles and lanterns draw at 40% of their coverage.
  `components/rtx/scene/surface.cpp:115`, `components/rtxvulkan/scene/scenebuffers.cpp:68`.
- An untextured surface reads the grey stand-in meant for a missing texture, so it draws at half its
  material colour (part of the guar mesh). `components/rtxvulkan/scene/scenebuffers.cpp:56`.
- A glow map on UV set 1 is read through set 0, so the draugrs' eye glow lands off their eyes; the
  mesh reader tells UV sets apart by array address, which the loader makes distinct per unit.
  `components/rtxvulkan/shaders/lib/traversal.glsl:1058-1059`, `components/rtx/mirror/meshreader.cpp:330-352`.
- The additive and medium walks read the diffuse map alone: spell shields and area effects lose their
  dark maps and environment sheets, and the rockslide draws as a flat grey sheet.
  `components/rtxvulkan/shaders/lib/medium.glsl:75-90`.
- Negative lights are refused, so the 334 darkening lamps vanilla places in 147 interiors (Telvanni
  towers, the Gateway Inn) are left out and those rooms come out lit.
  `components/rtx/scene/lightbuilder.cpp:173-177`.
- Flickering and pulsing lamps average their whole colour and swing about 2:1, where the game's
  average 0.625 of it and swing 4:1, so 421 of 574 vanilla light records burn 1.6 times too bright
  against steady lamps. `components/rtx/scene/lightbuilder.cpp:208-232`.
- Night-Eye's lift goes into the ambient, which geometry occludes and the exposure meter adapts to, so
  a cave lifted 133 times shows about 3.4 times brighter. `components/rtx/environment/skylight.cpp:210`.
- A magic bolt in flight carries a light sized by the spell's area: a 20 ft fireball flies with a lamp
  reaching 981 units, about 42 times brighter than the game's, and the impact's glow lights the area
  again. `apps/openmw/mwworld/projectilemanager.cpp:157-168`.
- Groundcover is never drawn under the ray tracer; only a log line says so.
  `apps/openmw/mwrender/rtx/rtxrenderer.cpp:214-219`.
- Textures in RGB8, L8, LA8, BC4, BC6H, BC7 and other formats outside the list draw as the grey
  stand-in, and a sky deck in one is left out. `components/rtx/image/texels.cpp:145-204`.
- A Lua static camera removes the player's body, torch, Light-spell glow and shadow from the traced
  world: `Mode::Static` is read as the harness's parked camera. `apps/openmw/mwrender/framedescriber.cpp:127`.
- Lua's `camera.setViewDistance` changes nothing in the traced picture, and `getViewDistance`
  reports it back. `apps/openmw/mwrender/sceneframe.hpp:176`.
- Sprites ignore an actor's fade (an invisible actor's torch flame burns whole), and a node's own
  `alpha` under a faded actor keeps the actor's alpha too. `components/rtx/mirror/sceneextractor.cpp:711-716`,
  `components/rtx/mirror/shading.cpp:22-49`.
- After a teleport, a fast travel or a load, local map tiles are traced before the ring stands their
  ground, and nothing asks for them again; the world map keeps the hole.
  `apps/openmw/mwrender/rtx/viewqueue.cpp:32-58`, `components/rtx/mirror/cells/cellring.cpp:205-217`.
- In an ash or blight storm the cloud deck and the sea turn about the world origin by a bearing that
  follows the player, so they slide as the player walks, and the sea swings when the weather flips.
  `components/rtxvulkan/shaders/lib/sky.glsl:40-50`, `components/rtxvulkan/shaders/lib/sea.glsl:100-108`.
- Masser or Secunda can change phase in mid-sky: the trace drops the fade the engine hides the change
  behind, and steps the phase by whole eighths. `components/rtx/environment/moonbuilder.cpp:212`.
- A Lua `weather.cloudTexture` change never reaches the trace, which reads the sheets once from the
  fallbacks. `components/rtx/environment/skybuilder.cpp:76-121`.
- The sun glare is up to four times the rasterizer's at sunrise and sunset: the disc's alpha is left
  out of its strength. `apps/openmw/mwrender/rtx/skyreader.cpp:245-253`.
- `tsky` outdoors changes the trace's lighting: the sky's light, the moons, the deck's shadow and the
  fog colour go with the dome. `apps/openmw/mwrender/rtx/skyreader.cpp:130-142`.
- A local map tile is lit by the world's lamps and shadowed by its sun, where the rasterizer's map
  has neither. `apps/openmw/mwrender/rtx/classmasks.cpp:7-22`.
- Effects on the first-person model are traced through the world's eye, at the wrong field of view
  and behind near walls, and a non-additive shell casts a shadow.
  `components/rtx/mirror/sceneextractor.cpp:318-322`.
- `tcb` and `tcg` draw collision triangles as solid white faces, where the rasterizer draws wireframe.
  `apps/openmw/mwrender/rtx/debugwalk.cpp:129-141`.
- Particles integrate at most 0.2 s a frame, so under a simulation-time scale they fall behind the
  world. `components/rtx/mirror/sceneextractor.cpp:390-405`.
- A node with `distortion` extra data is traced as an ordinary textured surface, where the rasterizer
  draws it only into its distortion buffer. `apps/openmw/mwrender/distortion.cpp:11-38`.
- With `distant land cells = 0` the reach takes `viewing distance` unclamped, past the documented
  ten-cell bound and the trace's 200 000-unit far plane. `components/rtx/mirror/cells/cellgrid.cpp:38-44`.
