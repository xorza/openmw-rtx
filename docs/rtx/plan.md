# Implementation plan

An experimental ray-traced renderer inside this OpenMW fork. The posture and the priority order live
in `CLAUDE.md`; the host engine's structure and the seams named below are in `docs/rtx/openmw.md`. This file
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

**B, because of §3.3 and §3.5 of `docs/rtx/openmw.md`**: by cull time the world is already CPU-resident
triangles with world transforms, resolved texture roles, and — for actors — *already skinned*
positions in `RigGeometry::getGeometry(frame)`. Terrain LOD, object paging and groundcover have all
already made their decisions. The mirror is a traversal and a diff, not a content pipeline.

Consequences to accept:

- The cull traversal still runs (`docs/rtx/openmw.md` §3.4). It is cheap and it is what makes skinning and
  LOD happen. The **draw** is what goes away.
- The mirror must be incremental. A full rebuild per frame is the naive version and it will not hold
  a frame budget; instance transforms change every frame, geometry rarely, materials almost never.
  Geometry and materials met that from M1; placements did not, and `docs/rtx/mirror.md` is the
  measurement of what that costs and the shape that replaces it.
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
users (`docs/rtx/openmw.md` §7), and each would need reimplementing before the game was playable again.
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
`openmw-rtxtool info`. Settings category `[RTX]` declared in all four places (`docs/rtx/openmw.md` §6) and a
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

Land *textures* came later, with M3's materials, and needed two things the rest of the loader does
not have. A cell's blend map indexes into the texture list of the plugin that wrote it, so the key is
`(plugin, index)` rather than a record id — and the id still matters, because redefining one repaints
every index that reaches it. And the shading is not on the scene graph at all:
`Terrain::TerrainDrawable` carries one alpha-blended pass per ground texture over the same triangles,
which is how a rasterizer draws a blend it cannot sample in one go. The RT path reads the pass vector
back into layers and sums them at the hit — the first place a mirrored material is not a copy of what
OpenSceneGraph would have drawn.

### M3 — Textures and bindless materials

Upload from `osg::Image` — DDS blocks pass through untouched, everything else is converted; mip
chains preserved or generated. One bindless descriptor array. Ray-cone texture LOD from the start:
retrofitting it is how rtxmw lost a week of caustics (`design.md` §7.6).

*Done when:* an albedo-only render matches the vanilla texel at a named pixel; a mip-level test
proves the cone is being used; a masked surface in front of a wall shows the wall through its holes.

### M4 — Direct lighting and shadows

Point lights and cell ambient first, since that is what an interior *is* — and the sun waits for M5,
because an exterior's direction and colour come from `MWWorld::Weather` and the harness has no
weather. Traced shadows, with water excluded by a mask bit rather than by a cutout test — the
any-hit version halves the frame rate (`design.md` §7.6). A shadow ray runs the same candidate loop
primary visibility does, so a grate throws bars, but at the finest mip: a shadow carries no cone to
resolve with. Opacity micromaps retire the loop at M12.

Everything about a lamp is derived, because a `LIGH` record carries a colour and a radius and **no
intensity**: the original renderer's fixed falloff curve supplied brightness. Intensity comes off the
recorded radius squared, and the reach off the radius stretched — Morrowind's radii light a lantern's
own post and nothing else, which worked when an ambient term lit the room and does not when the
ambient is real light. Binning into a world grid (`design.md` §8.10) waits until a cell has enough
lights to measure; the most so far is 26.

*Done when:* radiance at a test pixel matches a hand-computed value; removing a light provably
changes that pixel; the sun's shadow terminator lands where the geometry says.

### M5 — Sky, sun, moons

Driven by `MWWorld::Weather` and `DateTimeManager` rather than re-derived from the ini — the one
place this fork starts ahead of rtxmw (`design.md` §8.45, §8.50, §8.53, §8.59 all describe work that
is a getter here). Sky is a light source, not a backdrop.

The sun and the sky's two colours came first, off the fallback settings the game reads for itself and
the orbit at `apps/openmw/mwworld/weather.cpp:901`. The harness has no weather *simulation* — that
needs `MWWorld::World` — so it takes a weather name and an hour, and steps between the four phases
where the engine ramps across each transition. Exact at every hour outside a transition window, and
the ramp arrives with the engine at M11. What is still missing is the sky *dome*: moons, clouds, the
sun's own disc, and stars.

