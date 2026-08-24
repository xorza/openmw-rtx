# Implementation plan

An experimental ray-traced renderer inside this OpenMW fork. The posture and the priority order live
in `CLAUDE.md`; the host engine's structure and the seams named below are in `.notes/rtx/openmw.md`.
This file is the route.

**Decisions and what is left, not a changelog.** §2–§5 are the choices everything else rests on; §6
is the route, one or two lines a milestone, marked; §7 is the tooling; §8 is what is not done. Code
comments cite this file by section number, so the numbering does not move.

---

## 1. What is being built

OpenMW keeps the game: cells, references, physics, scripts, animation, weather, the GUI. It stops
owning the picture. Primary visibility, shadows, direct and indirect light, sky, water, fog and
tonemapping are a Vulkan ray-tracing renderer, upscaled by DLSS Ray Reconstruction.

The renderer is **optional at compile time** (`-DOPENMW_RTX=ON`) and **off by default at runtime**
(`[RTX] enabled = false`). With the option off, not one line of it is compiled and the rest of the
build is byte-identical.

## 2. Where the scene comes from — the decision

Three ways to feed a ray tracer from this codebase.

| | what it means | verdict |
|---|---|---|
| **A. Read the ESM/NIF directly** | New cell streaming, animation, LOD, paging — rtxmw's route | **No.** That work is the reason to fork OpenMW instead. |
| **B. Mirror the OSG scene graph** | Keep OpenMW's whole content pipeline; traverse the live graph each frame and maintain a Vulkan mirror of it | **Yes.** |
| **C. Replace OSG's draw backend** | A Vulkan `osg::GraphicsContext` implementation | No. OSG's drawing model is fixed-function-shaped state assignment; ray tracing is not a draw call. |

**B, because of §3.3 and §3.5 of `.notes/rtx/openmw.md`**: the world is already CPU-resident triangles
with world transforms and resolved texture roles. The mirror is a traversal and a diff, not a content
pipeline.

Consequences, as they settled:

- **The mirror does not run downstream of a cull, and there is no cull.** Rays go everywhere, so a
  frustum has nothing to say about what must be reachable. The three things OpenMW hangs off its cull
  traversal are the renderer's own: rigs are posed by a visitor that culls nothing, and terrain LOD
  and object paging come from `Terrain::World::collect` asked by distance (`TerrainResidency`).
  Inheriting them was the defect — an actor outside the frustum mirrored in a stale pose, distant
  ground absent from reflections.
- **The mirror is incremental.** A full rebuild per frame will not hold a frame budget: instance
  transforms change every frame, geometry rarely, materials almost never. `.notes/rtx/mirror.md` is
  the measurement and the shape that replaced it.
- **Draw-order tricks do not come across.** `RenderBin` ordering, the transparent-pass hack, the
  distortion pass, `pingpongcull` are rasterizer workarounds; the RT path answers the same questions
  with rays.

## 3. How the image reaches the screen

Two surfaces, one renderer, and no OpenGL under either.

**Headless (primary development surface).** A Vulkan device with **no surface extensions**, no
window, no GL. `openmw-rtxtool` loads game data, builds a scene, renders N frames, writes a PNG. It
works over ssh and starts in under a second warm, and it is where nearly all iteration happens.

**In-game: two paths, chosen at startup, and the one not chosen is never started.**
`-DOPENMW_RTX=ON` decides whether the ray tracer is built; `[RTX] enabled` decides whether it runs,
read once before the window exists. With it off the tree behaves exactly as upstream does. With it on
**OpenGL is not initialized at all** — no GL context, no `osgViewer` graphics window, no interop, no
rasterized frame underneath. The window is an SDL surface for Vulkan, the GUI is drawn by the
backend, and the inventory doll and the maps are traces rather than render-to-texture passes.

**Interop was the way in and is gone.** Vulkan rendered offscreen into an image exported with
`VK_KHR_external_memory_fd`, GL imported it and blitted it under the MyGUI overlay. That put a traced
frame on the screen at M2 while everything around it was simple. What it cost was a whole rasterized
world drawn and discarded every frame and a frame of latency (§12), and both went with it.

