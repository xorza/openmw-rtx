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
- **A micromap is a static table and the cutout it stands in for is not, so what it may claim is
  bounded by the whole mip chain.** `alphaPasses` reads the mask at whatever level the ray's cone
  resolves to, and a trilinear read is a convex combination of texels from two neighbouring levels —
  so a patch of mask can only be called opaque if the smallest texel *every* level could contribute
  is still over the cutoff, and transparent if the largest never is. `AlphaBounds` collapses the
  chain into that pair, each level contributing the three-by-three around the texel a point lands in
  because that is what a bilinear read at that level reaches. Nothing here is a dilation wide enough
  to guess at: the ring is derived per level, and one coarse level disagreeing is enough to send a
  microtriangle back to asking. That is what makes the micromap *remove work and change no pixel*,
  which is the only thing it is allowed to do — nine views, byte for byte, before and after.
  **And the bound is asymmetric in practice, which 4.10 measured.** *Opaque* survives the chain
  wherever a mask is mostly solid, because averaging a solid region leaves it solid; *transparent*
  mostly does not, because the three-by-three at level *l* spans `3 · 2^l` of the finest level's
  texels and reaches a leaf long before the chain runs out. So the verdict that costs traversal the
  least is the one a canopy gets least of, and that is a property of the mip chain rather than
  something a wider search would recover.

## 4. Findings

Ordered as `CLAUDE.md` orders them: how it looks, then performance — with plain correctness ahead of
both, because a wrong pixel is not a trade.

### 4.1 The firefly clamp is in and its effect is not counted

The clamp lives in the accumulator, at `ACCUMULATE_SIGMAS` from the running mean — the form with no
constant in it that is about this game, which is why it had to wait for a history. What is not
established is how much of the tail it actually takes.

The tail it is aimed at, measured before it existed, as the share of pixels whose one-sample bounce
luminance exceeds a threshold:

| threshold | Seyda Neen ship | Balmora | customs office | canalworks |
|---|---|---|---|---|
| 0.5 | 10.8% | 4.9% | 0.046% | 0% |
| 1 | 0.0037% | 0.0002% | 0.022% | 0% |
| 8 | 0% | 0.0001% | 0.0004% | 0% |
| 32 | 0% | 0.0001% | 0.0002% | 0% |
| 64 | 0% | 0% | 0% | 0% |

The signal ends at about one — a surface seeing a full hemisphere of sky — and above it is two to
thirty-four pixels of nine hundred thousand, reaching between 32 and 64.

**Why an absolute ceiling was never possible.** `falloff` is `window² / (d² + 1)`, so a bounce landing
on a lamp returns that lamp's intensity, and a lamp's intensity is content. Any number chosen for
these four cells is a number a modded fifth moves.

**What guards it today is that it does not eat real light**: `theHistoryCarriesWhereTheCascadeHasNo
NeighboursToBorrow` runs it across sixteen frames and the accumulated mean still sits on a converged
reference to within two per cent, which a clamp firing too eagerly would pull down. Counting what it
*removes* wants this table taken again through the accumulator, which needs the trace instrumented
the way it was to produce it.

**A NaN was looked for and is not there.** Every channel the trace writes was instrumented and counted
across seven views and two storms: none. Nor should there be — the tree answers untrusted content at
the boundary, in `describeClouds`, `fogbuilder`, `shadingmap`, `lightbuilder` and `sceneextractor`,
each written as `!(x > 0)` so a NaN lands on the safe side, and `Rtx::Camera` guards the normalised
zero vector. A guard at the five `imageStore`s was written, measured at sixty instructions a pixel,
and taken out again: it caught a fault that has never occurred and whose source is validated one cell
at a time rather than one pixel at a time.

### 4.2 The temporal half, and what it is worth