### M6 — Water

A port of rtxmw's water, which is the reason this fork exists in this shape. Read
`/home/xxorza/Projects/rtxmw/docs/design.md` §7 (lines 828–1113) and the shaders
`shaders/water.glsl`, `shaders/waves.glsl` before writing anything — the summary below is an index,
not a specification, and **the shader is more current than the document**.

- One unit quad instanced per water cell, level in the transform, **in the acceleration structure**
  so reflections, shadows and refraction see it without a second code path. **Done**, along with the
  material kind water needed — a third one beside the object's single diffuse and terrain's layer
  stack — and the mask bit that keeps water out of a shadow ray. Water shades to a flat placeholder
  until the rest of this arrives.
- Height field: **TMA spectrum** (JONSWAP under Kitaigorodskii shallow-water attenuation) spread by
  **Donelan-Banner**; 32 components as 8 wavenumber bands × 4 directions, sampled by *quantile* of
  the spread so every component carries equal energy; scaled to a significant wave height. Shortest
  wave **32 units** — 18 gives better caustics that tear, 50 is dull. Dispersion `sqrt(gk)` with
  Morrowind's gravity, 627.1. **Done**, along with the drift, the cone filter and the normal. The
  dispersion is the full `omega^2 = g k tanh(k h)` rather than its deep-water limit, because the
  shelf is the whole reason this is TMA: a 1257-unit wave over 60 units of water travels at 54% of
  its open-sea speed. The table resolves once on the host and animates from the frame's clock.
- **Ripples carried on the swell**: displace the sample position by a low-frequency field
  (`WAVE_DRIFT` 13 units over `WAVE_DRIFT_LENGTH` 640) before evaluating, and take the Hessian with
  respect to the drifted position. Without this the caustics tile into a lattice.
- Shading on hit: Schlick Fresnel `F0 = 0.02`, `η = 1/1.333`; **one reflection ray and one refraction
  ray at the pixel's own cone spread, not the bounce spread** — that single mistake flattened every
  reflection in the game and cut the caustic term to a sixth; Beer–Lambert with Jerlov coastal
  extinction `(0.004572, 0.000714, 0.001143)`; single scattering with **both legs** attenuated,
  `(1 − T²)/2`; and the sun itself. **Done**, together with the shore fade, total internal
  reflection, and the facet guard that keeps a glancing reflection finite. Hit resolution split from
  traversal to make it possible: `trace` owns its query and returns a surface, `shadeSurface` lights
  one, and water calls both — which is how a shader with no recursion reflects.

  The daylight is attenuated on its way down as well as up, and a primary ray is fogged when the
  camera is under the surface — both off one water level in the frame's constants, which is minus
  infinity where a cell holds no water so that "how deep is this point" is never positive and nothing
  needs a second question.

  **The glint is not a lobe on the water, it is a disc in the sky** — which is `water.glsl`'s answer
  and not §7.3's, and the shader is the more current of the two. Water already traces a reflection
  ray, so a highlight model of its own would be a second way to draw the same thing; the sun is drawn
  where a ray that hit nothing looks, and the reflection finds it. The disc is widened by the ray
  cone *and* by the slopes the cone could not resolve, and dimmed by exactly the widening — the same
  flux over a larger cap — so a rougher sea spreads the sun without adding any. Measured: a flat sea
  puts it on 4 pixels of 4,096 at 202 of 255, a sea with a state in it on 1,914 at a peak of 10.
  Anything else reflective now gets the sun for nothing, and the sun's size lives in one place.
- Waves below the ray cone are **averaged, and their variance returns as roughness** — LEAN mapping
  in one dimension. This is what makes the sun a shimmering road instead of a hard dot, and what
  keeps distant water from crawling with white sparks.
