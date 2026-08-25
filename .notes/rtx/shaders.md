# The shaders

What the ray-traced path actually computes, where it computes it, and what to do about the places
where those two have come apart. Companion to `plan.md` (the route) and `backends.md` (the two
backends); this one is about the GLSL and the Metal, and about the shared headers both read.

Status goes in commits. What is written down here is the shape and the work that is left.

**This file is pruned as the work lands.** A step that is done is deleted along with the finding it
answered, the way `ISSUES.md` deletes a fixed issue rather than marking it — so §5 always starts at
step 1 and "the next step" is always the first one in it. What a landed step settled that still
*binds* moves up into §1 or §3, which is `plan.md`'s pattern for its finished milestones.

## 1. What is there

`components/rtx/shaders/` — ten headers, 1,378 lines, included verbatim by GLSL, C++ and Metal.
`components/rtxvulkan/shaders/` — nine entry points and a `lib/` of fifteen, 3,773 lines together.
`components/rtxmetal/shaders/` — one, 92 lines.

`visibility.comp` is 208 lines: the extensions, the includes, and `main` — the composition order and
nothing else. It is the file to read to know what a frame *is*. Everything that decides what a pixel
shows is under `lib/`, one responsibility a file:

| file | lines | what it owns |
|---|---|---|
| `fog.glsl` | 465 | `fogNoise`, `fogShape`, `fogExtinctionAt`, the phase functions, `fogAlong` |
| `sea.glsl` | 339 | the spectrum and its derivatives: `drifted`, `sampleWave`, `waterSurfaceAt`, `caustic`, the four foam functions |
| `sky.glsl` | 330 | `skyGlow`, `cloudDeck`, `skyPatches`, `starField`, `moonFace`, `skyRadiance` |
| `sprites.glsl` | 323 | `puffLight`, `spriteTaper`, `spritesAlong`, the claims |
| `water.glsl` | 301 | shading the surface: `waterRay`, `bedFall`, `WaterMirror`, `shadeWater` |
| `bindings.glsl` | 252 | the descriptor set, the buffer references, `normalAt`/`texCoordAt`/`indexAt` |
| `traversal.glsl` | 244 | `Surface`, `alphaPasses`, `trace`, `occluded` |
| `shading.glsl` | 207 | `SurfaceResponse`, `gather`, `shadeSurface`, `pathEnd`, `cosineDirection`, `bounceLight` |
| `reproject.glsl` | 157 | `movedBy`, `reprojected`, the four `*MotionOf`, `clipDepth` |
| `texturing.glsl` | 141 | `coneLod`, `sampleDiffuse`, `sampleAlbedo`, `paintedLight`, `maskWeight` |
| `random.glsl` | 79 | `hashToUnit`, the streams, `randomAt`, `unitPair` |
| `underwater.glsl` | 77 | what a column of water does to light: `daylightReaching`, `sunThroughWater`, the two transmittances |
| `lights.glsl` | 51 | `falloff`, `lampsReaching` |
| `geometry.glsl` | 49 | `triangleCross`, `triangleCorners`, `cornerWeights`, `triangleUvs`, `interpolate` |
| `footprint.glsl` | 40 | `resolved`, `pixelBlur` — what a sampler can see, asked by waves, fog and sprites |

The rest of the directory is one idea apiece and the right size for it: `atrous.comp` (128, one
wavelet level), `exposure.comp` (94), `histogram.comp` (64), `composite.comp` (64), `tone.comp` (60),
`probe.comp` (63, draws nothing), `gui.vert`/`gui.frag` (37).

### The rules that hold the split

- **Water is three files and not one, and the dependency graph decides that.** `gather` lights a
  surface below the waterline through `sunThroughWater`, and `shadeWater` needs `gather` — so a
  single `water.glsl` is a cycle. The seam that breaks it is a real one: what a column of water does
  to light crossing it has no opinion about shading and comes first; what a water *surface* does with
  a ray comes after. `sea.glsl` is the third, and is the height field alone with nothing lit in it.
- **A constant lives with the thing it is about.** `WATER_SHORE_FADE` in `water.glsl`, `FOG_GRAIN` in
  `fog.glsl`, `CLOUD_TILE` in `sky.glsl`. What crosses a seam — `SHADOW_BIAS`,
  `WATER_REFRACTION_BEND` — sits in the file that owns the idea and is inherited by include.