`AccumulatePass` runs in front of the cascade whenever the wavelet does, and `atrous.comp` has
SVGF's third edge-stopping term for the first time. In place: reprojection through the motion vector
the trace already writes, bilinear over the four pixels it lands between with each tap taken only if
its stored normal and distance say it is the same surface; a history length per pixel, so the blend
is an exact mean while short and exponential past `ACCUMULATE_FRAMES`; first and second moments and
the variance the cascade weighs a tap by; the firefly clamp 4.1 asked for, at `ACCUMULATE_SIGMAS`
from the running mean, with no constant in it that is about this game; and `biasMask` and a lost
history as the two reset signals, `historyLost` being one expression both denoisers now read.

**A pixel with no history carries the largest variance there is, not nought.** `E[l²] - E[l]²` over
one sample is exactly zero, which a filter reads as *certain* and is the opposite of the truth — so
the variance is written by the accumulator rather than derived by the cascade.

**What it is worth: a little over a third of the error the cascade cannot reach.** Measured against a
128-sample converged reference, on a coplanar grid whose shading normals alternate by forty degrees:

| | RMSE against the reference |
|---|---|
| the cascade alone, one frame | 0.00406 |
| with sixteen frames behind it | 0.00253 |

**The scene is the finding.** On the flat sheet the other filter test uses, a history is worth
nothing at all — every pixel there looks at one surface under one smooth sky, so every pixel has the
*same* expected bounce, and a hundred and twenty-five taps average a hundred and twenty-five draws
from one distribution. The cascade lands within a third of a byte on a single frame and there is
nothing left to remove. Where neighbours genuinely disagree — which is what Morrowind's geometry is —
the cascade is left with little more than the centre pixel and the history carries the frame.

**And a third is a floor.** The read-back is eight bits a channel and 0.00253 is two thirds of one
byte at this brightness, so the accumulated frame is already at the edge of what the output format
can distinguish. A tighter figure needs the float channel a test cannot read yet.

The clamp is guarded by the same test rather than measured on its own: it runs across all sixteen
frames, and the accumulated mean still sits on the converged one to within two per cent — a clamp
eating real light would bias it down. That it *removes* fireflies is reasoned and not yet counted.

### 4.3 The shadow rays are bounded — closed

`gather` no longer walks the cell's lamps spending a ray apiece. Every candidate is weighed by what
it would deliver unshadowed — its reach, its falloff and the cosine, which is everything knowable
without tracing — one is kept by reservoir sampling, and exactly one shadow ray is spent on it. The
estimator divides by the chance it was kept, `sum of the weights / the weight of the one held`, which
is what makes it unbiased rather than merely cheap. With one lamp in a cell it is exactly the
arithmetic that was there before, and single-lamp views render byte for byte as they did.

Resampling needs a *sequence* of draws where the blue-noise tile gives one per pixel per frame, so
`random.glsl` grew a hashed counter beside it. The tile is an arrangement across the screen and there
is no screen-space arrangement of a sequence to arrange.

**Measured at 1280×720, three runs of thirty, against the commit before it:**

| view | walking every lamp | one reservoir |
|---|---|---|
| Seyda Neen customs office | 4.89 / 4.40 / 4.55 ms | **3.85 / 3.90 / 3.94 ms** |
| Wolverine Hall | 3.47 / 3.58 / 3.48 | **3.27 / 3.31 / 3.30** |
| Balmora, one lamp a cell | 4.37 / 4.46 / 4.47 | 4.26 / 4.43 / 4.50 |

About a fifth of the trace in the lamp-heavy interior, a twentieth in the fort, and nothing where
there was never more than one lamp to walk — which is the shape the change predicts.

**Unbiased, and the cost to the picture is under a per cent.** Against a 256-frame converged
reference at a fixed exposure:

| | customs office | wolverine hall |
|---|---|---|
| converged with a reservoir, against converged exhaustive | RMSE 0.00005 | 0.00000 |
| one filtered frame, walking every lamp | RMSE 0.05973 | 0.01260 |
| one filtered frame, one reservoir | RMSE 0.05985 | 0.01268 |

The first row is the bias check and it passes: resampling converges on the same answer. The other two
are what a player sees on the first frame, and the reservoir costs 0.2% and 0.6% of the error the
denoiser already leaves — which is to say the filter absorbs the variance the estimator adds.

