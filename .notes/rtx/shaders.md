# The shaders

What the ray-traced path actually computes, where it computes it, and what to do about the places
where those two have come apart. Companion to `plan.md` (the route) and `backends.md` (the two
backends); this one is about the GLSL and the Metal, and about the shared headers both read.

Status goes in commits. What is written down here is the shape and the steps.

## 1. What is there

`components/rtx/shaders/` — ten headers, 1,360 lines, included verbatim by GLSL, C++ and Metal.
`components/rtxvulkan/shaders/` — nine modules, 3,505 lines.
`components/rtxmetal/shaders/` — one, 92 lines.

| file | lines | what it is |
|---|---|---|
| `visibility.comp` | **2,995** | the whole renderer |
| `atrous.comp` | 128 | one wavelet level |
| `exposure.comp` | 94 | histogram → one number |
| `histogram.comp` | 64 | frame → 256 bins |
| `composite.comp` | 64 | channels → one picture |
| `tone.comp` | 60 | radiance → bytes |
| `probe.comp` | 63 | device-behaviour test, draws nothing |
| `gui.vert` / `gui.frag` | 37 | pass-through |
| `visibility.metal` | 92 | primary hit and a sky gradient |

Eight of those nine are one idea apiece and are the right size for it. The ninth is the renderer.

### What `visibility.comp` holds

Seventy-six functions and `main`, spanning thirteen distinct responsibilities:

| lines | responsibility |
|---|---|
| 1–410 | descriptor set, buffer references, thirty file-scope constants |
| 411–570 | triangle attributes, ray-cone LOD, bindless sampling, de-lighting, terrain masks |
| 572–920 | the wave spectrum, its two derivatives, surf placement, caustics |
| 922–1022 | the light grid, the alpha cutout, the shadow ray, the falloff window |
| 1024–1083 | the blue-noise tile and its per-frame turn |
| 1085–1423 | value noise, domain warp, coverage band, Mie phase, the volumetric march |
| 1425–1492 | direct lighting |
| 1494–1804 | cloud deck, star sheet, nebulae, two moons, the sun's disc |
| 1806–2030 | traversal, `Surface`, the Lambert model, `SurfaceResponse` |
| 2032–2311 | water shading: Fresnel, reflection, refraction, absorption, foam |
| 2313–2374 | the cosine-weighted bounce |
| 2376–2655 | particle systems, order-independent compositing, puff lighting |
| 2657–2822 | reprojection: surfaces, sprites, mirrors, sky, clip depth |
| 2824–2995 | `main` — the composition order |

Compiled, it is one SPIR-V function of 17,867 disassembled lines: `glslc -O` inlines everything.
Sixteen ray-query traversal loops, forty-four texture samples, seventeen `textureSize` queries,
fifty-one loops, four hundred and seventeen branch merges — in one kernel, with one register budget.

**GLSL `#include` already works here.** Every shader declares `GL_GOOGLE_include_directive`, the
build passes `-I` at `RTX_SHADER_INCLUDE` and `-MD` so ninja has the include graph. Nothing about the
toolchain is what keeps this file whole.

## 2. What the field settled on since this was written

Grounded rather than remembered. Sources at the end.

- **Reordering is a ray-generation shader's privilege.** `VK_EXT_ray_tracing_invocation_reorder`
  exposes `reorderThreadEXT` in raygen and nowhere else, deliberately — a compute shader's exposed
  workgroup layout and shared memory would need a different programming model. Ray query in a
  compute kernel cannot reorder. This is a hard architectural fact and §4.6 is about it.
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

### 4.1 Bindless indices lose their `nonuniformEXT` inside the two hottest samplers

`coneLod` and `sampleDiffuse` take the texture slot as a plain `uint` and index `textures[slot]` bare
(`visibility.comp:453`, `:492`). Every caller applies the qualifier at the *call site* —
`sampleDiffuse(nonuniformEXT(material.mDiffuse), …)` — so glslang decorates the argument and not the
access chain built inside the callee.

Measured on the module this tree builds:

```
OpAccessChain into textures[]      44
  decorated NonUniform            16
  not decorated                   28
```

The sixteen are the sites that index inline — the cloud deck, the star sheet, the sky patches, the
moon faces, the two sprite reads. The twenty-eight are every surface albedo, every terrain layer and
every alpha-cutout test: the paths where the index is *most* divergent, because neighbouring lanes
hit different materials.