- **Every file includes what it uses**, behind a guard, so the include order in `visibility.comp` is
  alphabetical rather than load-bearing.
- **A bindless index is qualified where it indexes, never where it is passed.** `nonuniformEXT` on a
  function *argument* decorates the argument and stops there; the access chain built inside the
  callee comes out bare and the driver may read one lane's descriptor for the whole wave.
  `bindings.glsl` states it beside the array and counts what it cost before the rule existed.
- **The shared headers are what C++, GLSL and Metal all read**, and they may carry a function as well
  as a number: `colour.h` holds `brightest` and `encodeSrgb`, `visibility.h` holds `skyGradient`,
  `camera.h` holds `rayAt`, `coneAt` and `pixelBlur`. That is what stops the Metal backend
  re-copying what it should include, which it did for the display curve and for the sky's gradient.
  `RTX_SHADER` in `portable.h` is how the two shading languages spell such a function — `inline` for
  Metal, where two translation units could include one header, and nothing for GLSL, which has one.
  A function only GLSL can compile takes the narrower guard and says why: `camera.h`'s build their
  results with GLSL's struct constructor, which Metal does not have.
- **What every consumer of a lamp must agree on is `lampAt`.** Three places accumulate lamps and they
  differ in the cosine, the shadow ray and the phase function; the reach test and the falloff are the
  part they may not, so those are said once and each weighs the result its own way. The air and a
  puff of smoke ask the same question as each other and share `lampsAt` outright.
- **The candidate loop is a macro because it cannot be a function.** `glslc` rejects `rayQueryEXT` as
  an `out` or `inout` parameter, so a traversal cannot be handed across a call; `RTX_RESOLVE_CUTOUTS`
  is the one construct that survives the restriction, and the alternative was two copies with a
  comment on one of them warning that any change had to be made in both.
- **`Camera` carries no origin, no near and no far, and that absence is what makes it shared.** The
  trace needs the eye's place in the world; the wavelet only ever takes *differences* of
  reconstructed positions, and the origin cancels in a subtraction. `VisibilityConstants` is the eye
  and the world around it; `Camera` is how a pixel becomes a ray, and `AtrousConstants` is that plus
  three numbers of its own.
- **Floating-point addition does not associate, and `rayAt` writes its sum out rather than hoisting
  a shared term.** `f + (a - b)` and `(f + a) - b` differ in the last place, and a direction that
  differs in the last place is a hit a texel over once it has been carried thirty thousand units.
  The trace and the wavelet had already drifted that way — the trace summed left to right and the
  wavelet hoisted — so the positions the filter reconstructed were never quite the ones that were
  shaded. Measured: swapping the association changes the traced frame. `rayAt` keeps the trace's,
  because the trace is what everything else is judged against.

Compiled, `visibility.comp` is still one SPIR-V function: `glslc -O` inlines everything, and the
module is 10,988 instructions with sixteen ray-query traversal loops, forty-four texture samples and
seventeen `textureSize` queries in it — one kernel, one register budget, which is what §4.6 is about.

## 2. What the field settled on since this was written

Grounded rather than remembered. Sources at the end.

- **Reordering is a ray-generation shader's privilege.** `VK_EXT_ray_tracing_invocation_reorder`
  exposes `reorderThreadEXT` in raygen and nowhere else, deliberately — a compute shader's exposed
  workgroup layout and shared memory would need a different programming model. Ray query in a
  compute kernel cannot reorder. This is a hard architectural fact and §4.5 is about it.
- **Opacity micromaps are the other half of the same win.** Indiana Jones measured alpha-tested
  vegetation from 7.90 ms to 3.58; Alan Wake 2 measured SER and OMMs together at 39% off the ray cost
  of a dense forest. OMMs are orthogonal to the pipeline question — they work under ray query.
- **Live state is what limits both SER and occupancy.** NVIDIA's own guidance: raygen shaders
  carrying a large number of ray-tracing live-state bytes lower SER efficiency, and the fix is to
  eliminate or shrink the variables causing it. The same pressure decides a megakernel's occupancy.
- **Megakernel against wavefront is measured and close.** A 2026 Vulkan study of both in one renderer
  put wavefront ~16% ahead, and attributed the gain to memory locality rather than to raw
  utilisation. It is not a landslide either way; it depends on register pressure, divergence and how
  coarsely rays can be regrouped.