**The exposure is why an earlier reading said otherwise.** Comparing two runs whose exposure was each
measured from its own frame put 12% of the customs office's channels apart with a worst case of 170,
and none of it was the estimator: auto-exposure had simply landed in two different places. Any
comparison of two renders here has to pin `--exposure` on both sides, the way it has to pin
`--validation`.

**Reuse is now a quality improvement rather than a debt.** Carrying a reservoir through the motion
vector brings last frame's *traced visibility* into a target function that cannot otherwise have it,
which is worth having — but it is no longer paying for anything, and it goes in the queue on its own
merits rather than ahead of them.

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

### 4.10 Opacity micromaps are built — closed

`Rtx::Micromap` classifies and `SceneAcceleration` builds: one `VkMicromapEXT` per mesh, in the same
submit as the bottom levels that reference it and separated from them by a barrier, chained on with
`VkAccelerationStructureTrianglesOpacityMicromapEXT` at `VK_INDEX_TYPE_NONE_KHR` — a triangle's
micromap entry is its own position in the geometry.

**A micromapped mesh is built non-opaque and its instances stop forcing.**
`VK_GEOMETRY_INSTANCE_FORCE_NO_OPAQUE_BIT_KHR` overrides what a micromap decided, so keeping it
would have handed the whole saving back at the instance; the geometry's `VK_GEOMETRY_OPAQUE_BIT_KHR`
is cleared alongside it, which is not redundant but a choice of failure mode. A micromap that went
missing over a geometry still claiming opacity turns a canopy into a solid card; over one that does
not claim it, every hit becomes a candidate — which is what the whole cell did before.

**A mesh only qualifies where every placement standing on it names the same cutout material.** A
micromap belongs to the structure and so to the mesh, a cutout belongs to the material, and a mesh
two materials disagree about has no one answer to give.

**The check passed: nine views, byte for byte, before and after.** `map` and `doll` were run as well
and drew what they drew before.

| view | cutout instances | micromapped | opaque | transparent | still asking |
|---|---|---|---|---|---|
| Seyda Neen shore | 3322 | 3322 | 12.5% | 2.4% | 85.0% |
| Vivec | 4727 | 4679 | 46.3% | 1.3% | 52.4% |
| Balmora | 2588 | 2588 | 13.6% | 4.5% | 81.9% |
| Dagon Fel | 473 | 397 | 34.4% | 3.9% | 61.8% |

**And the trace did not move.** GPU trace-zone medians, before against after:

| view | before | after |
|---|---|---|
| Vivec | 2.183 ms | 2.173 ms |
| Seyda Neen shore | 0.884 ms | 0.883 ms |
| Balmora | 1.894 ms | 1.897 / 1.920 / 1.900 ms |

Written down rather than acted on, which is `CLAUDE.md`'s rule for an M12 number. What it says is
that the candidate loop is not where these frames spend their trace: between a sixth and a half of
the micromapped surface stopped asking and nothing measurable came back. The reading to be careful
with is Balmora, whose first after-run came in at 2.945 ms — with the *upscale* zone moved by the
same proportion, which no micromap can touch. Three more runs settled it. **A zone that cannot have
changed moving with the one under test is the signal that a reading is contaminated**, and it
belongs beside `--validation` and `--exposure` in the list of things that have produced false
answers here.

**What the coverage column says is that the conservatism lands on the foliage.** *Transparent* is
the verdict that skips a triangle outright, and a canopy gets one to four per cent of it — for the
reason in §3, which is the mip chain and not the search. *Opaque* does far better, and Vivec is why:
a city of windows, grates and banners is mostly solid mask, and averaging a solid region leaves it
solid.

**Micromaps cost 492 KiB of structure storage at Seyda Neen**, 17281 KiB against 17773.