Without the decoration the driver may treat the descriptor index as wave-uniform and skip the
waterfall loop, which reads one lane's descriptor for the whole wave. That is the quietly-wrong
failure mode: it will look right in most frames, at most camera angles, on most materials.
`spirv-val` does not catch it and the validation layers do not either.

**Fix:** qualify at the point of use, not at the call site — `textures[nonuniformEXT(slot)]` inside
both functions. The call sites can then drop theirs. One line each, and it makes the rule statable:
*a bindless index is qualified where it indexes, never where it is passed.*

### 4.2 Nothing guards against a NaN or a firefly reaching the history

There is no `isnan`, no `isinf` and no clamp on any radiance channel before `imageStore`
(`main`, 2932–2945). Three ways in:

- `bounceLight` is one cosine-weighted sample. A bounce landing on an emissive texel or a unit from a
  lamp returns a value orders above its neighbours, and the à-trous cascade spreads it over
  sixty-two pixels rather than removing it — it has no variance estimate to reject it with.
- `caustic` is floored at `1/WATER_CAUSTIC_MAX` and multiplied by a Jensen gain, but `sunThroughWater`
  multiplies it by an `exp` whose argument is a path divided by `max(-downward.z, 0.05)`.
- Anything in the scene tables that arrives as a NaN — a zero-length normal, a degenerate transform
  — propagates through `normalize` and lands in `guide`.

A NaN in the radiance channel does not stay in one frame: DLSS Ray Reconstruction accumulates it, and
`mReset` is only raised on a camera jump.

**Fix:** one `sanitised()` in the shared colour header, applied at each of the five `imageStore`s
that write radiance or a guide — `mix(vec3(0), c, equal(c, c))` for the NaN half and a
percentile-derived ceiling for the firefly half. The ceiling has to be *measured* and written down
rather than picked, the way `FOG_COVERAGE` was: the honest form is a multiple of the frame's own
exposure, which `exposure.comp` already computes.

### 4.3 The indirect signal has no temporal dimension at all

`atrous.comp` is five spatial levels with no history buffer, no reprojection and no variance — its
own header says so: *"SVGF's luminance term is missing outright: it weighs by the variance of an
accumulated history, and there is no history here yet."*

So the two paths through the frame are:

- **Ray Reconstruction on** — the wavelet is skipped entirely (`vulkanrenderer.cpp:637`), and NGX is
  handed a raw one-sample-per-pixel bounce.
- **Ray Reconstruction off** — one sample per pixel, blurred by a 125-tap cascade with no way to tell
  a bright pixel from a wrong one.

Both are the same missing piece. Every denoiser the field settled on — SVGF, A-SVGF, ReLAX, ReBLUR —
is a *temporal* accumulator with a spatial cascade attached, and the spatial part exists to fill in
where the temporal part was rejected. This renderer has the second half without the first.

It already owns every input the first half needs: motion vectors for surfaces, sprites, mirrors and
sky; a bias mask saying where the past is not worth carrying; a plane-distance edge test; and a
`GBUFFER_DEPTH` carrying both a clip value and a world distance.

**This is the largest single lever on how it looks**, and it is the one the priority order puts
first.

### 4.4 Direct lighting is unbounded in the number of lamps

`gather` walks every lamp the grid cell holds and spends a shadow ray on each that passes two cheap
tests (`1455–1478`). The grid bounds *which* lamps, not *how many* — a Balmora interior with a dozen
candles is a dozen shadow rays a pixel, and the cost is per-pixel rather than per-frame.

RIS with one reservoir bounds it to one, and ReSTIR's temporal and spatial reuse then makes that one
sample worth more than the dozen were. The renderer already has the two things that make it cheap:
the light grid gives a bounded candidate set to resample from, and the motion vectors give a
temporal neighbour.

This is sequenced *after* 4.3, because a reservoir needs a history to reuse and the history is what
4.3 builds.

### 4.5 The same computation is written more than once

Each of these is two or three places that must agree and are not enforced to.

| written | where | how they differ |
|---|---|---|
| the lamp loop | `gather:1455`, `fogLight:1312`, `puffLight:2394` | cosine and shadow ray; `INV_FOUR_PI`; neither |
| the ray-query candidate loop | `occluded:994`, `trace:1867` | `trace` carries a cone, `occluded` does not |
| the ray generator | `visibility.comp:2832`, `atrous.comp:62` | none — `atrous.comp` exists to rebuild it exactly |
| the transformed UVs | `sampleDiffuse:487`, `sampleAlbedo:536` | none — recomputed on purpose, six multiplies |
| `encodeSrgb` | `tone.comp:41`, `visibility.metal:26` | none — "matching term for term" |
| `skyGlow` | `visibility.comp:1500`, `visibility.metal:39` | none |
| the wave-spectrum loop | `waterSurfaceAt:685`, `caustic:850` | first and second derivative of one field |
| `0xFFFFFFFF` | `NO_TEXTURE`, `NO_SKY_TEXTURE`, `NO_MOON_FACE` | nothing; three names in two headers |