- **Ray query is the faster traversal on this hardware.** Slightly ahead of the pipeline on NVIDIA,
  substantially on AMD. So the pipeline is worth reaching for only for what it *enables* — SER,
  and a shader binding table this renderer has no use for.
- **Reservoir resampling is the standard answer to many lights.** ReSTIR bounds a pixel to one
  shadow ray however many lamps reach it, and reuses spatially and temporally on top. The 2025 work
  unifies direct and indirect into one reservoir, halves spatial reuse cost by reciprocal neighbour
  selection, and cuts spatiotemporal correlation with duplication maps.
- **Denoisers demodulate, and the industry settled on plane distance over depth gradient.** NRD's
  ReLAX is an à-trous cascade with a temporal history and a variance estimate; the demodulation
  contract is `diffuse / albedo` and `specular / envBRDF`, remodulated after. This renderer's
  demodulation is already right, including the argument for why water's specular albedo is the
  Fresnel share and not an environment BRDF.

## 3. What the shaders get right, so it does not get refactored away

Naming this first because a reorganisation is exactly where it gets lost.

- **Ray cones from the start.** `coneLod` is Akenine-Möller's formulation with the grazing-angle
  stretch, and `resolved` uses the same footprint to fade a wave, a fog octave and a sprite rim. One
  idea of what a sampler can see, applied everywhere.
- **The demodulation contract is correct and unusually well argued.** Albedo without the path in it,
  water and air folded into the light rather than the surface, Fresnel as the specular albedo, total
  internal reflection reporting one rather than Schlick's 0.024, the wave's normal in the guide.
- **One height field, differentiated twice.** The wave normal and the caustic Hessian come off the
  same spectrum at the same drifted coordinate. The alternative — two fields — is the classic way
  light lands where the surface is not.
- **Every constant is derived and says what it was measured against.** `WATER_REFRACTION_BEND` from
  `WATER_IOR`, `FOG_SPREAD` as `sum(a²)/sum(a)²`, `FOG_COVERAGE` measured with a standard error
  quoted, the Jensen correction on the caustic determinant with its before-and-after.
- **The shared headers are one fact per number**, pinned by `static_assert` on every side.

## 4. Findings

Ordered as `CLAUDE.md` orders them: how it looks, then performance — with plain correctness ahead of
both, because a wrong pixel is not a trade.

### 4.1 The bounce has a firefly tail, and no way to clamp it that is not a picked number

Measured on the module this tree builds, as the share of pixels whose one-sample bounce luminance
exceeds a threshold:

| threshold | Seyda Neen ship | Balmora | customs office | canalworks |
|---|---|---|---|---|
| 0.5 | 10.8% | 4.9% | 0.046% | 0% |
| 1 | 0.0037% | 0.0002% | 0.022% | 0% |
| 2 | 0.0009% | 0.0002% | 0.0016% | 0% |
| 8 | 0% | 0.0001% | 0.0004% | 0% |
| 32 | 0% | 0.0001% | 0.0002% | 0% |
| 64 | 0% | 0% | 0% | 0% |

**The signal ends at about one and the tail is a handful of pixels.** Outdoors the bulk of the bounce
sits under 0.5 and falls off a cliff at 1, which is a surface seeing a full hemisphere of sky; above
that is two to thirty-four pixels of nine hundred thousand, reaching somewhere between 32 and 64. The
interior is the awkward one — two hundred pixels above 1, which is a chandelier doing its job and not
obviously separable from a bounce that landed on one.

`bounceLight` is one cosine-weighted sample, and the à-trous cascade has no variance estimate to
reject an outlier with, so it spreads each of those over sixty-two pixels rather than removing it.

**There is no constant here that can be derived rather than picked, which is why this is not fixed
yet.** The renderer's own scale would give one — `DAYLIGHT`, `EMISSIVE_INTENSITY` — but the value a
bounce can legitimately reach is set by the brightest lamp in the cell, and a lamp's intensity is
content: `falloff` is `window² / (d² + 1)`, so a bounce landing on a lamp returns that lamp's
intensity whatever it is. Any absolute ceiling is a number somebody chose, and a modded cell moves it.

**The clamp wants to be relative, and 4.2 is what makes that possible.** Against a running mean a
sample can be rejected for being far from what the same pixel has been seeing, with no scene-
independent constant anywhere in it. So this is folded into 4.2 rather than standing alone.

