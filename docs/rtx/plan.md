# Implementation plan

An experimental ray-traced renderer inside this OpenMW fork. The posture and the priority order live
in `CLAUDE.md`; the host engine's structure and the seams named below are in `docs/rtx/openmw.md`. This file
is the route.

**Decisions and what they came to, not a changelog.** §2–§5 are the choices everything else rests on;
§6 is the route and what each milestone settled; §7 is the tooling that decides how fast it goes;
§10–§12 are the live ones — where the current design came from and what is still wrong. Blow-by-blow
belongs in commits.

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

- ~~The cull traversal still runs. It is cheap and it is what makes skinning and LOD happen.~~
  **Wrong, and §12 has the evidence.** Cull is view-dependent, so inheriting skinning, terrain LOD
  and object paging from it means an actor outside the frustum is mirrored in a stale pose and
  distant geometry is absent from reflections. The decision to mirror the graph is untouched; what
  changes is that the three things OpenMW hangs off the cull traversal have to become ours, after
  which the traversal has no reason to run and the draw goes with it.
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

### What the end state would cost, now that it is worth costing

The reasoning above is still right about the *order*, and it has aged in one respect: most of the
render-to-texture users it names are rasterizer features this renderer replaces rather than things to
port. Of the ten files in `mwrender` that use one, the sky, the water's reflection and refraction, the
ripples, the precipitation occlusion and the post-processor are all answered with rays or not needed.
What is actually left is four things.

| what | what it becomes | size |
|---|---|---|
| MyGUI | a Vulkan render manager: textured quads, a scissor, a texture cache | the one real piece — `components/myguiplatform` is 1,529 lines and its twin would be comparable |
| the inventory doll, the local and global maps | **traces**, not rasterizations: a second camera into an offscreen image, which this renderer already does for `shot` | small |
| video playback | FFmpeg decodes the same either way; only the upload and the blit change | small |
| the window and swapchain | already written — `openmw-rtxtool` has run one since M0 | none |

And a long list of deletions: the GL interop backend, `Tracer`'s composite, the post-processor, the
rasterizer's water, sky and ripples, `osgViewer` altogether, `sdlutil`'s GL window, the shadow maps,
`pingpongcull` and the render-bin ordering. **OSG stays as a scene graph and a content loader** — the
harness has proved since M0 that it needs no GL context to be either.

The payoff is not only the blit. It is that §12 stops being a problem to solve: no interop, no frame
of latency, no world drawn twice, no cull traversal at all, and frame pacing owned rather than
inherited.

**It contradicts a standing rule and that is the decision to take, not the estimate.** `CLAUDE.md`
says the rasterizer stays working and untouched and that the RT path is a compile-time option that
is off by default. Stripping OpenGL ends both. That is a choice about what this fork is, and it
belongs to whoever is making it rather than to a plan document.

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

**M0–M10 are done. M11 is what is left in the game (§12); M12 is continuous.** What follows keeps
the decisions each milestone settled and drops the acceptance criteria they were built against.

### M0–M3 — device, mirror, visibility, textures

`SceneDesc` and a `NodeVisitor` in `openmw-rtx-bridge` that walks an `osg::Node` and emits meshes,
instances with world transforms, materials from the state-set roles, and lights. BLAS per mesh, TLAS
per frame, a ray-query compute pass. One bindless texture array, DDS blocks passed through untouched.

Two decisions from this stretch still bind. **The interop spike happened at M2, not later** — getting
a flat-shaded RT image into the game window while it was still simple enough to debug. And **ray-cone
texture LOD went in from the start**: retrofitting it is how rtxmw lost a week of caustics
(`design.md` §7.6).

### M2b — Terrain, in the harness only

In the game terrain arrives free: `Terrain::QuadTreeWorld` has already put chunks in the graph by cull
time and the mirror takes them like anything else. Headless there is no `Terrain::World` at all, so an
exterior rendered as objects floating in sky — Seyda Neen traced at 5.5% before and 79% after. An
`ESMTerrain::Storage` over `EsmData::mLands` feeds a `Terrain::TerrainGrid`, one chunk grid per cell,
no LOD. **The renderer did not change at all**, which is the mirroring argument proving itself on the
first thing it was asked to carry.