**One gap, named.** `buildMicromaps` classifies against the texture descriptions the scene was
handed, which on an `extend` are the ones that arrived with it — so a mesh appearing beside an image
already resident has nothing to classify against and goes on asking until the next reset. Every
reset builds the world's, which is every cell load, door and travel. Closing it means the
classification moving into the core beside `SceneTextures`, where an image manager can resolve a
mask that did not arrive, and that is a widening of `Renderer::setScene` to pay for a case worth
measuring first.

## 5. Steps

Each step is checkable on its own and leaves the tree working. The check is named because *"it
compiled"* is not one. As one lands it is deleted from here, so step 1 is always what to do next.

**The byte comparison is the check for anything that must not change a pixel.** `openmw-rtxtool
shot` is deterministic — `mFrame` and `mTime` are zero under it and every draw is keyed on them — so
an identical frame is an identical file and one differing byte means something moved that should not
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
runs of one build, the doll about 0.02% — so those two need a magnitude compared against a
same-build control, never `cmp`. Both are worth running anyway: `map` is the only orthographic path
and `doll` the only transparent-background one.

**1 — bin the emitters (4.7).** A tile pass over emitter spheres, written before the trace, read as
a range the way the light grid is. *Check:* the trace timer at Seyda Neen with 165 emitters, and the
byte comparison — binning must not change a pixel.

**2 — say each thing once, and stop the game carrying what only the harness wants (4.4, 4.9).** The
consolidation this review was for, and the last of it. The wave spectrum is evaluated at two
different points — the surface a ray met and the bed its light landed on — so the two *loops* cannot
merge; what is one thing said twice is the structure inside them, `drifted` and `sampleWave` and the
phase whose `sin` one caller wants and whose `cos` the other does, and that belongs in one place a
change to the spectrum reaches both from. Beside it: `0xFFFFFFFF` under three names across two
headers, and the random stream ids split between `scene.h`, where C++ generates the tile, and
`random.glsl` — where the count is a promise the ids have to keep, so a second shader that drew
would have to know which ones to avoid. And `atomicAdd(hits, 1)` is a debug facility compiled into
the shipping kernel; a specialization constant the harness sets and the game does not leaves one
module serving both.

*Check:* the byte comparison, because none of this may move a pixel — and `shot` still printing its
hit fraction, because the specialization is only correct if the number it reports is the same one.

**3 — a linear channel the harness can read (4.1, 4.2).** Both measurements below are up against
eight bits. 4.2's figure of 0.00253 is already two thirds of a byte at that brightness, so the
accumulated frame sits at the edge of what the read-back can distinguish and a tighter number cannot
be had at all; 4.1's tail is counted in bounce luminance, which the tone curve has spent by the time
`OffscreenTrace` sees it. What both want is the image before the curve, in the format it was traced
in.

*Check:* the filter tests reproduce their present figures through the new channel to within what the
eight-bit path could resolve. A channel that disagreed with the one it replaces is a channel
measuring something else.

**4 — count what the firefly clamp removes (4.1).** The tail table in 4.1 was taken before the clamp
existed, on the one-sample bounce; what is not established is how much of it the clamp actually
takes once a history is behind it. That wants the same instrumentation the table came from, run
through `AccumulatePass` this time, on the same four cells.

*Check:* the table again, per view, with the clamp on and off — and the guard that already exists
still holding: the accumulated mean stays on the converged reference to within two per cent, which a
clamp eating real light would pull down.

**5 — the temporal half, off the floor (4.2).** "A little over a third of the error the cascade
cannot reach" is a floor and not a figure, for the reason step 3 exists. Recompute it where the
format can hold the answer.

*Check:* the same pair of RMSEs against a 128-sample reference on the same coplanar grid — the
cascade alone, and the cascade with sixteen frames behind it. The ratio is the answer; the absolute
numbers are what the eight-bit path could not give.

**6 — carry the reservoir (4.3).** Temporal through the motion vector first, spatial across
neighbours after. This is the one item here that changes the picture rather than the frame time: it
brings the previous frame's *traced visibility* into a target function that cannot otherwise have
it, so the lamp that is kept is the lamp that was actually reaching. 4.3 closed the cost argument,
so it is queued on its own merits and nothing is waiting on it.