Two deserve their own note.

**The ray generator is duplicated by design and `AtrousConstants` is the evidence.** That struct
carries `mForward`, `mRight`, `mUp`, `mOrthographic`, `mWidth`, `mHeight`, `mJitter` and
`mSpreadAngle` — eight fields copied from `VisibilityConstants` for no reason but that
`atrous.comp` has to rebuild the trace's rays *exactly*, jitter and projection included, or the
positions it reconstructs are not the ones that were shaded. Its own comment says as much. A shared
`Camera` sub-struct and one `rayAt()` both files include is the same fact stated once.

**The candidate loop cannot be a function and can be a macro.** The comment at `972` is right that
`glslc` rejects `rayQueryEXT` as an `out` or `inout` parameter, and it draws the correct conclusion —
*"any change to the cutout has to be made in both places"* — which is a standing invitation to a bug.
A preprocessor macro over the loop body is the one construct that survives the restriction, and both
sites then read the same text.

**The wave loops are not redundant and must not be merged blindly.** `waterSurfaceAt` is evaluated
at the surface the ray met; `caustic` is evaluated at the bed the light landed on. Different points,
so one loop cannot serve both. What *is* shared is the structure — `drifted`, `sampleWave`, and a
phase whose `sin` one caller wants and whose `cos` the other does — and it belongs in one place
where a change to the spectrum reaches both.

### 4.6 SER is unreachable from the current architecture

`plan.md` §8 lists SER as an unstarted M12 item. It cannot be started as things stand:
`reorderThreadEXT` exists in ray generation shaders only, by deliberate design of the extension, and
this renderer traces from a compute shader with `GL_EXT_ray_query`.

That is not an argument for moving. Ray query is the faster traversal on this hardware, the megakernel
keeps everything in registers with no payload hand-off, and there is no shader binding table worth
having here. It is an argument for the M12 entry to say what it costs: **SER requires a ray-tracing
pipeline, and the decision is whether the reordering is worth the rewrite.** The way to answer it is
to shrink live state first (4.7) and measure occupancy, because that is where most of what SER
recovers already is.

Opacity micromaps are the item to take from M12 instead. They work under ray query, this shader's
`alphaPasses` runs on every candidate of every cutout, and the published measurements are large.

### 4.7 One kernel, one register budget

The megakernel holds live simultaneously, in `main`: a `Surface` (two bools, four `vec3`, two floats,
a `uint`), a `SurfaceResponse`, a `WaterMirror`, a `SpriteLayer` with two `SpriteClaim`s, a `vec4`
of fog, a transmittance, and `beforeParticles`. Underneath it, `shadeWater` holds a `WaterSurface`
and two `WaterPath`s across three more traversals.

Sixteen inlined traversal loops share that budget. The pixel that pays the most — shallow water with
foam over it — runs a primary trace, a reflection, a refraction, a `bedFall` probe, a sun shadow ray,
a lamp shadow ray each, eight fog shadow rays and a bounce, all in one register allocation.

Nothing here should be *acted* on before it is measured — that is the `CLAUDE.md` rule and it holds.
What can be done now is make it measurable: the file split (§5) is what lets a variant be compiled
and compared, and Nsight's occupancy figure against `visibility.comp` is one number that decides
whether 4.6 is worth asking.

### 4.8 The sprite march is O(emitters) per pixel with no binning

`spritesAlong` loops over every emitter in the scene for every pixel (`2501`), rejecting each with a
ray-sphere test. Measured in `plan.md`: 165 emitters at Seyda Neen. At 1920×1080 that is 340 million
sphere tests a frame to find the few that matter.

The comment defends the sphere test, and it is right that one rejection is cheap. It is a per-pixel
cost against a per-frame answer: which emitters a *tile* can see is the same question the light grid
already answers for lamps, computed once per tile instead of once per pixel.

### 4.9 The à-trous cascade re-reads its guides per tap

Twenty-five taps a level, five levels, each doing two `imageLoad`s and a `normalize` inside
`positionAt`. That is 250 image loads and 125 normalizes per pixel, with a 5×5 neighbourhood whose
guides every thread in the workgroup shares.