Land textures needed two things the rest of the loader does not have. A cell's blend map indexes into
the texture list of *the plugin that wrote it*, so the key is `(plugin, index)` and not a record id.
And the shading is not on the scene graph: `Terrain::TerrainDrawable` carries one alpha-blended pass
per ground texture over the same triangles, which is how a rasterizer draws a blend it cannot sample
in one go. The RT path reads the pass vector back into layers and sums them at the hit — the first
place a mirrored material is not a copy of what OSG would have drawn.

### M4 — Direct lighting and shadows

Traced shadows, with water excluded **by a mask bit rather than by a cutout test** — the any-hit
version halves the frame rate (`design.md` §7.6).

**Everything about a lamp is derived, because a `LIGH` record carries a colour and a radius and no
intensity**: the original renderer's fixed falloff supplied the brightness. Intensity comes off the
recorded radius squared and the reach off the radius stretched — Morrowind's radii light a lantern's
own post and nothing else, which worked when an ambient term lit the room and does not when the
ambient is real light.

### M5 — Sky, sun, moons

Driven by `MWWorld::Weather` and `DateTimeManager` rather than re-derived from the ini — the one place
this fork starts ahead of rtxmw (`design.md` §8.45, §8.50, §8.53, §8.59 all describe work that is a
getter here). **Sky is a light source, not a backdrop.**

The harness has no weather *simulation* — that needs `MWWorld::World` — so it takes a weather name and
an hour and steps between the four phases. Still missing: the sky *dome* — moons, clouds, the sun's
disc, stars.

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

**Two numbers the tests hold rather than eyeball.** The ten-above/ten-below transmission invariant
agrees to 3% — it is what found the missing half, because the two views are different code paths
through the same physics and cannot agree while one lights the bottom as though the water over it
were not there. And the caustic pattern is 0.366 contrast with **51.1% of it new a twelfth of a second
later**, against the reference's sweep of 73% at an 18-unit cutoff (its best, and they tear), 51% at
32 and 33% at 50. This fork cuts at 32 and lands on 51: the same spectrum reproducing the same
behaviour. Every water expectation derives from `WATER_EXTINCTION`, `WATER_IOR` and `WATER_F0` rather
than a literal beside it, so tuning is a one-line change.

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

The extinction is not the reference's eyeballed figure but `MWRender::FogManager`'s own
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

**Removal is done, and it is mark and sweep over slots.** The mirror re-walks the whole graph every
frame, so anything alive was met: `SceneExtractor::retire` drops every identity it did not find and
`SceneDesc::release` frees the slots behind it **without renumbering anything** (§10). That is not
only about memory — the identity maps are keyed on raw `osg` pointers, and an address the engine
freed when a cell unloaded can be handed straight back for something else, so without the sweep the
next thing allocated there inherits a mesh it has nothing to do with.

The harness unloads too now (`harness.md` §3.2), so the milestone's first clause is closed.

**What is left is the seam with the rasterizer**, and it is §12: the traced frame reaches the screen
one frame late, the whole world is rasterized every frame and discarded, and — the part that is a
defect rather than a cost — skinning, terrain LOD and object paging are inherited from a
view-dependent cull. Owning those three is what closes this milestone.

### M12 — Performance

SER, opacity micromaps for cutout foliage, BLAS refit for skinned actors against the double-buffered
`RigGeometry` output, BLAS compaction, cluster acceleration structures if they earn their place.
Target: 1920×1080 internal → 3840×2160 at 60 fps.

#### Where it stands

`openmw-rtxtool bench`, the `[default]` suite: 1920×1080 out of 1280×720 at DLSS quality, layers off,
600 frames of world at 60 Hz after a warm-up second, median of each row.

| place | frame | trace submit | place submit | left over | fps |
|---|---|---|---|---|---|
| Seyda Neen's ship, 7,013 instances | 8.67 ms | 4.87 | 1.87 | 1.93 | 115.3 (77.9 at the 1% low) |
| Balmora's guild of mages, 1,239 | 6.79 ms | 5.00 | 1.14 | 0.65 | 147.4 (105.0) |

