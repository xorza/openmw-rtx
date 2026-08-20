# Implementation plan

An experimental ray-traced renderer inside this OpenMW fork. The posture and the priority order live
in `CLAUDE.md`; the host engine's structure and the seams named below are in `openmw.md`. This file
is the route.

Status belongs in commits and in `docs/rtx/design.md` once that exists — **not here**, so this file
stays worth trusting.

---

## 1. What is being built

OpenMW keeps the game: cells, references, physics, scripts, animation, weather, the GUI. It stops
owning the picture. Primary visibility, shadows, direct and indirect light, sky, water, fog and
tonemapping move to a Vulkan ray-tracing renderer, and the result is upscaled by DLSS Ray
Reconstruction.

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

**B, because of §3.3 and §3.5 of `openmw.md`**: by cull time the world is already CPU-resident
triangles with world transforms, resolved texture roles, and — for actors — *already skinned*
positions in `RigGeometry::getGeometry(frame)`. Terrain LOD, object paging and groundcover have all
already made their decisions. The mirror is a traversal and a diff, not a content pipeline.

Consequences to accept:

- The cull traversal still runs (`openmw.md` §3.4). It is cheap and it is what makes skinning and
  LOD happen. The **draw** is what goes away.
- The mirror must be incremental. A full rebuild per frame is the naive version and it will not hold
  a frame budget; instance transforms change every frame, geometry rarely, materials almost never.
- Some things OpenMW hands the graph are draw-order tricks rather than geometry — `RenderBin`
  ordering, the transparent-pass hack, the distortion pass, `pingpongcull`. Those are rasterizer
  workarounds and simply do not come across; the RT path answers the same questions with rays.

## 3. How the image reaches the screen

Two surfaces, one renderer.

**Headless (primary development surface).** A Vulkan device with **no surface extensions**, no
window, no GL. The `openmw-rtxtool` binary loads game data, builds a scene, renders N frames, writes
a PNG. This works over ssh, starts in under a second warm, and is where nearly all iteration happens.
Copied wholesale from rtxmw, which found that opening a window to check a change "costs tens of
seconds of the user's screen and confirms almost nothing the headless path does not".

**In-game: GL/Vulkan interop, not a Vulkan window.** The SDL window stays `SDL_WINDOW_OPENGL`. Vulkan
renders offscreen into an image exported with `VK_KHR_external_memory_fd`, synchronised with
`VK_KHR_external_semaphore_fd`; GL imports both (`GL_EXT_memory_object_fd`, `GL_EXT_semaphore_fd`)
and blits the result under the MyGUI overlay. Verified present on this machine (NVIDIA 610.57.04,
RTX 4090 Laptop).

Why not a native Vulkan window with a Vulkan MyGUI backend: the GUI is not the problem — the
character-preview doll, the local map, the global map and video playback are all OSG render-to-texture
users (`openmw.md` §7), and each would need reimplementing before the game was playable again.
Interop costs one full-screen blit and one semaphore wait per frame and keeps every one of them
working. A native Vulkan window is a legitimate end state; it is not the way in.

## 4. Layout

```
components/rtx/                 openmw-rtx        Vulkan. Knows osg math types, nothing else of OSG.
    shaders/    GLSL + headers shared verbatim with C++ (`#ifdef __cplusplus`)
components/rtxbridge/           openmw-rtx-bridge osg::Node -> SceneDesc, with change tracking.
apps/openmw/mwrender/rtx/       the game-side owner: environment inputs, interop composite
apps/rtxtool/                   openmw-rtxtool    headless harness: cell reports, screenshots,
                                                  benchmarks, golden images