**A NaN was looked for and is not there.** Every channel the trace writes was instrumented and
counted across seven views and two storms: no NaN and no infinity, anywhere. Nor should there be —
the tree already answers untrusted content at the boundary, in `describeClouds`, `fogbuilder`,
`shadingmap`, `lightbuilder` and `sceneextractor`, each written as `!(x > 0)` so that a NaN lands on
the safe side, and `Rtx::Camera` guards the normalised zero vector that would otherwise fill an image
with them. **That is where this belongs and it is already done.** A guard at the five `imageStore`s
was written and measured at sixty instructions a pixel — twenty comparisons, twenty selects, twenty
`abs` — to catch a fault that has never occurred and whose source is validated one cell at a time
rather than one pixel at a time. It was taken out again.

### 4.2 The temporal half is built and its quality win is not yet measured

`AccumulatePass` runs in front of the cascade whenever the wavelet does, and `atrous.comp` has
SVGF's third edge-stopping term for the first time. What is in place:

- The indirect channel is reprojected through the motion vector the trace already writes, bilinearly
  over the four pixels it lands between, each tap taken only if its stored normal and distance say
  it is the same surface.
- A history length per pixel, so the blend is an exact mean while the history is short and an
  exponential one past `ACCUMULATE_FRAMES`.
- First and second moments, and the variance the cascade weighs a tap by. A pixel with no history
  carries the largest variance there is rather than nought — `E[l²] - E[l]²` over one sample is
  exactly zero, which a filter reads as *certain* and is the opposite of the truth.
- The firefly clamp 4.1 asked for, at `ACCUMULATE_SIGMAS` from the running mean, with no constant in
  it that is about this game.
- `biasMask` and a lost history are the two reset signals, and `historyLost` is now one expression
  that both denoisers read.

**What is not established is that it makes the picture better.** The scene the filter tests use — a
grazing sheet under a smooth sky — is already converged by the cascade alone: one filtered frame
lands within a third of a byte of a sixty-four-sample reference, so there is nothing left for a
history to take. Five levels of à-trous have every advantage there, because the signal is uniform and
every neighbour is a valid sample of it.

So the test asserts what it can: that sixteen accumulated frames do not cost what the cascade gained,
and that the accumulated mean sits on the converged one rather than drifting off it — an average that
dimmed the frame would be quieter and wrong.

**The measurement wants a scene the cascade struggles with**: contact regions, small geometry, and
pixels with few neighbours looking at the same thing. That is the number this entry is still missing,
and it is what closes it.

### 4.3 Direct lighting is unbounded in the number of lamps

`gather` walks every lamp the grid cell holds and spends a shadow ray on each that passes two cheap
tests. The grid bounds *which* lamps, not *how many* — a Balmora interior with a dozen
candles is a dozen shadow rays a pixel, and the cost is per-pixel rather than per-frame.

RIS with one reservoir bounds it to one, and ReSTIR's temporal and spatial reuse then makes that one
sample worth more than the dozen were. The renderer already has the two things that make it cheap:
the light grid gives a bounded candidate set to resample from, and the motion vectors give a
temporal neighbour.

This is sequenced *after* 4.2, because a reservoir needs a history to reuse and the history is what
4.2 builds.

### 4.4 The same computation is written more than once

Each of these is two or three places that must agree and are not enforced to.

| written | where | how they differ |
|---|---|---|
| the transformed UVs | `texturing.glsl`, in `sampleDiffuse` and in `sampleAlbedo` | none — recomputed on purpose, six multiplies |
| the wave-spectrum loop | `sea.glsl`, in `waterSurfaceAt` and in `caustic` | first and second derivative of one field |
| `0xFFFFFFFF` | `NO_TEXTURE`, `NO_SKY_TEXTURE`, `NO_MOON_FACE` | nothing; three names in two headers |

**The wave loops are not redundant and must not be merged blindly.** `waterSurfaceAt` is evaluated
at the surface the ray met; `caustic` is evaluated at the bed the light landed on. Different points,
so one loop cannot serve both. What *is* shared is the structure — `drifted`, `sampleWave`, and a
phase whose `sin` one caller wants and whose `cos` the other does — and it belongs in one place
where a change to the spectrum reaches both.