- Side-of-surface is decided by the **plane**, never by the wave normal.
- **Caustics analytically**, from the Jacobian of the refraction map: `1/|det J|` with
  `J = I − bend·depth·H`, depth capped at 140 units (past the first focus the rays have folded and
  one Jacobian no longer describes what is there). **Done**, and two things came out differently
  here.

  One determinant and not the reference's ratio of two, because this surface is not displaced: the
  quad stays flat and only its normal moves, so the patch of surface the light left *is* the patch of
  parameter space it came from. A Gerstner sea would need `det(I + dD)` over the numerator.

  And the textbook form is quietly wrong by twelve per cent. The map is evaluated at the bed rather
  than at the surface the light left, so samples are not weighted by the area each stands for, and
  `E[1/det] > 1/E[det]`: measured over a bed at the depth cap, the term handed back **12.3% more
  light than fell on the water**. Writing `det = 1 − u + v`, `E[det H]` vanishes for independent
  sinusoids — `Hxx·Hyy` and `Hxy²` are the same sum — leaving `Var[u] = bend²·Σ(Ak²)²/2` as the whole
  of the second order, which accumulates in the loop already running for a multiply and an add.
  12.3% → 2.4%, with the pattern's contrast unmoved at 0.366; on a real frame the mean green goes
  from 109.34 without caustics to 109.33 with them.

  **Chromatic dispersion was measured out rather than in.** Cauchy's 1.3326/1.3342/1.3392 costs three
  determinants over a Hessian that does not depend on the channel, and the reference measured twelve
  pixels in ninety thousand differing by more than one level — which this fork's own rule about
  proving a parameter matters cannot be satisfied for. It goes in if the sea ever gets steep enough
  for the determinant to approach zero, which is where prism edges on cusps would come from.
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

All three hold. The invariant is what found the missing half: the two views are different code paths
through the same physics and cannot agree while one of them lights the bottom as though the water
over it were not there — it agrees to two bytes of 63, where the arithmetic says 1.068 in radiance
compressed by sRGB to about 3%. Every water expectation in the tests derives from `WATER_EXTINCTION`,
`WATER_IOR` and `WATER_F0` rather than from a literal beside them.

Contrast is 0.366 of the pattern's own mean, and **51.1% of it is new a twelfth of a second later** —
against the reference's sweep of 73% at an 18-unit cutoff (its best caustics, and they tear), 51% at
32, and 33% at 50. This fork cuts at 32 and lands on 51: the same spectrum reproducing the same
behaviour. Both numbers are asserted, so moving the cutoff cannot silently move them.

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

**Done.** The extinction is not the reference's eyeballed figure but `MWRender::FogManager`'s own
ramp read back: the original fogs *linearly* from `view * (1 - depth)` to `view`, so matching where
each is half gone gives `sigma = ln(2) / (view * (1 - depth / 2))`, and clear weather's 0.69 over the
game's 7168 units comes to 1.476e-4 against the 1.5e-4 settled by eye. It is the same conversion
indoors, which is why there is no second scale: a room is faint because it is small.

The coverage band was measured against this field rather than copied — mean 0.4996, standard
deviation 0.1204, and `0.45..0.65` leaves 40% of the volume clear. **Sample that over a plane wider
than the grain, not a sphere**: a million pixels of a sphere of radius 5,000 is a million samples of
about 390 cells, and the mean it gives is wrong by 9% while looking precise. The band is normalised
by its own mean so the noise redistributes air rather than removing it, and
`theBankedFieldHoldsAsMuchAirAsAnEvenOne` is what stops that constant drifting when the band moves.
It settles at 0.971 rather than 1.000, which is Jensen's inequality on a convex `exp` and not an
error.

Octaves fade where the march's own step outruns them, which is `resolved`'s argument with a different
sampler — and **fewer octaves is a wider distribution, not a narrower one**, so the field is rescaled
about its mean to hold the spread the whole stack would have. It pays only on long rays: a third off
an open view, under 4% at Balmora, where the steps are short enough to resolve everything.

The sun scatters through Jendersie and d'Eon's HG-Draine fit at eight-micron droplets, **per steradian
rather than normalised to isotropic** — the `4 pi` a ratio test cannot see, so the absolute value is
asserted beside the ratio. Eight shadow rays cut the march into stretches, gated off below a fiftieth
of what the sky puts in, which costs 0.2 to 0.36 ms where the sun is in play and nothing at all in an
interior. `aLidOverTheMarchTakesTheSunOutOfTheAirBeneathIt` asserts the air under a lid scatters
*exactly* what sunless air does, because merely darker would pass while leaking.