files/rtx/views.cfg             named viewpoints
files/rtx/golden/               reference images
```

C++ sources sit flat in each directory, as every other component in this tree does; only the GLSL
gets a subdirectory, matching `files/shaders`. An earlier draft of this plan grouped them into
`device/`, `scene/` and `passes/`, which would have made every include read `rtx/device/device.hpp`
and put this component at odds with its twenty-five neighbours.

The seam that matters: **`openmw-rtx` never includes an OSG scene-graph header and never includes a
game header.** It is testable against synthetic scenes with no game data present, which is what makes
the allocation test and the pass tests fast and hermetic. `openmw-rtx-bridge` is the only place that
knows both worlds.

Build wiring:

```cmake
option(OPENMW_RTX "Build the experimental ray tracing renderer" OFF)
option(OPENMW_RTX_DLSS "Link NVIDIA DLSS Ray Reconstruction" ${OPENMW_RTX})
set(OPENMW_RTX_NGX_DIR "" CACHE PATH "NVIDIA NGX SDK root")
```

`find_package(Vulkan REQUIRED COMPONENTS glslc)`. Every shader is compiled by `glslc` at build time
and **validated by `spirv-val` in the same custom command** — an invalid module fails the build, not
the frame. Debug builds compile with `-g` so Nsight shows source. `OPENMW_RTX` adds
`target_compile_definitions(openmw-lib PRIVATE OPENMW_RTX)`; every game-side reference to the renderer
sits behind that one macro.

The NGX SDK is a C API — from C++ it is an include and a link, unlike rtxmw's hand-written FFI
(`design.md` §8.17). A copy is at `/home/xxorza/Projects/rtxmw/.refs/dlss`.

## 5. Target hardware

Ada-class NVIDIA, nothing else. Confirmed available here:
`VK_KHR_ray_tracing_pipeline`, `VK_KHR_acceleration_structure`, `VK_KHR_ray_query`,
`VK_KHR_ray_tracing_position_fetch`, `VK_KHR_ray_tracing_maintenance1`, `VK_EXT_opacity_micromap`,
`VK_EXT/NV_ray_tracing_invocation_reorder`, `VK_NV_cluster_acceleration_structure`,
`VK_NV_partitioned_acceleration_structure`, `VK_EXT_mesh_shader`, `VK_KHR_cooperative_matrix`.

Required at startup; a missing one is a hard failure with a named extension, not a fallback path.

## 6. Milestones

Water and fog are early because they are the parts already solved next door and the parts that
change the picture most. Each milestone lands with tests and a `openmw-rtxtool` verb or view that
demonstrates it.

### M0 — Skeleton, device, and the switch

CMake option and targets; shader compile + validate step; instance and device bring-up with the
feature set of §5; `VK_EXT_debug_utils` messenger that **aborts on any error in debug builds**;
`openmw-rtxtool info`. Settings category `[RTX]` declared in all four places (`openmw.md` §6) and a
checkbox in the launcher's Graphics page plus the in-game settings window, both marked as taking
effect on restart.

*Done when:* `openmw-rtxtool info` prints the RT pipeline properties; a test brings a device up and
down with zero validation errors; the toggle round-trips through the launcher and the config file.

### M1 — Scene description and extraction

`SceneDesc` types. A `NodeVisitor` in `openmw-rtx-bridge` that walks an `osg::Node` and emits meshes
(dedup by `osg::Geometry*` and by content hash for paged chunks), instances with world transforms,
materials from the stateset roles, and lights. Change tracking: a dirty set, not a rebuild.
`openmw-rtxtool scene --cell X` prints the counts.

*Done when:* a fixture NIF extracts to a hand-counted triangle and material count; re-extracting an
unchanged graph produces an empty dirty set.

### M2 — Primary visibility

BLAS per mesh, TLAS per frame, a ray-query compute pass writing depth, instance id and barycentrics.
`openmw-rtxtool screenshot`. **Interop spike here, not later** — get a flat-shaded RT image into the
game window while it is still simple enough to debug.

*Done when:* Seyda Neen's shore renders recognisably; the primary-hit fraction is reported on stdout
and asserted by a test; the same image appears in the game window through the interop path.

### M2b — Terrain, in the harness only

In the game terrain arrives free: `Terrain::QuadTreeWorld` has already put chunks in the scene graph
by cull time, and the mirror picks them up like anything else. Headless there is no `Terrain::World`
at all, so an exterior renders as objects floating in sky — Seyda Neen traced at 5.5% before this and
79% after.

A `ESMTerrain::Storage` over `EsmData::mLands`, which are already loaded, feeding a
`Terrain::TerrainGrid`. One chunk grid per cell, no LOD: the quadtree is for a world that streams,
and this one loads a cell and stops. **The renderer does not change at all** — chunks come out as
`osg::Geometry` and the extractor takes them like any other drawable, which is the mirroring argument
proving itself on the first thing it was asked to carry.

Land *textures* are not part of it. They are indexed per content file and `EsmLoader` flattens
records across files, so answering `getLandTexture` properly means teaching the loader a shape it
does not have. Every layer falls back to `_land_default.dds` until M3, which is where terrain
materials belong anyway.

### M3 — Textures and bindless materials

Upload from `osg::Image` — DDS blocks pass through untouched, everything else is converted; mip
chains preserved or generated. One bindless descriptor array. Ray-cone texture LOD from the start:
retrofitting it is how rtxmw lost a week of caustics (`design.md` §7.6).

*Done when:* an albedo-only render matches the vanilla texel at a named pixel; a mip-level test
proves the cone is being used; a masked surface in front of a wall shows the wall through its holes.

### M4 — Direct lighting and shadows

Sun plus point lights, binned into a world grid (`design.md` §8.10). Traced shadows, with water
excluded by a mask bit rather than by a cutout test — the any-hit version halves the frame rate
(`design.md` §7.6). A shadow ray runs the same candidate loop primary visibility does, so a grate
throws bars, but at the finest mip: a shadow carries no cone to resolve with. Opacity micromaps
retire the loop at M12.

*Done when:* radiance at a test pixel matches a hand-computed value; removing a light provably
changes that pixel; the sun's shadow terminator lands where the geometry says.

### M5 — Sky, sun, moons

Driven by `MWWorld::Weather` and `DateTimeManager` rather than re-derived from the ini — the one
place this fork starts ahead of rtxmw (`design.md` §8.45, §8.50, §8.53, §8.59 all describe work that
is a getter here). Sky is a light source, not a backdrop.

### M6 — Water

A port of rtxmw's water, which is the reason this fork exists in this shape. Read
`/home/xxorza/Projects/rtxmw/docs/design.md` §7 (lines 828–1113) and the shaders
`shaders/water.glsl`, `shaders/waves.glsl` before writing anything — the summary below is an index,
not a specification, and **the shader is more current than the document**.

- One unit quad instanced per water cell, level in the transform, **in the acceleration structure**
  so reflections, shadows and refraction see it without a second code path.
- Height field: **TMA spectrum** (JONSWAP under Kitaigorodskii shallow-water attenuation) spread by
  **Donelan-Banner**; 32 components as 8 wavenumber bands × 4 directions, sampled by *quantile* of
  the spread so every component carries equal energy; scaled to a significant wave height. Shortest
  wave **32 units** — 18 gives better caustics that tear, 50 is dull. Dispersion `sqrt(gk)` with
  Morrowind's gravity, 627.1.
- **Ripples carried on the swell**: displace the sample position by a low-frequency field
  (`WAVE_DRIFT` 13 units over `WAVE_DRIFT_LENGTH` 640) before evaluating, and take the Hessian with
  respect to the drifted position. Without this the caustics tile into a lattice.
- Shading on hit: Schlick Fresnel `F0 = 0.02`, `η = 1/1.333`; **one reflection ray and one refraction
  ray at the pixel's own cone spread, not the bounce spread** — that single mistake flattened every
  reflection in the game and cut the caustic term to a sixth; Beer–Lambert with Jerlov coastal
  extinction `(0.004572, 0.000714, 0.001143)`; single scattering with **both legs** attenuated,
  `(1 − T²)/2`; GGX sun glint against the wave normal.
- Waves below the ray cone are **averaged, and their variance returns as roughness** — LEAN mapping
  in one dimension. This is what makes the sun a shimmering road instead of a hard dot, and what
  keeps distant water from crawling with white sparks.
- Side-of-surface is decided by the **plane**, never by the wave normal.
- **Caustics analytically**, from the Jacobian of the refraction map:
  `det(I + dD) / det(I + dD − bend·depth·H)`, `1/|det J|`, depth capped at 140 units (past the first
  focus the model starts making light). Chromatic dispersion at 1.3326/1.3342/1.3392 for 600/550/450 nm
  is two extra multiply-adds and changes twelve pixels in ninety thousand — keep it, it is free and
  it is right.
- Underwater: Beer–Lambert on primary rays, total internal reflection past 48.6°, sun colour filtered
  by depth, and **the albedo is dimmed, not the lighting** (the filter divides by albedo).
- Shore fade over the last 35 units of depth; both water rays leave from the viewer's side and trace
  **solid geometry only**.

OpenMW-side inputs: water level per cell, `has_water = (flags & 0x02) || is_exterior()`, sea level
z = 0 outdoors. Not ported yet by anyone: shoreline foam (the **sign** of `det J` is where a surface
folds, which is where whitecaps belong) and underwater sun shafts.

*Done when:* the ten-units-above / ten-units-below transmission invariant agrees to 3% (and to 11%
at a slant, for the reason in §7.6 — water really is clearer from a boat); caustic contrast and
per-twelfth-second change are measured, not eyeballed; every expectation derives from one
`EXTINCTION` constant so tuning is a one-line change.

### M7 — Fog

A port of §8.38–8.43 (lines 2310–2560) **plus everything the shader has learned since** — `fog.glsl`
now has sun shafts (8 shadow rays across 24 steps) and a Mie phase function with a droplet-size
parameter that the document does not describe. Read the shader.

- 24 steps along the primary ray, non-uniformly distributed (`fog_depth`), density falling off
  exponentially from the **cell's water level** rather than the origin, every lamp in the light grid
  scattering into each step.
- **No new bindings**: `(emitted + albedo·lighting)·T + inscatter == (emitted·T + inscatter) +
  albedo·(lighting·T)`, so folding fog into both halves inside the trace equals fogging their sum,
  and the trace already has the lights.
- Three fbm octaves, each on its own heading at its own speed, lacunarity 2.27 so the lattices never
  align; horizontal domain warp `fbm(p + w·fbm(p))` at one level; a **coverage band**
  (`smoothstep(0.44, 0.66, fbm)`) is what makes it patchy rather than merely uneven — and the band
  must be checked against the fbm's actual distribution, which averaging octaves narrows sharply.
- The noise wants to be **coarse**: `FOG_GRAIN` 900, warp 450, scale height 2600. Structure finer
  than the step spacing aliases into noise the temporal filter then removes.
- Indoors is not a small outdoors (§8.42). Morrowind's recorded fog density is relative to view
  range; extinction here is absolute, so the conversion is explicit.

This fork starts ahead again: `MWRender::FogManager` and `MWWorld::Weather` already supply per-region,
per-weather fog colour and density, which rtxmw substitutes the sky for.

*Done when:* fog is off in the tests that measure surface radiance, and on in a view whose histogram
is asserted; a ridge view shows banks below it and a ground view shows structure.

### M8 — Indirect light

Path-traced diffuse bounce, sky as an emitter, blue-noise sampling, à-trous denoise demodulated by
albedo. Water and fog already compose with this by construction — both bypass the filter.

### M9 — De-lighting

Vanilla textures are pre-lit: baked ambient occlusion, baked highlights, baked lamp glow. Recovering
albedo is the difference between "ray traced Morrowind" and "Morrowind with a filter". rtxmw divides
an estimate out at sample time (`design.md` §8.35) and judges it on a contact sheet (§8.37). Do both.

### M10 — G-buffer and DLSS Ray Reconstruction

The exact buffer layout NGX reads (`design.md` §8.20), motion vectors including the
scene-far-from-origin case (§8.13), jitter with the right sign on both axes (§8.30), exposure applied
consistently (§8.28). 1920×1080 internal → 3840×2160.

### M11 — Full in-game integration

Cell add/remove, object add/move/remove, player and actor animation, first/third person camera,
menu and pause behaviour, screenshots, the local map. Settings that can change at runtime take effect
through `processChangedSettings`.

### M12 — Performance

SER, opacity micromaps for cutout foliage, BLAS refit for skinned actors against the double-buffered
`RigGeometry` output, BLAS compaction, cluster acceleration structures if they earn their place.
Target: 1920×1080 internal → 3840×2160 at 60 fps.

---

## 7. Development infrastructure

This is not overhead; it is what decides how fast the milestones above go.

### 7.1 `openmw-rtxtool`

```
openmw-rtxtool                                        a window where the game starts
openmw-rtxtool info                                   device, extensions, limits, memory
openmw-rtxtool scene  --view balmora --twice          instance/mesh/material counts, and what a
                                                      second extraction pass adds, which is nothing