**The instance counts moved as much as the times did.** Seyda Neen was 51,742 because the harness
loaded a nine-by-nine grid the game never loads; it is three-by-three now and holds what the game
holds (`harness.md` §4). The frame went from 30.26 ms to 8.67 over the same stretch of work: the
revision split, the mirror moving after the cull, the appendable texture array, one submit instead of
three for a moving frame, the instance rows built once instead of twice, and slots instead of
compaction (§10).

And the same frames as the device's own clock reports them, which is a different story:

| place | trace | upscale | refit | tlas | exposure | composite | tone | **GPU total** |
|---|---|---|---|---|---|---|---|---|
| Seyda Neen's ship | 2.30 | 2.00 | 0.43 | 0.20 | 0.05 | 0.03 | 0.02 | **5.03 ms** |
| Balmora's guild | 2.21 | 2.07 | 0.27 | 0.17 | 0.04 | 0.03 | 0.02 | **4.81 ms** |

**The CPU and the device have converged.** Five milliseconds of device work inside an 8.67 ms frame,
where it used to be 8.74 inside 30. What closed the gap was the CPU side: one submit for a moving
frame instead of three fenced ones, the instance rows built once instead of twice, the bottom-level
addresses asked for once instead of per instance per frame, and the top level's buffers kept instead
of allocated. `placeScene` is 1.87 ms where it was 11.60.

That reframes M12 again. Both places are inside the 16.7 ms budget at 1080p, and the remaining
exterior cost is split evenly between the trace and the upscaler with nothing obviously wasteful
between them. The next real number is not in this table: it is the rasterizer still drawing a world
nobody looks at, and the frame of latency that comes with it (§12).

#### What the finished features cost

Written down as each one lands rather than acted on — see `CLAUDE.md`, *Feature-complete first, then
fast*. Numbers are 1920×1080, no upscaling, validation off, best of thirty on this box.

| what | where | cost |
|---|---|---|
| the sprite layer, in the trace | Seyda Neen's ship, 165 emitters and 4,655 particles | 8.22 → 8.85 ms |
| | Balmora's guild of mages, 19 emitters | 7.34 → 7.47 ms |
| | Balmora from the bridge | 6.87 → 6.92 ms |
| the harness's live props, whole frame | Balmora in a window, 94 props | 49 → 37 fps |
| a cell arriving | nineteen crossings across the island | 47 ms of building each, none of them a rebuild (§10) |

The trace is the cheap half and the sphere test is why: an emitter is a few units across and one
rejection throws it away for almost every pixel. The window's six milliseconds are not the sprites —
they are the 185 extra deforming drawables the instanced props bring, refit once a frame whether or
not their skeletons moved. A prop's *emitters* change every frame; its geometry usually does not,
and nothing tells the two apart yet.

---

## 7. Development infrastructure

This is not overhead; it is what decides how fast the milestones above go.

### 7.1 `openmw-rtxtool`

The verbs and how to run them are in `CLAUDE.md`; what the harness *is* and where it still differs
from the game is `docs/rtx/harness.md`. Three decisions are worth keeping here.

**Headless is the primary surface.** A window costs tens of seconds of somebody's attention and
confirms almost nothing a PNG does not. `shot` renders the real renderer in about a second and prints
a summary line — hit fraction, camera, frame time — so a change is checkable without a screenshot
being looked at. `view` exists for what only a window shows: how something moves, and whether an
artefact is a still or a shimmer.

**`bench` counts seconds of *world*, not of wall clock.** The world steps a sixtieth per frame however
long that frame took, so ten seconds is six hundred frames and two builds render the *same* six
hundred — same particles in the same places, same sample in each pixel. A run against the clock would
animate further on the faster build and be measuring a different scene. Where it goes is a suite of
`views.cfg` ids, so a place worth measuring and the same place worth looking at cannot drift apart.
The validation layers are off unless asked for: a number quietly measured under instrumentation is
worse than no number.

**The game is benched too, over savegames** (`apps/rtxtool/bench.sh`), because the harness cannot see
what costs the game a frame — every renderer defect found in the M12 stretch was invisible in the
harness and obvious within seconds of measuring the game.

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

**Not `--wrap=malloc`**: it would count the driver's allocations too, which are not this renderer's
to control, and everything the frame path is forbidden to do arrives through `operator new` anyway.