The standard shape is one shared-memory tile per workgroup, loaded once. Whether it is worth it
depends on 4.3 — if the cascade becomes the spatial half of a temporal denoiser, its level count
usually falls, and the tile is worth more per level.

### 4.10 Small things

- `visibility.comp:1972–1985` — `shadeSurface`'s doc comment has been merged into
  `SurfaceResponse`'s. The `@param incoming` paragraph now documents a struct, and `shadeSurface`
  has no comment at all. Stale narration in a file that is otherwise scrupulous about it.
- `visibility.comp:2925` — `atomicAdd(hits, 1)` is telemetry on the release path. Cheap, and measured
  as such at the top of the file, but it is a debug facility compiled into the shipping kernel.
- `visibility.metal` is at M0's feature level while `visibility.comp` is at M11's. That is the
  *stated* policy — each machine develops its own backend — but the two share `skyGlow` and
  `encodeSrgb` by copy, so the copies drift with no compiler to say so. Whatever is genuinely
  API-neutral belongs in `components/rtx/shaders/` where both include it, which is exactly what the
  Metal file's own header comment says is coming.
- The random streams are split across two headers: `RANDOM_STREAMS` and `BLUE_NOISE_EXTENT` are
  shared in `scene.h`, `STREAM_FOG`, `STREAM_BOUNCE` and `STREAM_TURN` are private to
  `visibility.comp`. The count is a promise the stream ids have to keep, and a second shader that
  draws would have to know the ids to avoid.

## 5. The split

By responsibility, into `components/rtxvulkan/shaders/lib/`. Nothing moves *between* responsibilities
and nothing changes what is computed — this step is mechanical and is verified as such (§6, step 3).

| file | what it owns |
|---|---|
| `bindings.glsl` | the descriptor set, the buffer references, `normalAt`/`texCoordAt`/`indexAt` |
| `camera.glsl` | ray generation for both projections — **shared with `atrous.comp`** |
| `footprint.glsl` | `resolved`, `pixelBlur` — what a sampler can see, asked by waves, fog and sprites |
| `random.glsl` | `hashToUnit`, the streams, `randomAt`, `unitPair` |
| `geometry.glsl` | `triangleCross`, `triangleCorners`, `cornerWeights`, `triangleUvs`, `interpolate` |
| `texturing.glsl` | `coneLod`, `sampleDiffuse`, `sampleAlbedo`, `paintedLight`, `maskWeight` |
| `traversal.glsl` | `Surface`, `alphaPasses`, the candidate-loop macro, `trace`, `occluded` |
| `lights.glsl` | `falloff`, `lampsReaching`, the one lamp accumulator the three loops share |
| `sky.glsl` | `skyGlow`, `cloudDeck`, `skyPatches`, `starField`, `moonFace`, `skyRadiance` |
| `sea.glsl` | the spectrum and its derivatives: `drifted`, `sampleWave`, `waterSurfaceAt`, `caustic`, the four foam functions |
| `water.glsl` | shading it: `daylightReaching`, `sunThroughWater`, transmittance, `waterRay`, `bedFall`, `shadeWater` |
| `fog.glsl` | `fogNoise`, `fogShape`, `fogExtinctionAt`, the phase functions, `fogAlong` |
| `shading.glsl` | `SurfaceResponse`, `gather`, `shadeSurface`, `pathEnd`, `cosineDirection`, `bounceLight` |
| `sprites.glsl` | `puffLight`, `spriteTaper`, `spritesAlong`, the claims |
| `reproject.glsl` | `movedBy`, `reprojected`, the four `*MotionOf`, `clipDepth` |

`visibility.comp` is then `main` and the composition order — the fourteen lines that say the sky goes
behind the water, the water under the fog, the fog under the sprites, and what each channel gets. It
is about 170 lines, and it is the file to read to know what a frame *is*.

**Two rules that make the split hold:**

- **A constant lives with the thing it is about.** `WATER_SHORE_FADE` in `water.glsl`, `FOG_GRAIN` in
  `fog.glsl`, `CLOUD_TILE` in `sky.glsl`. What two responsibilities share — `SHADOW_BIAS`,
  `WATER_BIAS`, `NO_TEXTURE_ALBEDO` — moves into the C++-shared headers under
  `components/rtx/shaders/`, which is where a number two sides must agree on already goes.