*Check:* the bias check 4.3 already has — converged with reuse against converged exhaustive, at a
pinned exposure, which must stay at the fifth decimal — and beside it the first-frame RMSE, which is
what reuse is for and which has to *fall*.

**7 — the shared-memory guide tile (4.8).** 250 image loads and 125 normalizes a pixel over a
neighbourhood every thread in the workgroup shares. Held until 5 has said what the cascade is still
worth, because a temporal denoiser usually needs fewer levels and the tile is worth more per level.

*Check:* the byte comparison — a tile is a cache and must change nothing — and the atrous timer
against the level count it was measured at.

**8 — measure occupancy on `visibility.comp` (4.6).** Nsight against the megakernel, after 6 has
changed what is live: the micromaps already took a texture fetch and a candidate loop off part of
the hot path, and a carried reservoir puts a buffer into it. Nothing in 4.6 is to be *acted* on
before this — that is `CLAUDE.md`'s rule and it holds — and the split into `lib/` is what makes a
cheaper variant a one-line edit once the number says which one to try.

*Check:* the number itself, written down beside the register count and the live-state inventory 4.6
lists, and nothing else changed.

**9 — decide SER (4.5).** With 8's number in hand: if occupancy is where the megakernel is losing,
price the ray-tracing pipeline that `reorderThreadEXT` requires, against what ray query and the
register-resident megakernel are worth. Until it is decided, `plan.md` §8's SER entry should say it
needs one.

Every one of these is M12, and each gets its number written down when it lands rather than acted on
before.

## 6. Sources

- [VK_EXT_ray_tracing_invocation_reorder proposal](https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_ray_tracing_invocation_reorder.html) — why reordering is raygen-only
- [Khronos: Boosting Ray Tracing Performance with Shader Execution Reordering](https://www.khronos.org/blog/boosting-ray-tracing-performance-with-shader-execution-reordering-introducing-vk-ext-ray-tracing-invocation-reorder)
- [Path Tracing Optimizations in Indiana Jones: Opacity MicroMaps and BLAS compaction](https://developer.nvidia.com/blog/path-tracing-optimizations-in-indiana-jones-opacity-micromaps-and-compaction-of-dynamic-blass/)
- [Path Tracing Optimization in Indiana Jones: SER and live-state reductions](https://developer.nvidia.com/blog/path-tracing-optimization-in-indiana-jones-shader-execution-reordering-and-live-state-reductions/)
- [Megakernel vs Wavefront GPU Path Tracing](https://arxiv.org/pdf/2605.27323)
- [Vulkan Documentation Project: Ray Tracing](https://docs.vulkan.org/guide/latest/extensions/ray_tracing.html) — ray query against the pipeline
- [VK_EXT_opacity_micromap appendix](https://github.com/KhronosGroup/Vulkan-Docs/blob/main/appendices/VK_EXT_opacity_micromap.adoc) — `BarycentricsToSpaceFillingCurveIndex`, transcribed verbatim into `microtriangles.cpp`
- [Vulkan specification: Ray Opacity Micromap](https://docs.vulkan.org/spec/latest/chapters/raytraversal.html) — the four states, the two-state override, and how the curve recurses
- [NVIDIA-RTX/NRD](https://github.com/NVIDIA-RTX/NRD) — ReBLUR, ReLAX, the demodulation contract
- [Real-Time Denoising: SVGF, A-SVGF, DLSS & ReLAX](https://www.mysimulator.uk/content/articles/realtime-denoising.html)
- [ReSTIR PT Enhanced](https://dl.acm.org/doi/10.1145/3804494) — reciprocal neighbour selection, duplication maps
- [ReSTIR GI: Path Resampling for Real-Time Path Tracing](https://blog.zcy.moe/en/blog/restir-gi/)
- [Spatiotemporal reservoir resampling for dynamic direct lighting](https://dl.acm.org/doi/abs/10.1145/3386569.3392481)
- [NVIDIA-RTX/RTXPT](https://github.com/NVIDIA-RTX/RTXPT) — reference real-time path tracing library