What the second path needed, and what each became: MyGUI a Vulkan render manager
(`components/myguirtx`); the doll and the maps traces into the renderer's own GUI texture table
(`MWRender::Rtx::TracedView`); video playback the same FFmpeg decode into a texture the backend
uploads (`MyGUIPlatform::Picture`); the window and swapchain already written since M0. **OSG stays as
a scene graph and a content loader** — the harness has proved since M0 that it needs no GL context to
be either.

## 4. Layout

```
components/rtx/                 openmw-rtx          the API-neutral core: no OSG, no game headers
    shaders/    GLSL + headers shared verbatim with C++ (`#ifdef __cplusplus`)
components/rtxvulkan/           openmw-rtx-vulkan   the Vulkan backend, and its GLSL
components/rtxmetal/            openmw-rtx-metal    the Metal backend (`.notes/rtx/backends.md`)
components/rtxbackends/         openmw-rtx-backends which backend a build has
components/rtxbridge/           openmw-rtx-bridge   osg::Node -> SceneDesc, with change tracking
components/myguirtx/            MyGUI's backend for it
components/surface/             what the content says a surface is; both renderers read it
apps/openmw/mwrender/rtx/       the game-side owner
apps/rtxtool/                   openmw-rtxtool      headless harness
files/rtx/views.cfg             named viewpoints; benches.cfg names the suites
```

C++ sources sit flat in each directory, as every other component in this tree does; only the GLSL
gets a subdirectory, matching `files/shaders`.

The seam that matters: **`openmw-rtx` never includes an OSG scene-graph header and never includes a
game header**, and never a backend's. It is testable against synthetic scenes with no game data
present, which is what makes the allocation test and the pass tests fast and hermetic.
`openmw-rtx-bridge` is the only place that knows both worlds.

Every shader is compiled by `glslc` at build time and **validated by `spirv-val` in the same custom
command** — an invalid module fails the build, not the frame. Debug builds compile with `-g` so
Nsight shows source.

## 5. Target hardware

Ada-class NVIDIA, and Apple silicon for the other backend. Confirmed available here:
`VK_KHR_ray_tracing_pipeline`, `VK_KHR_acceleration_structure`, `VK_KHR_ray_query`,
`VK_KHR_ray_tracing_position_fetch`, `VK_KHR_ray_tracing_maintenance1`, `VK_EXT_opacity_micromap`,
`VK_EXT/NV_ray_tracing_invocation_reorder`, `VK_NV_cluster_acceleration_structure`,
`VK_NV_partitioned_acceleration_structure`, `VK_EXT_mesh_shader`, `VK_KHR_cooperative_matrix`.

Required at startup; a missing one is a hard failure naming the extension, never a fallback path.

**§5.3 — the target this is all written against: 1920×1080 internal → 3840×2160 at 60 fps.**

## 6. Milestones

**M0–M11 are done. M12 is continuous.** What each settled that still binds is beside it; what any of
them left behind is in §8.

- **M0–M3 — device, mirror, visibility, textures. Done.** `SceneDesc` and a bridge that walks an
  `osg::Node` into meshes, instances, materials and lights; BLAS per mesh, TLAS per frame, a
  ray-query compute pass; one bindless texture array with DDS blocks passed through untouched.
  *Binds:* **ray-cone texture LOD went in from the start** — retrofitting it is how rtxmw lost a week
  of caustics (`design.md` §7.6).
- **M2b — terrain in the harness. Done.** An `ESMTerrain::Storage` over `EsmData::mLands` feeds a
  `Terrain::TerrainGrid`, one chunk grid per cell, no LOD — Seyda Neen traced at 5.5% before and 79%
  after, **and the renderer did not change at all**. *Binds:* a cell's blend map indexes the texture
  list of *the plugin that wrote it*, so the key is `(plugin, index)`; and `TerrainDrawable`'s
  one-pass-per-ground-texture stack is read back into layers and summed at the hit.
- **M4 — direct lighting and shadows. Done.** *Binds:* water is excluded from shadow rays **by a mask
  bit rather than a cutout test** — the any-hit version halves the frame rate; and everything about a
  lamp is derived, because a `LIGH` record carries a colour and a radius and no intensity.
- **M5 — sky, sun, moons. Sun, moons and sky light done; the rest of the dome and the weather in it
  are not** (§8). Driven by `MWWorld::Weather` and `DateTimeManager` rather than re-derived from the
  ini. *Binds:* **sky is a light source, not a backdrop**, and the sun and the moons are discs a ray
  that hit nothing finds — so anything reflective gets them for nothing and their size lives in one
  place. And **the sun is one object, not the engine's five dials**: `Sky::sunAt` reads Morrowind's
  arithmetic, `RtxBridge::makeSkylight` is the only thing that may build a sun out of it, and its
  irradiance being zero is the whole of "there is no sun". Every sun bug this renderer has had was
  two of those dials disagreeing.
- **M6 — water. Done.** TMA spectrum under Donelan–Banner spread, 32
  components, shortest wave 32 units; ripples carried on the swell; Schlick Fresnel with one
  reflection and one refraction ray **at the pixel's own cone spread, not the bounce spread**;
  Beer–Lambert with Jerlov coastal extinction; caustics from `1/|det J|` with the second-order
  correction that took the term from handing back 12.3% more light than fell on the water to 2.4%.
  *Binds:* **the shader is more current than any prose about it**, here and in
  `/home/xxorza/Projects/rtxmw/docs/design.md` §7; every expectation derives from `WATER_EXTINCTION`,
  `WATER_IOR` and `WATER_F0` rather than a literal beside it. And the surf is McCowan's breaking
  criterion against the depth *at this instant* — `H = 0.78 d`, so the band's width falls out of the
  sea state and nothing places it. **The note that said to take it off the sign of `det J` was
  wrong**: that Jacobian is the refracted bundle's, it scales with depth, and this surface is not
  displaced — so it is near the identity in exactly the shallows foam belongs in, and where it does
  invert it marks a caustic cusp under the water rather than a surface that folded.
- **M7 — fog. Done**, bar a test of a real view (§8). 24 non-uniform steps, density falling off from
  the cell's water level, three fbm octaves with a coverage band normalised by its own mean, HG-Draine
  at eight-micron droplets per steradian, eight shadow rays for shafts. *Binds:* **no new bindings** —
  folding fog into both halves inside the trace equals fogging their sum; and extinction comes from
  `MWRender::FogManager`'s own ramp read back rather than an eyeballed figure.
- **M8 — indirect light. Done.** Path-traced diffuse bounce, sky as an emitter, blue-noise sampling,
  à-trous denoise demodulated by albedo. Water and fog bypass the filter by construction.
- **M9 — de-lighting. Done.** An estimate divided out at sample time, judged on a contact sheet.
- **M10 — G-buffer and DLSS Ray Reconstruction. Done.** 1920×1080 internal → 3840×2160.
- **M11 — full in-game integration. Done.** Cells, objects, actors, camera, menus, screenshots, the
  local and global maps. *Binds:* **removal is mark and sweep over slots** — the mirror re-walks the
  whole graph every frame, so anything alive was met, and the identity maps are keyed on raw `osg`
  pointers an unloading cell can hand straight back for something else.
- **M12 — performance. Continuous** (§8).

### Where M12 stands

`openmw-rtxtool bench`, the `[default]` suite: 1920×1080 out of 1280×720 at DLSS quality, layers off,
600 frames of world at 60 Hz after a warm-up second, median of each row.

| place | frame | trace submit | place submit | fps |
|---|---|---|---|---|
| Seyda Neen's ship, 7,013 instances | 8.67 ms | 4.87 | 1.87 | 115.3 (77.9 at the 1% low) |
| Balmora's guild of mages, 1,239 | 6.79 ms | 5.00 | 1.14 | 147.4 (105.0) |

| place | trace | upscale | refit | tlas | **GPU total** |
|---|---|---|---|---|---|
| Seyda Neen's ship | 2.30 | 2.00 | 0.43 | 0.20 | **5.03 ms** |
| Balmora's guild | 2.21 | 2.07 | 0.27 | 0.17 | **4.81 ms** |

**The CPU and the device have converged.** Five milliseconds of device work inside an 8.67 ms frame,
where it used to be 8.74 inside 30. Both places are inside the 16.7 ms budget at 1080p, and the
remaining exterior cost is split evenly between the trace and the upscaler with nothing obviously
wasteful between them.

### What the finished features cost

Written down as each one lands rather than acted on — `CLAUDE.md`, *Feature-complete first, then
fast*. 1920×1080, no upscaling, validation off, best of thirty on this box.

| what | where | cost |
|---|---|---|
| shoreline foam | a shoreline in the middle distance | 8.14 → 8.25 ms |
| | the camera standing in the surf, foam over a third of the frame | 8.47 → 10.32 ms |
| the sprite layer, in the trace | Seyda Neen's ship, 165 emitters and 4,655 particles | 8.22 → 8.85 ms |
| | Balmora's guild of mages, 19 emitters | 7.34 → 7.47 ms |
| the harness's live props, whole frame | Balmora in a window, 94 props | 49 → 37 fps |
| a cell arriving | nineteen crossings across the island | none of them a rebuild (§10) |
| the GUI's own submit | 12,400 interface frames with a video playing | 0.38 queue round trips a frame |

Foam's near-field figure is a shadow ray per covered pixel — it is lit the way every other diffuse
surface is — and the reflection and refraction rays under it are still traced and then mixed away
where it covers. Removing those would want the depth before the refraction that measures it, which
is the circle to break if it ever matters.

The trace is the cheap half of the sprites and the sphere test is why: an emitter is a few units
across and one rejection throws it away for almost every pixel. The window's six milliseconds are not
the sprites — they are the 185 extra deforming drawables the instanced props bring, refit once a
frame whether or not their skeletons moved (§8).

## 7. Development infrastructure

This is not overhead; it is what decides how fast the milestones go. The verbs are in `CLAUDE.md`.

**7.1 The harness is the primary surface.** `shot` renders the real renderer headlessly in about a
second and prints hit fraction, camera and frame time, so a change is checkable without a screenshot
being looked at. `view` exists for what only a window shows: how something moves, and whether an
artefact is a still or a shimmer. `bench` counts seconds of **world**, not of wall clock — the world
steps a sixtieth a frame however long that frame took, so two builds render the same six hundred
frames with the same particles in the same places. The game is benched too, over savegames
(`apps/rtxtool/bench.sh`), because every renderer defect found in the M12 stretch was invisible in the
harness and obvious within seconds of measuring the game.

**7.2 Tests**, in the order they catch things: unit tests against synthetic scenes with no game data;
renderer tests driving the real renderer headlessly and asserting radiance; and `verify`, an A/B of
every view against **a directory this machine wrote earlier**, never a corpus in the tree — the
picture is a function of the driver and the card as much as of the code. `contactsheet` is for the
cases where the right answer is a judgement. Game data absent, the tests that need it **skip**;
present but wrong, they **fail** — a silent skip looks like a pass. Every test loads the validation
layers, synchronization validation included, and fails on any recorded error.

**7.3 Zero allocations per frame.** A steady-state frame with a stationary camera must not touch the
heap; the concern is jitter, not throughput. A counting global `operator new`/`delete` in one
translation unit of the test binary renders N frames of the real renderer and asserts a counter
across the measured window, against a named budget rather than `== 0` so a driver path that must
allocate can be accommodated deliberately. It measures zero. Forbidden on the frame path:
`std::string` construction, `push_back` into an unreserved vector, `std::function` capture,
`make_unique`, and any logging that is not compiled out.

**7.4 Validation and debugging.** Layers on in debug and in every test, error severity aborting after
logging with object names. Synchronization, GPU-assisted and best-practices validation opt in per
run. **Every Vulkan object is named** and every pass labelled with `VK_EXT_debug_utils`, compiled out
in release. `VK_EXT_device_fault` on device loss.

**7.5 Profiling.** Per-pass timestamp queries report the device's side; `apps/rtxtool/profile.sh`
wraps `perf` for the CPU's. Two things that would be re-learned otherwise: `task-clock` rather than
`cycles`, because this box's hybrid PMU splits `cycles` across `cpu_core` and `cpu_atom` and halves
every count; and a control fifo bounding the recording to the measured frames.

## 8. What is not done

**Content the renderer does not draw yet**

- **The sky dome** (M5): clouds and stars. The sky is a horizon-to-zenith gradient, a sun disc and
  the two moons — phased on the game's own three-day clock, lit by McEwen's lunar-Lambert, and able
  to eclipse the sun. Nothing else is in it.
- **Weather effects** (M5's other half): rain, snow, ash and blight storms, blizzards, and a
  thunderstorm's lightning. `WorldState` carries `mWeatherId`, `mWeatherTransition` and `mWindSpeed`
  and nothing on this side reads one of them — what reaches the picture is only what `MWWorld::Weather`
  had already folded into the sun, the ambient and the fog band. Four of the ten weathers hang a NIF
  particle system off the sky and rain builds its own in `SkyManager::createRain`, all of it under a
  node the trace never sees. **Three answers, not one.** The particles are the sprite layer's case and
  it was written for them — `shaders/scene.h` names a rain streak as the reason that layer is
  composited rather than denoised — except that `GpuSprite` is an eye-facing disc where rain is
  `FIXED`-aligned along `(0,0,-1)`, so the layer wants an oriented sprite before it wants anything
  else. A storm is mostly a **medium**: ash and blight are a change of extinction and phase function
  in M7's fog, with sprites as the near-field detail on top, and treating them as a curtain of quads
  is how one ends up a grey wall. And **lightning is a light** — in the light table for the frames it
  lasts, throwing shadows and reflecting, not `mFlashBrightness` added to the screen. Where it lands
  is M6's: `Weather_Rain_Ripples` gates `Water::emitRipple`, and ripples already ride the swell.

**Performance — M12, and none of it is started**

- **SER** (`VK_EXT/NV_ray_tracing_invocation_reorder`).
- **Opacity micromaps for cutout foliage.** The device features are required and probed; nothing
  builds a micromap.
- **BLAS compaction**, and **cluster acceleration structures** if they earn their place.
- **Refit only what moved.** A prop's emitters change every frame and its geometry usually does not,
  and nothing tells the two apart — 185 deforming drawables refit every frame in the harness's props
  view.
- **The GUI's second submit.** `GuiTextures` flushes just before `drawGui` rather than inside it, so
  a frame that writes a texture costs two queue round trips where it could cost one. Measured at 0.38
  a frame, which is not what a frame is spending its time on.

**Tooling**

- **A test of a real fogged view** (M7). The banks over a shore and the shafts themselves are looked
  at rather than asserted; there is no histogram test.

**Open questions**

- **Interiors.** A room is not a valley (`design.md` §8.42) and interiors are half the game. Whether
  fog, sky light and bounce need a separate interior model is unanswered.
- **Groundcover.** Grass is alpha-cutout and enormous in instance count. Particles were the other
  half of this question and are answered: a sprite goes in a table beside the lights and is marched
  against the primary ray, never into an acceleration structure.
- **Distant land.** OpenMW's object paging and terrain LOD were tuned for a rasterizer's silhouette
  budget, not a BVH's, and the renderer now asks for them by distance rather than inheriting them.
  What distance is right is unmeasured — **and it cannot be measured in the harness yet**, which
  pages terrain and nothing else: the game's `ObjectPaging` and its groundcover are not placed there,
  so distant land is a different scene in the two.

- **The harness's people are a posed row, not the game's.** `PosedActors` stands creatures and NPCs
  in front of the camera and steps their animation by a clock, which exercises skinning, rigs and the
  deforming path and does not exercise what the mechanics actually ask for — an idle chosen by AI, a
  weapon drawn mid-swing, someone turning to face the player. A pose defect only the game produces
  cannot be reproduced under `shot`. Probably never fully closed: what an NPC is doing is a mechanics
  answer, and the harness has no mechanics.

**Standing risk**

- **Vanilla content assumes a rasterizer** — sheets lit from both sides, discarded outer transforms,
  Z-first Euler angles, two-sided stencil (`design.md` §8.1–8.6). Every one is diagnosed next door.
  Read it before debugging anything that looks like a content bug.

## 9.

Risks and open questions were here and are in §8, where what is left is. The number stays empty
because §10 and §12 are cited from code.

## 10. Slots, not compaction

**Done, and it is the rule the tables are built on.** `SceneDesc` used to reclaim by compacting, and
that is why a cell boundary cost a full `setScene` — nineteen of nineteen crossings on a route across
Vvardenfell, because a departing cell almost always takes a mesh or a material with it and
renumbering invalidates every acceleration structure and the whole texture array.

**Nothing is renumbered while the world is being walked.** A mesh, material or texture that nothing
names is freed — its index goes on a free list and the entry stays where it is — and the next arrival
that fits takes the slot, counting as an arrival because it holds different content than it did.
Travel is the exception and the one the player already pays for: `clear()` replaces the scene.

**A slot and the room behind it are two different things.** A slot is one row of a table and every row
is the same size; what varies is the run of vertices, indices, layers or mask weights it points at,
and that is handed out by `Rtx::SpanAllocator` — best fit, because the free list is what a departing
ring left and first fit spends a cathedral's hole on a crate, with freed runs merged so a cell's
thousands of releases become the one hole it arrived as. On the device the same run lives in
`BlockedBuffer`: blocks allocated once and never moved, so a bottom-level structure's address stays
valid and a crossing appends instead of rebuilding.

Flying the Bitter Coast to the Ashlands, nineteen boundaries in ten seconds of world:

| | compacting | slots |
|---|---|---|
| crossings that rebuilt everything | 19 of 19 | **0 of 19** |
| worst frame | 2,808 ms | **547 ms** |
| crossings, total | 39.7 s | **7.3 s** |
| — of that, building | 28.5 s | **0.9 s** |

**One bug came with it and is worth remembering.** `VulkanRenderer` decided whether to rebuild by
comparing the mesh table's *size*, which cannot see a freed slot taken over by something else. A
skinned body landing in one was refitted into a structure never made for it — reached after seventeen
crossings. The tables carry revisions that count arrivals rather than entries.

**Ownership is counted, not swept.** A texture is freed when the last material or hold naming it lets
go, wherever that happens; the sweep only ever answered on the frames a mesh or a material died as
well.

## 11. The three roots — closed

`.notes/ISSUES.md` once listed five defects that traced back to three roots, and all three are shut.
**A**, variable-length runs with no allocator, is `SpanAllocator` for the host runs and `BlockedBuffer`
for the device geometry. **B**, the mirror reading outside cull where `SceneUtil::StateSetUpdater`
only writes during one, is `SceneExtractor::animate` running the animators itself against a state set
it keeps per node, with `Material::mTextureTransform` for the UV controllers that had nowhere to land.
**C**, arrivals reported as a count rather than a set, is a slot carried on every arrival — for
textures and for meshes both.

## 12. The seam with the rasterizer — closed

There was one, and code comments still point here for what it was. The rasterizer drew the whole
world every frame into a framebuffer nobody looked at; `Tracer`'s composite went over the top inside
the same traversal, so the image presented was the *previous* frame's trace. It cost 2.05 ms a frame
of drawing at Seyda Neen against 6.2 ms of tracing, plus a frame of latency — and, worse than either,
it made skinning, terrain LOD and object paging view-dependent decisions inherited from a cull.

**What closed it was owning those three** (§2) and then not needing the traversal at all. There is no
cull, no interop and no rasterized frame underneath: the trace runs inside the frame that presents it.
The harness was the existence proof throughout — it has no cull, poses its actors by hand, and renders
the same content correctly.