### 4.5 SER is unreachable from the current architecture

`plan.md` §8 lists SER as an unstarted M12 item. It cannot be started as things stand:
`reorderThreadEXT` exists in ray generation shaders only, by deliberate design of the extension, and
this renderer traces from a compute shader with `GL_EXT_ray_query`.

That is not an argument for moving. Ray query is the faster traversal on this hardware, the megakernel
keeps everything in registers with no payload hand-off, and there is no shader binding table worth
having here. It is an argument for the M12 entry to say what it costs: **SER requires a ray-tracing
pipeline, and the decision is whether the reordering is worth the rewrite.** The way to answer it is
to shrink live state first (4.6) and measure occupancy, because that is where most of what SER
recovers already is.

Opacity micromaps are the item to take from M12 instead. They work under ray query, this shader's
`alphaPasses` runs on every candidate of every cutout, and the published measurements are large.

### 4.6 One kernel, one register budget

The megakernel holds live simultaneously, in `main`: a `Surface` (two bools, four `vec3`, two floats,
a `uint`), a `SurfaceResponse`, a `WaterMirror`, a `SpriteLayer` with two `SpriteClaim`s, a `vec4`
of fog, a transmittance, and `beforeParticles`. Underneath it, `shadeWater` holds a `WaterSurface`
and two `WaterPath`s across three more traversals.

Sixteen inlined traversal loops share that budget. The pixel that pays the most — shallow water with
foam over it — runs a primary trace, a reflection, a refraction, a `bedFall` probe, a sun shadow ray,
a lamp shadow ray each, eight fog shadow rays and a bounce, all in one register allocation.

Nothing here should be *acted* on before it is measured — that is the `CLAUDE.md` rule and it holds.
What can be done now is make it measurable: the split is what lets a variant be compiled and
compared — swapping a lib file for a cheaper one is now a one-line edit — and Nsight's occupancy
figure against `visibility.comp` is the one number that decides whether 4.5 is worth asking.

### 4.7 The sprite march is O(emitters) per pixel with no binning

`spritesAlong` loops over every emitter in the scene for every pixel, rejecting each with a
ray-sphere test. Measured in `plan.md`: 165 emitters at Seyda Neen. At 1920×1080 that is 340 million
sphere tests a frame to find the few that matter.

The comment defends the sphere test, and it is right that one rejection is cheap. It is a per-pixel
cost against a per-frame answer: which emitters a *tile* can see is the same question the light grid
already answers for lamps, computed once per tile instead of once per pixel.

### 4.8 The à-trous cascade re-reads its guides per tap

Twenty-five taps a level, five levels, each doing two `imageLoad`s and a `normalize` inside
`positionAt`. That is 250 image loads and 125 normalizes per pixel, with a 5×5 neighbourhood whose
guides every thread in the workgroup shares.

The standard shape is one shared-memory tile per workgroup, loaded once. Whether it is worth it
depends on 4.2 — if the cascade becomes the spatial half of a temporal denoiser, its level count
usually falls, and the tile is worth more per level.

### 4.9 Small things

- `visibility.comp`, in `main` — `atomicAdd(hits, 1)` is telemetry on the release path. Cheap, and
  measured as such where the buffer is declared, but it is a debug facility compiled into the
  shipping kernel.
- The random streams are split across two files: `RANDOM_STREAMS` and `BLUE_NOISE_EXTENT` are shared
  in `scene.h`, where C++ generates the tile; `STREAM_FOG`, `STREAM_BOUNCE` and `STREAM_TURN` are in
  `random.glsl`. The count is a promise the stream ids have to keep, and a second shader that drew
  would have to know the ids to avoid.

## 5. Steps

Each step is checkable on its own and leaves the tree working. The check is named because *"it
compiled"* is not one. As one lands it is deleted from here, so step 1 is always what to do next.

**The byte comparison is the check for anything that must not change a pixel.** `openmw-rtxtool shot`
is deterministic — `mFrame` and `mTime` are zero under it and every draw is keyed on them — so an
identical frame is an identical file and one differing byte means something moved that should not
have. Nine views cover the cases that matter:

```sh
for v in seyda-neen-ship seyda-neen-shore seyda-neen-customs vivec balmora \
         sadrith-mora vivec-canalworks dagon-fel wolverine-hall; do
    ./openmw-rtxtool shot --view=$v --out=/tmp/before-$v.png
done
#  … change …
cmp /tmp/before-$v.png /tmp/after-$v.png
```