openmw-rtxtool scene  --list-views                    the named viewpoints
openmw-rtxtool scene  --cell -2,-9 --find lighthouse  where an object stands, for authoring a view
openmw-rtxtool shot   --view balmora --out b.png      one frame, no window
openmw-rtxtool view   --view vivec --size 2560x1440   a window to fly around in
openmw-rtxtool view   --view balmora --frames 600     the same, for something that cannot click
openmw-rtxtool sheet  out.png --views all             contact sheet, for judging a look change
openmw-rtxtool golden --views all                     compare against files/rtx/golden, write diffs
openmw-rtxtool bench  --views all --frames 300 --json
openmw-rtxtool watch  --view seyda-neen-shore         re-render on shader change
```

The first four verbs exist. `sheet`, `golden`, `bench` and `watch` do not yet.

- **A cell argument is addressed the way Morrowind does**: a pair of integers is an exterior,
  anything else is an interior's name.
- **Every run prints a summary line** — primary-hit fraction, mean luminance, frame time — so a
  change is checkable without opening the PNG. rtxmw's single best harness decision.
- **Named views** in `files/rtx/views.cfg`: cell, and usually a camera. A view id is the unit of
  comparison across commits, and every one of them is a Zed task in `.zed/tasks.json`. Pressing `P`
  in the window prints the current camera as a block to paste back into the file, which is how a
  view gets authored once someone has flown to somewhere worth keeping.
- **Deterministic by construction**: fixed frame index drives jitter and blue-noise offsets, time of
  day and weather are forced, `--seed` fixes anything left. Two runs of the same view are the same
  bytes.

### 7.2 Tests

Three kinds, in the order they catch things:

1. **Unit tests against synthetic scenes** — `openmw-rtx` alone, no game data. Spectrum maths,
   Jacobians, light binning, descriptor bookkeeping. These are the tests that can assert
   hand-computed numbers, and they are where most of the water and fog physics belongs.
2. **Renderer tests driving the real renderer headlessly** — a device, a small scene, a few hundred
   pixels, asserted radiance. rtxmw's `primary_visibility.rs` does this in under a second, and it is
   why that project could refactor freely.
3. **Golden images** over the named views, with a perceptual metric and a per-view threshold, and a
   contact sheet for the cases where the right answer is a judgement.

Game data is required for (2) and (3). Absent, they **skip**; present but wrong, they **fail** — a
silent skip looks like a pass. Morrowind GOTY is at `/home/Games/Morrowind`.

Every test enables the validation layers and fails on any recorded error, keyed by thread so parallel
tests do not fail each other (rtxmw's `validation_log.rs` is the pattern).

### 7.3 Zero allocations per frame

A steady-state frame with a stationary camera must not touch the heap. The concern is jitter, not
throughput: at 60 fps a single 2 ms allocator stall is a dropped frame and an average hides it.

A test binary linked with `-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free` plus a
counting global `operator new`/`delete` renders N frames of the real renderer — frame constants,
recording, submit, wait — and asserts the counter is zero across the measured window. Warm up first;
device bring-up and first-call driver caching allocate legitimately. Budget expressed as a named
constant, not `== 0`, so a driver path that must allocate can be accommodated *deliberately*, with
the number and the reason visible.

In debug builds a `NoAllocScope` guard around the record path asserts on the same counter, so a
regression surfaces during ordinary play rather than only in CI.

What this forbids on the frame path: `format!`-equivalents, `std::string` construction, `push_back`
into an unreserved vector, `std::function` capture, `make_unique`, and any logging that is not
compiled out.

### 7.4 Validation and debugging

- `VK_LAYER_KHRONOS_validation` on in debug builds and in every test. Error severity → log the
  message with the object names, then `std::abort()`. Warnings are printed and not stored.
- Opt-in via CLI/setting: synchronization validation, GPU-assisted validation, best practices.
- **Name every Vulkan object** and label every pass with `VK_EXT_debug_utils`, compiled out in
  release. An unreadable capture is a debugging session that does not happen.
- `VK_EXT_device_fault` on device loss; NVIDIA Aftermath if it proves necessary.
- Shaders compiled with `-g` in debug so Nsight correlates to source.

### 7.5 Shader hot reload

Watch `components/rtx/shaders/`; on change, recompile with `glslc`, run `spirv-val`, rebuild only the
affected pipelines, keep rendering. Bound to a key in-game and to `openmw-rtxtool watch` headless. A
compile failure prints and keeps the last good pipeline.

Art-direction constants — water extinction and scattering, fog height and grain, the tone curve —
live in one hot-reloadable block, not in `const` declarations. Tuning must not be a rebuild.

### 7.6 Profiling

Per-pass timestamp queries into a ring buffer; `bench --json` for the harness, an overlay in-game.
**Only A/B pairs measured back-to-back in one run mean anything on this laptop** — rtxmw measured the
same scene at 116 and 382 fps because the GPU idles at 315 MHz and ramps to 2,280 under sustained
load. No absolute frame rate from this machine is worth writing down.

### 7.7 Build speed

`ninja`, `mold`, `ccache` at 25 GB, `-DOPENMW_UNITY_BUILD=OFF` so incremental edits stay cheap, and
`CMAKE_EXPORT_COMPILE_COMMANDS=ON` for clang-tidy and the LSP. 32 threads available; a cold build of
everything except the Qt tools is under six minutes, and the RTX targets alone are seconds.

Nothing further is worth adding. `sccache` and `distcc` distribute across machines and there is one
machine; GCC precompiled headers are a rebuild-everything hazard for a saving ccache already has.

---

## 8. Risks

| risk | shape | response |
|---|---|---|
| **Mirror cost** | The per-frame traversal and diff of the OSG graph becomes the frame's bottleneck | Measure it at M2, before anything depends on it. Dirty sets keyed on OSG's own frame numbers; instance transforms are the only per-frame data. |
| **Interop stall** | The GL/VK handoff serialises the two devices | Timeline semaphores, one frame of latency, measured at M2's spike. |
| **Vanilla content assumes a rasterizer** | Sheets lit from both sides, discarded outer transforms, Z-first Euler angles, two-sided stencil (`design.md` §8.1–8.6) | Every one of these is already diagnosed next door. Read §8 before debugging anything that looks like a content bug. |
| **De-lighting is a look problem, not a code problem** | No test says an albedo is right | Contact sheets and a human. Budget iteration for it. |
| **Skinned BLAS refit** | Hundreds of actors, refit per frame | It is why `RigGeometry`'s CPU output is a gift and also a memcpy. Measure at M12; consider refitting only what moved. |
| **clang-format drift** | CI pins 14, this machine has 22 | Format with 14 or accept churn; decide before the first large diff. |

## 9. Open questions

- **Interiors.** A room is not a valley (`design.md` §8.42) and interiors are half the game. Whether
  fog, sky light and bounce need a separate interior model is unanswered.
- **Groundcover and particles.** Grass is alpha-cutout and enormous in instance count; particles are
  camera-facing quads that ray tracing has no natural answer for.
- **The GUI's long-term home.** Interop is the way in. Whether it stays is a performance question
  nobody can answer yet.
- **Distant land.** OpenMW's object paging and terrain LOD were tuned for a rasterizer's silhouette
  budget, not a BVH's.