**Two things had to be true before the number meant anything.** `CommandPool::submitAndWait` takes a
fresh command buffer on every call, which the driver satisfies out of the heap — setup's shape, so a
frame reuses a buffer against a fence instead. And the validation layers allocate on every command
they inspect, so the test needs a device without them (`getUnvalidatedHarness`).

Forbidden on the frame path: `std::string` construction, `push_back` into an unreserved vector,
`std::function` capture, `make_unique`, and any logging that is not compiled out.

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

Per-pass timestamp queries into a ring buffer report the device's side; `apps/rtxtool/profile.sh`
wraps `perf` for the CPU's. Two things that took a while to get right and would be re-learned
otherwise: `task-clock` rather than `cycles`, because this box's hybrid PMU splits `cycles` across
`cpu_core` and `cpu_atom` and halves every count; and a control fifo bounding the recording to the
measured frames, so what the profile attributes time to is what the report's figures came from.

---

## 8. Risks

| risk | shape | response |
|---|---|---|
| **Vanilla content assumes a rasterizer** | Sheets lit from both sides, discarded outer transforms, Z-first Euler angles, two-sided stencil (`design.md` §8.1–8.6) | Every one is diagnosed next door. Read it before debugging anything that looks like a content bug. |
| **De-lighting is a look problem, not a code problem** | No test says an albedo is right | Contact sheets and a human. Budget iteration for it. |
| **Skinned BLAS refit** | Hundreds of actors, refit per frame | Why `RigGeometry`'s CPU output is a gift and also a memcpy. Consider refitting only what moved. |

Retired: the mirror's cost and the interop stall were the two that justified measuring early, and both
were measured (§6 M12). Formatting drift is settled — `clang-format` 14, as CI pins.

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


## 10. Slots, not compaction

`SceneDesc` reclaimed by compacting: `retain` closed the gaps and `carryPlacement` renumbered what
pointed into them. Correct, and the reason a cell boundary cost a full `setScene` — nineteen of
nineteen crossings on a route across Vvardenfell, because a departing cell almost always takes a
mesh or a material with it and renumbering invalidates every acceleration structure and the whole
texture array.

**Placements already knew better.** A slot index is what a hit reads back, so closing a gap would
rename every placement above it; instead a dropped placement leaves its slot behind and the next
arrival takes it. The rest of the tables now work the same way.

### The rule

**Nothing is renumbered while the world is being walked.** A mesh, a material or a texture that
nothing wears is freed — its index goes on a free list and the entry stays where it is — and the
next arrival that fits takes the slot. Reclamation without renumbering means a crossing appends,
which is what `extendScene` was built for and could not reach.

A slot that is reused holds different geometry than it did, so it counts as an arrival: the
acceleration structure over it and the tables that describe it are rebuilt exactly as they would be
for a slot at the end. Nothing downstream has to tell the two apart.

**Travel is the exception, and it is the one the player already pays for.** Walking out of the world
you were in — a door, a fast travel, a load — replaces the scene rather than growing it, and that is
a full rebuild by design. `clear()` empties every table and the free lists with them.

### What each table costs

| table | a freed slot | reuse |
|---|---|---|
| placements | a gap in the instance table | any arrival, since a placement is fixed size |
| materials | a fixed-size record, plus a layer run and its masks that leak until the scene is replaced | any arrival |
| meshes | a **capacity** in the four shared vertex and index buffers | the best-fitting arrival, which is what keeps a large hole for a large mesh |
| textures | one slot of a bindless array | not yet — see below |

A mesh keeps the range it was given for as long as it exists, so a slot carries a capacity as well as
a count and is reused only by a mesh that fits inside it. Best fit rather than first: the free list
is what a departing ring left, which is short, and first fit spends a cathedral's hole on a crate.

### What it came to

The route in `bench --suite=streaming` flies the Bitter Coast to the Ashlands at 12,000 units a
second — nineteen cell boundaries in ten seconds of world, fast enough that what a ring costs is what
the run measures.