What is looked at rather than asserted: the banks over a shore, and the shafts themselves. There is
no histogram test of a real view.

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

**Removal is done, and it is mark and sweep.** The mirror re-walks the whole graph every frame, so
anything alive was met: `SceneExtractor::retire` drops every identity it did not find and
`SceneDesc::retain` closes the gaps behind it, carrying the survivors' indices through. That is not
only about memory — the identity maps are keyed on raw `osg` pointers, and an address the engine
freed when a cell unloaded can be handed straight back for something else, so without the sweep the
next thing allocated there inherits a mesh it has nothing to do with.

What is left of the milestone's first clause is the *harness*, which still never unloads: it keeps a
snapshot of the still world rather than re-walking it, and a sweep against that would retire the
region the camera is standing in. Giving it cell graphs to own is what would close it.

### M12 — Performance

SER, opacity micromaps for cutout foliage, BLAS refit for skinned actors against the double-buffered
`RigGeometry` output, BLAS compaction, cluster acceleration structures if they earn their place.
Target: 1920×1080 internal → 3840×2160 at 60 fps.

#### Where it stands

`openmw-rtxtool bench`, the `[default]` suite: 1920×1080 out of 1280×720 at DLSS quality, layers off,
600 frames of world at 60 Hz after a warm-up second, median of each row.

| place | frame | trace submit | place submits | left over | fps |
|---|---|---|---|---|---|
| Seyda Neen's ship, 51,742 instances | 30.26 ms | 6.94 | 11.60 | 11.72 | 33.0 (25.0 at the 1% low) |
| Balmora's guild of mages, 1,239 | 7.61 ms | 4.84 | 1.74 | 1.03 | 131.5 (80.5) |

And the same frames as the device's own clock reports them, which is a different story:

| place | trace | upscale | refit | tlas | exposure | composite | tone | **GPU total** |
|---|---|---|---|---|---|---|---|---|
| Seyda Neen's ship | 3.55 | 2.55 | 2.08 | 0.43 | 0.06 | 0.04 | 0.03 | **8.74 ms** |
| Balmora's guild | 2.04 | 2.04 | 0.27 | 0.17 | 0.05 | 0.03 | 0.02 | **4.62 ms** |

**The GPU is idle for two thirds of the exterior frame.** Eight and three quarter milliseconds of
device work sit inside a thirty millisecond frame, and the gap is CPU: eleven milliseconds of
`placeScene` against two and a half of building anything, and eleven more of the harness posing
actors and walking the graph. Three submits, each fenced before the next begins, so none of it
overlaps anything.

That reframes M12. The trace is 3.55 ms and the budget is 16.7; what stands between this renderer and
that number is not the shader. It is packing fifty-one thousand instance records twice a frame,
handing them over in two submits nobody overlaps with, and a harness that re-poses a town every
frame — and only the last of those is the harness's rather than the renderer's.

#### What the finished features cost

Written down as each one lands rather than acted on — see `CLAUDE.md`, *Feature-complete first, then
fast*. Numbers are 1920×1080, no upscaling, validation off, best of thirty on this box.

| what | where | cost |
|---|---|---|
| the sprite layer, in the trace | Seyda Neen's ship, 165 emitters and 4,655 particles | 8.22 → 8.85 ms |
| | Balmora's guild of mages, 19 emitters | 7.34 → 7.47 ms |
| | Balmora from the bridge | 6.87 → 6.92 ms |
| the harness's live props, whole frame | Balmora in a window, 94 props | 49 → 37 fps |
| a retirement | any frame a cell leaves | one full `setScene` |

The trace is the cheap half and the sphere test is why: an emitter is a few units across and one
rejection throws it away for almost every pixel. The window's six milliseconds are not the sprites —
they are the 185 extra deforming drawables the instanced props bring, refit once a frame whether or
not their skeletons moved. A prop's *emitters* change every frame; its geometry usually does not,
and nothing tells the two apart yet.