**Both sides must pass the same `--validation`.** DLSS is handed a wall-clock frame delta, and the
layers change the timing enough to change the upscaled frame — a validation-on baseline against
validation-off shots reads as 14% of channels differing with nothing wrong at all. Use
`--validation=0` on both, which is also four times faster.

`map` and `doll` are **not** deterministic — the map runs about 0.11% of channels apart between two
runs of one build, the doll about 0.02% — so those two need a magnitude compared against a same-build
control, never `cmp`. Both are worth running anyway: `map` is the only orthographic path and `doll`
the only transparent-background one.

**1 — measure what the accumulator is worth (4.2).** It is built and it runs; what no test shows is
a quality win, because the scene the filter tests use is already converged by the cascade alone.
*Check:* a fixture the cascade does poorly on — a lit corner, or geometry small enough that few
neighbours share a surface — and the RMSE of one filtered frame against a converged reference,
against the same with a settled history. The firefly half has its own: the share of pixels above the
thresholds in 4.1, which must fall without the mean moving.

**2 — bound the shadow rays (4.3).** RIS over the grid cell's candidates, one reservoir, one shadow
ray. Then temporal reuse through step 1's history, then spatial. *Check:* the same RMSE-against-
reference metric, in an interior with a dozen lamps — and a count of shadow rays per pixel, which is
the thing being bounded.

**3 — opacity micromaps.** The device features are already required and probed; nothing builds a
micromap. `alphaPasses` is what stops being invoked. *Check:* the trace timer on a view of foliage,
which is what M12 measures.

**4 — bin the emitters (4.7).** A tile pass over emitter spheres, written before the trace, read as
a range the way the light grid is. *Check:* the trace timer at Seyda Neen with 165 emitters, and the
byte comparison — binning must not change a pixel.

**5 — decide SER (4.5).** After 1 and 2 have changed the live-state picture. Measure occupancy on
`visibility.comp`; if it is where the megakernel is losing, price the ray-tracing pipeline. Until
then, `plan.md` §8's SER entry should say it needs one.

1 and 2 are what the frame looks like. 3–5 are M12, and each gets its number written down when it
lands rather than acted on before.

## 6. Sources

- [VK_EXT_ray_tracing_invocation_reorder proposal](https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_ray_tracing_invocation_reorder.html) — why reordering is raygen-only
- [Khronos: Boosting Ray Tracing Performance with Shader Execution Reordering](https://www.khronos.org/blog/boosting-ray-tracing-performance-with-shader-execution-reordering-introducing-vk-ext-ray-tracing-invocation-reorder)
- [Path Tracing Optimizations in Indiana Jones: Opacity MicroMaps and BLAS compaction](https://developer.nvidia.com/blog/path-tracing-optimizations-in-indiana-jones-opacity-micromaps-and-compaction-of-dynamic-blass/)
- [Path Tracing Optimization in Indiana Jones: SER and live-state reductions](https://developer.nvidia.com/blog/path-tracing-optimization-in-indiana-jones-shader-execution-reordering-and-live-state-reductions/)
- [Megakernel vs Wavefront GPU Path Tracing](https://arxiv.org/pdf/2605.27323)
- [Vulkan Documentation Project: Ray Tracing](https://docs.vulkan.org/guide/latest/extensions/ray_tracing.html) — ray query against the pipeline
- [NVIDIA-RTX/NRD](https://github.com/NVIDIA-RTX/NRD) — ReBLUR, ReLAX, the demodulation contract
- [Real-Time Denoising: SVGF, A-SVGF, DLSS & ReLAX](https://www.mysimulator.uk/content/articles/realtime-denoising.html)
- [ReSTIR PT Enhanced](https://dl.acm.org/doi/10.1145/3804494) — reciprocal neighbour selection, duplication maps
- [ReSTIR GI: Path Resampling for Real-Time Path Tracing](https://blog.zcy.moe/en/blog/restir-gi/)
- [Spatiotemporal reservoir resampling for dynamic direct lighting](https://dl.acm.org/doi/abs/10.1145/3386569.3392481)
- [NVIDIA-RTX/RTXPT](https://github.com/NVIDIA-RTX/RTXPT) — reference real-time path tracing library