| | compacting | slots |
|---|---|---|
| crossings that rebuilt everything | 19 of 19 | **0 of 19** |
| worst frame | 2,808 ms | **547 ms** |
| p99 frame | 2,483 ms | 457 ms |
| crossings, total | 39.7 s | **7.3 s** |
| — of that, building | 28.5 s | **0.9 s** |
| — of that, reading content | 11.3 s | 6.4 s |
| the whole run | 56.9 s | **16.6 s** |

**Building a ring went from about 1,500 ms to about 47 ms**, and the renderer stopped being what a
crossing costs: what is left is reading the content files and instancing the models, which the game
hides behind `CellPreloader`'s threads and this harness deliberately does not.

**One bug came with it and is worth remembering.** `VulkanRenderer` decided whether to rebuild its
acceleration structures by comparing the mesh table's *size*, which cannot see a freed slot taken
over by something else. A skinned body landing in one was refitted into a bottom-level structure
that had never been made for it — a build into a null handle, reached after seventeen crossings. The
table now carries a revision that counts arrivals rather than entries, and `RtxSceneDescTest` asserts
that a reused slot bumps it.

### Where it stops, for now

**Textures are still append-only.** Reusing one means the renderer can no longer treat arrivals as a
contiguous tail of the array, which is a change to `SceneTextures`, `extendScene` and `TextureArray`
together. Until then the texture table is the one thing that only grows, and the 4,096 the array
holds is the session's bound.

**And a cell arriving still rebuilds every bottom-level structure.** The geometry those structures
were built from lives in one device buffer sized to the scene, and appending to it moves it — every
structure holds a device address into it. Appending properly needs that buffer to become blocks that
are allocated once and never moved, which is the next step and the one that turns a crossing from
"rebuild everything cheaply" into "build what arrived".


## 11. What is left, and it is three roots rather than five defects

`.notes/ISSUES.md` listed five. Traced back, they are three, and each has one answer that closes
more than one of them.

| defect | root |
|---|---|
| a cell arriving rebuilds every bottom-level structure | **A** and **C** |
| a freed material leaks its layer run and masks | **A** |
| the texture table only grows, and 4,096 is the session's bound | **C** |
| animated textures are frozen on the frame the mirror first met | **B** |
| shader water reaches the mirror with no material | **B** |

### A. Variable-length runs in shared buffers have no allocator

A mesh owns a range in four buffers, a material owns a run of layers, a layer owns a run of mask
weights — and every one of them is a variable-length span in a buffer shared by thousands. §10 solved
that for meshes with a capacity and a best-fit free list, and solved it *ad hoc*, in `addMesh`.

The same shape is what the layer runs need, and it is what the **device** geometry needs: those
buffers are sized to the scene and move when they grow, which is why every structure has to be built
again when one mesh arrives — each holds a device address into them. Give the allocator a block size
and the rule that a span never straddles a block, and the device side becomes blocks that are
allocated once and never moved.

One `SpanAllocator`, four users: CPU mesh ranges (replacing what §10 wrote by hand), layer runs,
masks, device geometry blocks.

### B. The mirror reads outside cull, and some state only exists during cull

`SceneUtil::StateSetUpdater` — which every one of OpenMW's texture, UV, alpha and material-colour
animations is — behaves two ways. As an **update** callback it swaps the node's own state set between
two copies of its own; as a **cull** callback it pushes one onto the cull visitor and never touches
the node, so a mirror running outside cull sees the original for ever. `NifOsg` picks the second for
anything marked `AnimFlag_AutoPlay`, which is what a fire or a lava flow is.

Water is out of this now — it is identified by node mask and given a material keyed on nothing, which
is also what fixed the churn and the shader-water case. Everything else autoplayed is still frozen.

**The answer is that the mirror runs the animators itself**: walk the path for the updaters, `apply`
them into a scratch state set, read the material from that. Mirroring *during* cull is the other
answer and is refused — §12.

### C. "What arrived" is a count, not a set — done for textures

`extendScene` takes the textures that arrived as a contiguous tail, and `SceneAcceleration` rebuilds
every structure because it has no way to be told which meshes are new. Both assume arrivals are the
end of a table.

They stopped being that in §10: a slot a departing cell freed is taken over by the next arrival that
fits, wherever it sits. So the scene has to report arrivals as a **list of slots** — which is what
lets a texture slot be reclaimed at all, and what lets the bottom-level build be handed the meshes
that are actually new.