- **The shared headers keep their job and gain one.** They are what C++ and GLSL and Metal all read.
  `colour.h` grows `encodeSrgb`, `brightest` and the luminance it already holds the weights for —
  guarded so the host side gets none of the GLSL. That is what stops the Metal backend re-copying
  them.

## 6. Steps

Each step is checkable on its own and leaves the tree working. The check is named because *"it
compiled"* is not one.

**1 — `nonuniformEXT` at the point of use.** `visibility.comp:453` and `:492`; drop the qualifier
from the four call sites that now hand it in (`965`, `1937`, `1943`, `1950`). *Check:* rebuild and count — all 44 access chains into
`textures[]` decorated, against 16 today:
```sh
spirv-dis build/resources/rtx/shaders/visibility.comp.spv > /tmp/v.spvasm
grep -c 'OpAccessChain %_ptr_UniformConstant_[0-9]* %textures' /tmp/v.spvasm
```

**2 — the stale doc comment at `1972`.** Give `shadeSurface` its `@param` back and leave
`SurfaceResponse` its own. *Check:* read it.

**3 — the split (§5), mechanical.** Move text; change nothing. *Check:* `openmw-rtxtool shot` on
three views before and after, compared **byte for byte** — `mFrame` and `mTime` are zero under `shot`
and every draw is keyed on them, so an identical frame is an identical file. A single differing byte
means something moved that should not have.

```sh
./openmw-rtxtool shot --view=balmora  --out=/tmp/before-balmora.png
./openmw-rtxtool shot --view=seydaneen --out=/tmp/before-seyda.png
./openmw-rtxtool shot --view=balmora --weather=Rain --out=/tmp/before-rain.png
# … split …
cmp /tmp/before-balmora.png /tmp/after-balmora.png
```

**4 — one ray generator.** A `Camera` sub-struct in a shared header, `rayAt()` in `camera.glsl`,
`AtrousConstants` reduced to its own three fields plus that struct. *Check:* step 3's byte comparison
again, plus a map tile (`openmw-rtxtool map`) — the orthographic path is the one the duplication was
most likely to have got wrong.

**5 — one lamp loop, one candidate loop, one sRGB curve, one sky gradient.** The lamp accumulator
takes what differs as parameters; the candidate loop becomes a macro; `encodeSrgb` and `skyGlow`
move into `components/rtx/shaders/` and the Metal file includes them. *Check:* byte comparison, and
`components-tests` — the water and fog suites read the constants these touch.

**6 — sanitise the radiance channels (4.2).** NaN rejection at the five `imageStore`s; the firefly
ceiling derived from the measured exposure rather than picked, and the derivation written into the
comment beside it the way `FOG_COVERAGE`'s is. *Check:* a test that traces a lamp at contact range and
asserts no channel exceeds the ceiling; `shot` on a night exterior for the visual half.

**7 — temporal accumulation (4.3).** The largest step, and the one the priority order puts first.
Reproject the indirect channel through the motion vector it already writes, accumulate with a
history-length counter, estimate variance, and feed it to the wavelet as SVGF's third edge-stopping
term. `biasMask` is already the reset signal. *Check:* `openmw-rtxtool shot --accumulate` gives a
converged reference; the metric is RMSE of one filtered frame against it, before and against after,
on a fixed view. That number is what says it worked.

**8 — bound the shadow rays (4.4).** RIS over the grid cell's candidates, one reservoir, one shadow
ray. Then temporal reuse through step 7's history, then spatial. *Check:* the same RMSE-against-
reference metric, in an interior with a dozen lamps — and a count of shadow rays per pixel, which is
the thing being bounded.

**9 — opacity micromaps.** The device features are already required and probed; nothing builds a
micromap. `alphaPasses` is what stops being invoked. *Check:* the trace timer on a view of foliage,
which is what M12 measures.

**10 — bin the emitters (4.8).** A tile pass over emitter spheres, written before the trace, read as
a range the way the light grid is. *Check:* the trace timer at Seyda Neen with 165 emitters, and the
byte comparison — binning must not change a pixel.

**11 — decide SER (4.6).** After 7 and 8 have changed the live-state picture. Measure occupancy on
`visibility.comp`; if it is where the megakernel is losing, price the ray-tracing pipeline. Until
then, `plan.md` §8's SER entry should say it needs one.

Steps 1–6 are correctness and consolidation, and none of them changes a pixel except 6, which changes
only wrong ones. 7 and 8 are what the frame looks like. 9–11 are M12, and each gets its number
written down when it lands rather than acted on before.

## 7. Sources

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