The retirement's own cost — a compaction, which is a copy of what survived — is not the number that
matters about it. What matters is that it bumps the revision, and a changed revision is a full
`setScene`: every bottom-level structure built again, every table uploaded again, every texture
uploaded again. That is already what a cell *arriving* costs and removal does not make it worse, but
it is the thing standing between this renderer and a walk across Vvardenfell, and making the build
incremental is M12's.

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
openmw-rtxtool bench                                  time the [default] suite of files/rtx/benches.cfg
openmw-rtxtool bench  --views all --json=bench.json   the same over every named viewpoint
apps/rtxtool/profile.sh --view balmora-mages-guild    the same under perf, and where the time went
openmw-rtxtool watch  --view seyda-neen-shore         re-render on shader change
```

`sheet`, `golden` and `watch` do not exist yet.

**What the harness cannot see, and what to do about it**, is `docs/rtx/harness.md`: it feeds the
renderer by a different route from the game — no scene graph, no unloading, no sweep — and every
renderer defect found in the M12 stretch was invisible here and obvious the moment the game was
measured.

#### `bench`

**Ten seconds of *world* per place, not ten seconds of wall clock.** The world steps a sixtieth of a
second per frame however long that frame took — which is `PosedActors`' own step and not a second
opinion about it — so `--seconds=10` is six hundred frames, and two builds render the same six
hundred: the same particles in the same places, the same sample in each pixel. A run against the
clock would animate further on the faster build and be measuring a different scene.

Where it goes is a **suite**: a list of `views.cfg` ids in `files/rtx/benches.cfg`, so a place worth
measuring and the same place worth looking at cannot drift apart. `--views=a,b` is the same thing for
one run and `--views=all` is every viewpoint there is. The layers are **off unless asked for**,
whatever the build default is — a run that quietly measured one under instrumentation is worse than
no run at all, because it produces a number.

Three distributions per place rather than one figure, because they are three different problems:

| row | what it is |
|---|---|
| `trace ms` | the renderer drawing — one submit, including the wait |
| `place ms` | the renderer being told what moved: the top level rebuilt, every skinned mesh refitted |
| `frame ms` | the whole loop, so what is left over is the harness posing actors and re-walking the graph |

Each carries median, mean, p95, p99, best and worst, by nearest rank, so every figure is a frame that
actually happened. A warm-up second is drawn and thrown away first: this box's GPU idles at 315 MHz
and ramps under load, and a scene's first frames pay for its residency as well.

Under those, a `gpu ms` row of what the **device's own clock** says each stretch cost — `refit`,
`tlas`, `trace`, `filter`, `composite`, `upscale`, `exposure`, `tone` — medians only, most expensive
first. That row against the three above it is what separates a slow shader from a slow everything
else, and on an exterior the answer turned out to be neither the shader nor the GPU at all.

#### The same run, inside the game

`openmw-rtxtool bench` is deterministic and the game is not — but the game is the only thing that
carries the real workload, and every renderer defect this fork found in the M12 stretch was invisible
to the harness and obvious the moment the game was measured: the harness stages a world once and
re-walks only its actors, so it never pays for the whole-graph walk, the sweep, or a cell arriving.

```sh
apps/rtxtool/bench.sh                       # ten seconds at every bench_*.omwsave, newest first
apps/rtxtool/bench.sh --seconds=30 --note="after the appendable texture array"