**Landed for the texture table.** `TextureData` carries the slot it belongs to, `SceneDesc` keeps a
free list and a list of the slots a walk wrote, `SceneTextures` describes a set rather than a tail,
and `TextureArray::write` puts each arrival at its own element. A texture nothing wears is freed —
counted as worn by a material's own slots and by the layer runs a *live* material owns, so an
orphaned run is deliberately not allowed to speak for one, or the image would leak with the layers.

The array holds a freed slot's image until something takes the slot over, which is what keeps every
descriptor pointing at something that exists.

Flying the island route, the table now **settles at 685 textures** — 669 by the third crossing, 685
by the seventh and 685 at the end — where it used to climb past 989 and keep going. The 4,096 the
array holds has stopped being the session's bound, and texture memory settles around 11 MiB rather
than 18.

**Not yet done for meshes.** The other half of C is handing the bottom-level build the meshes that
arrived instead of all of them, and that is blocked on **A**: the geometry those structures read
lives in a device buffer that moves when it grows.

### Order

**A** next: it is what a cell arriving still costs, and it carries the layer leak and the rest of C
with it. **B** independently — it changes what the picture looks like rather than what it costs, and
it wants a frame to look at.

## 12. The seam with the rasterizer, and why the frame is one late

Two defects share one cause and neither is a flag: **the rasterizer draws the whole world every frame
and the result is thrown away**, and **the traced image on screen is always the previous frame's**.

### What actually happens in a frame

`Engine::frame` calls `mViewer->renderingTraversals()`, which culls the world and draws it into the
main camera's framebuffer object. `MWRender::PostProcessor`'s HUD camera — `POST_RENDER`, clear mask
zero — then draws the post-processing canvases and, since this fork, `Tracer`'s composite over the
top. Every pixel the rasterizer produced is covered. `traceFrame()` runs *after* the traversal
returns, so the composite that presented the image was drawn before the trace that made it: the
screen is one frame behind, and the first frame shows the rasterizer because there is nothing to
present yet.

**Measured at Seyda Neen: 2.05 ms a frame in `renderingTraversals()` against 6.2 ms of tracing.**
OSG picks `DrawThreadPerContext` — OpenMW never pins a threading model — so the draw runs on its own
thread and overlaps the trace on the CPU while competing with it for the GPU.

**Why the trace sits after the traversal**: skinned vertices, terrain LOD and which objects are paged
in are all cull-time decisions (`openmw.md` §3.4), and the mirror reads all three. Tracing before it
mirrored two different frames at once — a character's hands arrived a frame behind the arms they hang
off. That is the constraint as the code stands, and the next section is why it should not be one.

### Do we need to cull at all? No

**Not for visibility, ever.** Rays go everywhere — reflections, shadows, bounce — so anything the
camera frustum rejects still has to be in the acceleration structure. The mirror already avoids that
trap by walking the graph itself rather than the cull's results.

**What it does not avoid is cull's side effects, and inheriting those is already wrong.**
`SceneUtil::RigGeometry::accept` skins only when a `CullVisitor` reaches it, and
`getDeformedGeometry` hands back what the last cull produced. An actor outside the frustum is never
culled, so the mirror mirrors a pose frozen at whatever they were doing when last on screen — visible
today in any water reflection or shadow they are in. Terrain LOD and object paging are the same
shape: view-dependent decisions about *what exists*, taken against a rasterizer's silhouette budget.

So the target is not "skip the draw", it is **stop being downstream of cull**. Three things have to
become ours:

| what cull gives us now | where it should come from |
|---|---|
| skinned and morphed vertices | a visitor of our own over every rig, which is what the harness already does — `PosedActors` drives `RigGeometry` by hand and has no cull traversal at all |
| terrain chunk LOD | `Terrain::World` asked by distance rather than by view |
| object paging | the same, by distance |

Once those are ours the traversal has no reason to run, and the latency in §12 dissolves with it: the
trace moves to the end of the update traversal and presents in the same frame. The two halves above
become a stepping stone at most, and possibly nothing worth building.

**The harness is the existence proof.** It has no cull, poses its actors by hand, and renders the
same content correctly (`harness.md` §6).