OPENMW_RTX_BENCH=10s:2s openmw --load-savegame <a save>    # one place, by hand
OPENMW_RTX_BENCH=600:120 openmw --load-savegame <a save>   # or a frame count
```

The same three rows and the same `gpu ms`, then the game quits — it prints what `bench` prints
because both call the same `Rtx::describeTimes`. `bench.sh` runs every save whose name begins
`bench_`, and **prepends** what it measured to `.notes/bench.txt` with the commit it was measured at,
because the question a results file gets asked is "did that help" and the answer is the top two
blocks.

**Compiled in by asking, not by build type.** `-DOPENMW_RTX_BENCH=ON`, which `release.sh` passes and
a shipping build does not. Gating it on "not Release" would have been the obvious thing and the wrong
one: the build worth measuring is the optimised one, so that puts the benchmark in every build except
the only one whose numbers mean anything.

**Where to stand is a savegame's job.** `--load-savegame` restores the player, the camera and the
world exactly; a pair of coordinates restores only the first, and leaves the weather, the actors and
the animation wherever that run happened to put them.

**Determinism is not on offer here and is not chased.** The game has AI, physics and scripts, so two
runs differ — what makes the numbers comparable is a fixed camera, enough frames, and reading the
tail rather than the mean. Measured against itself the median held to a tenth of a millisecond across
consecutive runs, which is finer than any change worth making.

**It touches nothing outside this fork's own code.** `MWRender::Rtx::Bench` reads one environment
variable, is fed each frame by `Tracer`, and ends the run through `StateManager::requestQuit` the way
the quit key does — no command line, no engine loop, no rendering manager. Where `OPENMW_RTX_BENCH`
is undefined the class has no members and no body, so the frame path carries nothing and a Release
build contains none of it. The only upstream file involved is `apps/openmw/CMakeLists.txt`, inside
the `if (OPENMW_RTX)` blocks this fork already added.

#### Timestamps and labels

`Rtx::GpuTimer` writes a pair of timestamps around each of those zones and resolves them after the
frame's last fence. Both ends are taken at `ALL_COMMANDS`, so a zone begins when the work before it
has finished and ends when its own has: the spans cannot overlap, which would distort a renderer
whose passes overlap and does not distort this one, since there is a full barrier between every pass
already. Zones span all three of a frame's submits — the two structure builds and the draw — because
each reserves and resets only the pair of queries it writes.

The same bracket opens a `VK_EXT_debug_utils` label, so a Nsight or RenderDoc capture shows the frame
as named regions rather than as a run of dispatches. That is where the counters a timestamp cannot
give you live. Labels follow `OPENMW_RTX_DEBUG_NAMES` and so are absent from a Release build;
timestamps are not gated, because Release is where a measurement is taken.

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

A counting global `operator new`/`delete`, defined in one translation unit of the test binary so the
linker uses it for the whole thing, renders N frames of the real renderer — frame constants,
recording, submit, wait — and asserts the counter across the measured window. Warm up first; device
bring-up and first-call driver caching allocate legitimately. Budget expressed as a named constant,
not `== 0`, so a driver path that must allocate can be accommodated *deliberately*, with the number
and the reason visible. It measures zero.

**No `--wrap=malloc`**, which the first version of this called for. It would also count the driver's
own allocations, which are not this renderer's to control, and every one of the things the frame path
is forbidden to do — a `std::string` built, an unreserved vector grown, a `std::function` captured, a
`make_unique` reached for — arrives through `operator new` regardless. Wrapping malloc would have
added noise without adding reach.

**Two things had to be true before the number meant anything.** `CommandPool::submitAndWait` takes a
fresh command buffer from the pool on every call, which the driver satisfies out of the heap: it is
setup's shape and says so, and a frame has to reuse a buffer against a fence instead. And the
validation layers go to the heap on every command they inspect — sixty-six times a frame here — so
the test needs a device without them, which is what `getUnvalidatedHarness` is for.

A `NoAllocScope` guard asserting on the same counter in debug builds waits for M11: it needs the
counter in the renderer rather than in a test binary, and there is no ordinary play for it to surface
during until the renderer is inside the engine.

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
The device's side of that is §7.1's `gpu ms` row. The CPU's side is `apps/rtxtool/profile.sh`.

#### `profile.sh`

`perf record` around a `bench` run, and it answers the question the GPU timestamps opened: an
exterior frame takes 30 ms of which the device does 8.7, so where does the rest go.

**The recording is bounded to the frames that were measured**, by perf's control fifo:
`--delay=-1` starts the counters off and `bench --perf-control=<path>` writes `enable` when the
warm-up ends and `disable` when the place is done. Everything else a run does — three ESM files,
a cell extracted, ninety megabytes of structures built, the renderer taken down — is outside the
profile rather than a quarter of it, and the alternative is trimming a whole-run recording to a
boundary nobody can name to better than a second.

Three choices that this machine forces:

| | why |
|---|---|
| `-e task-clock`, not `cycles` | The CPU is hybrid. `cycles` resolves to `cpu_core/cycles/` **and** `cpu_atom/cycles/`, so a thread that migrated between a P core and an E core is split across two reports. `task-clock` is one software event on every core, and it counts nanoseconds. |
| `--call-graph fp` | Arch builds every package with `-fno-omit-frame-pointer`, so the chain runs out through libstdc++, OSG and SDL for free. `--dwarf` is there for the graphics driver, which has `.eh_frame` and no frame pointers, and costs about five times as much. |
| `--no-inline` | perf resolves the inline stack at a *return* address, which at `-O3` lands in whatever was inlined after the call — a chain through `placeScene` comes back as `~basic_string`, `_M_dispose`, `_M_is_local`. |

**It profiles the build the numbers come from and does not have one of its own.** A stock Release
build has neither line numbers nor frame pointers, so a report off one is a list of exported symbols
with no path back to the frame — but the answer to that is `-g1 -fno-omit-frame-pointer` on
`release.sh`, not a second binary beside it. `-g1` is line tables and nothing else, free at runtime,
33 MiB against 7 stripped and 140 at full `-g`; frame pointers measured at **0.2%** on a 29 ms frame,
against a run-to-run spread of 2.7%. A profiling build would have cost no less and would have
explained a frame the benchmark never timed.

The summary is **cores busy** — the recording's task-clock nanoseconds over the window `bench` says
it measured — and then the same samples four ways: by total time with pass-through frames collapsed,
by self time, by source line, and by library. The line view is what `-g1` buys: the second-hottest
entry in an exterior frame is an unresolved address in libc until the line tables call it
`memmove-vec-unaligned-erms.S:660`. `--offcpu` swaps the sampler for perf's BPF off-CPU
profiler, which needs root; it can say which library the process waits in and cannot say which
frame asked, because a BPF-collected stack is unwound by frame pointer and the driver has none.

**What it found first time out**, on the deck at Seyda Neen at 1280x720 traced, 1920x1080 shown:

| | share of the measured CPU |
|---|---|
| `PosedActors::step` — the harness standing in for the game | 54% |
| `VulkanRenderer::placeScene` | 22% |
| `SceneExtractor::extract` | 20% |
| `makeInstanceRecords`, self | 6.4% |

and **0.67 cores busy**. A 30 ms frame with 8.7 ms of device work and two thirds of one core: the
frame is neither GPU-bound nor CPU-bound, it is serialised — three fenced submits with the harness
re-posing five hundred actors between them.

**One submit times the GPU's clock rather than the shader**, which this laptop makes unmissable: it
idles at 315 MHz and ramps only under load, so rtxmw measured the same scene at 116 and 382 fps. Here
a single cold trace of one view came back at 0.485, 1.21, 1.89 and 2.13 ms on four runs of identical
code — and three interleaved eight-run blocks of an A/B gave 0.373, 0.440, 0.707, where the two
*identical* blocks differed by more than the change did. A single number from `shot` was worse than
no number, because it looked like one.

**`shot --repeat N` traces the same frame N times inside one device session and reports the best,
with the median and worst beside it.** The best is the answer and the spread is whether to believe
it. Measured over five separate processes at 1920x1080 with `--repeat=100`, the best came to 0.1908,
0.1908, 0.1927, 0.1946 and 0.1948 ms — **a spread of 2.1%**, against 4.4x for the cold single submit,
and against a *median* whose own spread is 6.8%. The minimum over enough runs is the repeatable
statistic; the mean and the median are not.

The default is eight, which costs four milliseconds against a quarter-second of device setup and is
within 16% of the converged figure. **A comparison worth quoting uses hundreds**, and quotes the best.

**And it uses `--validation=false`, because the layers are on by default outside a Release build.**
Core and synchronization validation cost about 6% of a trace; GPU-assisted validation costs another
100%, so a Balmora frame that traces in 2.66 ms reads 5.68 under all three. `shot` says "with the
validation layers on" beside any figure measured under them, so a number that should not be compared
carries the reason with it.

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
- **Groundcover.** Grass is alpha-cutout and enormous in instance count. Particles were the other
  half of this question and are answered: a sprite goes in a table beside the lights and is marched
  against the primary ray, never into an acceleration structure.
- **The GUI's long-term home.** Interop is the way in. Whether it stays is a performance question
  nobody can answer yet.
- **Distant land.** OpenMW's object paging and terrain LOD were tuned for a rasterizer's silhouette
  budget, not a BVH's.
