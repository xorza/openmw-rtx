# Redesign for the open issues

This plan covers the open issues in `.notes/ISSUES.md`, investigated on 2026-10-04 on `master`
with `restir-gi` merged into the working tree. Each section gives the evidence, the cause, the
redesign and the steps. The last section gives the order of the work.

Two of them need a decision from you before work starts:

- Issue 3 changes upstream code (`groundcover.cpp`). By the fork's rules, an upstream bug fix
  waits for your yes.
- Issue 4 starts with measurements on a quiet desktop. The design depends on what they show.

| # | Issue | Cause found | Fix | Approval |
|---|-------|-------------|-----|----------|
| 1 | A soft blended edge is cut at 0.5 | One rule for the whole material and for every ray | Eye rays read the texel's alpha as a pane; light rays keep the cut | No |
| 2 | macOS install carries the harness | One global output directory, inside the bundle | The harness builds into the tree, outside the bundle | No |
| 3 | Groundcover shapes under a transform | Upstream applies the instance transform before the node transforms | Bake the shape's own transform into the instanced geometry | **Yes** |
| 4 | Host rows move between runs | Not proven yet. Data layout for the whole process is the main suspect | Experiments first, then the layout fix they show | Experiments first |
| 5 | A thin sun shadow loses depth | The shadow denoiser treats a hard edge as noise | Penumbra-sized reach (NRD SIGMA's practice) | No |

---

## 1. A blended mask is cut at 0.5 for every ray

### Evidence

`Material::isTranslucent` (`components/rtx/scene/material.hpp`) decides for the whole material.
A blend-over with no test of its own is a pane only if its material alpha is below one or its
texture never reaches 255. In all other cases it is a mask, and `getAlphaTest` cuts it at
`sBlendCutoff` = 0.5 for every ray: the eye's any-hit (`visibility.rahit`), the shadow rays and the
bounce, all through `candidateStops` (`lib/traversal.glsl`).

The hit already composites per-texel opacity: `resolveFor` reads the texel's alpha into
`Surface::mOpacity` when `isSeenThrough`, and `peeled` makes a pane of any opacity below one.
A mask never reaches that path, because its material is not translucent.

The comment on `isTranslucent` gives the reason for the current rule. When soft leaves were panes
for every ray, the sun and sky rays that walk past see-through surfaces turned grainy: at Seyda
Neen's pier the panes went from 3 to 91. That cost came from the light rays, not from the eye.

### Cause

The pane-or-mask choice is made once for the material and once for all ray roles. But the two
roles want different things:

- The eye wants the blend, the same as `objects.frag` draws it.
- A light ray wants a cheap and noise-free cut.

### Redesign

Split the choice by ray role, and make it per texel for the eye:

- **Light rays** (shadow, sky, lamp, bounce, the reuse's visibility rays): no change. A mask is cut
  at `sBlendCutoff`.
- **Drawing rays** (the eye and its peel layers): a mask is cut at `sPaneCutoff` (half a step of
  8-bit alpha), and the texel's alpha becomes `Surface::mOpacity`. A texel at 255 is a solid. A
  texel between the two cutoffs is a pane, which the launch peels and the pane filter averages, as
  it does for glass today.
- **The last peel layer** (`record.mLayer == PEEL_LAYERS`) cuts a mask at `sBlendCutoff`, not
  at `sPaneCutoff`. Today the last layer shades whatever it meets as a solid. With soft edges, a
  fringe texel of alpha 0.1 would then show as a solid. A cut at 0.5 there is what the trace
  draws today, so dense foliage that uses all of the budget looks no worse than now.

A material whose texture never reaches solid keeps its current pane rule for every ray. This also
fixes the cobweb, the Telvanni crystal and the Bloodmoon ice: the eye sees each texel at its own
alpha, and their shadows stay what they are today.

### Steps

1. **Material.** Add `Material::softEdged()`: blend, `BlendKind::Over`, no test of its own, opacity
   one, and a texture that reaches solid. The GPU material row gets one more flag
   (`MATERIAL_SOFT_EDGE` in `scene.h`), so the shader does not compute the rule a second time.
   `isTranslucent`, `isCutout` and `getAlphaTest` do not change. They state the light rays' rule.
2. **Traversal.** `candidateStops` already knows if its ray draws (`facingFor`). For a drawing ray
   on a soft-edged material, cut at `sPaneCutoff`, except on the last peel layer. Give the rule one
   function, `drawnReference(material, layer)`, which the eye's any-hit and the inline queries both
   call, so that the two cannot disagree.
3. **Resolve.** In `resolveFor`, a drawing ray on a soft-edged material reads the texel's alpha
   into `mOpacity`, as `seenThrough` does today. `peeled` then needs no change.
4. **Tests (GPU).** A card textured with an alpha ramp from 0 to 255, over a black floor, under a
   white sky that lights it, with the denoiser off:
   - each column of the eye's frame equals `alpha × card + (1 − alpha) × behind`, worked out by
     hand from the ramp, to the rounding of the texture filter;
   - the card's shadow on the floor is bit for bit the same as before the change (light rays
     unchanged);
   - with five cards in a stack, the fifth is cut at 0.5 and is not shaded as a solid.
5. **Measurement.** Use `./omw shot --views=all --map --upscale=off` before and after, plus
   `./omw release bench` at the foliage places (the pier, the pond, and a forest view if
   `views.cfg` has none). Gates:
   - the trace's median and p99 move by no more than the cost of the extra peel layers in fringe
     pixels;
   - `noise` at the pier and the pond is no worse.

   If the cost is too high, an alternative was considered: stochastic alpha for the eye ray
   (Enderton et al. 2010, *Stochastic Transparency*). It costs no layers, but a run without
   accumulation shows dithered edges, and `repeat` runs with the upscaler and the denoiser off. So
   the peel is the first choice.

---

## 2. The macOS install carries `openmw-rtxtool`

### Evidence

The top-level `CMakeLists.txt` sets `CMAKE_RUNTIME_OUTPUT_DIRECTORY` to
`${APP_BUNDLE_DIR}/Contents/MacOS` on Apple. `apps/rtxtool/CMakeLists.txt` makes `openmw-rtxtool`
with `openmw_add_executable`, so the harness inherits that directory. `install(DIRECTORY
"${APP_BUNDLE_DIR}" ...)` installs the bundle whole. The harness's own data is already outside the
bundle (`RTX_HARNESS_DIR`, "because no install carries the harness"). Only the executable is
inside.

### Cause

Where a program lands follows one global variable. Nothing states that the harness belongs to the
build tree and not to what ships.

### Redesign

The harness says where it lands: in `apps/rtxtool/CMakeLists.txt`, set the target's
`RUNTIME_OUTPUT_DIRECTORY` to the build tree's runtime directory. That is the directory every other
system already uses (`${OpenMW_BINARY_DIR}`, or `$<CONFIG>` under it for a multi-config
generator). Linux and Windows do not change. The fork's own GPU test binary (`rtx-gpu-tests`) gets
the same property, because it also runs only from the tree that built it. Upstream's test
binaries are upstream's and are not in scope.

Outside the bundle, the macOS resource lookup (`MacOsPath`, the binary's `../Resources`) no longer
finds the bundle's `Resources`. The harness already gets its folder from a compiled-in path
(`OPENMW_RTX_HARNESS_DIR`), for the reason that it runs only from its tree. On Apple, give it the
bundle's `Contents/Resources` the same way, as the default of `--resources`.

### Steps

1. Set the target property for `openmw-rtxtool` and `rtx-gpu-tests` in the fork's CMake files only.
2. On Apple, add the bundle's resources as the default of `--resources`.
3. Verify on the macOS laptop, which you give access to on request (`omw` refuses macOS):
   configure, build, then `cmake --install` into a temporary prefix. Check that
   `OpenMW.app/Contents/MacOS` has no `openmw-rtxtool`, and that `build/openmw-rtxtool --help` runs.
4. Verify on Linux: `./omw build` and `./omw test`. Nothing moves.

---

## 3. Groundcover shapes under a transform (upstream code — needs approval)

### Evidence

`Groundcover::createChunk` deep-copies the model and `InstancingVisitor` (`groundcover.cpp`) puts
each plant's offset and scale into vertex attribute 6, and its rotation into attribute 7.
`groundcover.vert` then computes `rotation * scale * gl_Vertex`, adds the offset, and only then
multiplies by `gl_ModelViewMatrix`. That matrix holds the shape's own node transforms inside the
model. So the result is `Nodes × Instance × vertex`. Every other reference is `Instance × Nodes ×
vertex`. The ray tracer stands a plant as a static, which is the second order, so the two
renderers put such a plant in two places.

### Cause

The node transforms inside the model come after the instance transform, in the wrong order.
For a model whose shapes stand under the identity, the two orders give the same result. That is why
vanilla content hides the defect.

### Redesign

Make the rasterizer's order the same as everything else. In `InstancingVisitor`, bake each
geometry's static transform, from the model's root down to the geometry
(`osg::computeLocalToWorld` over the visitor's node path, cut at the model root), into its
vertices and normals (and its tangents, if any). The copy was made with `DEEP_COPY_ARRAYS`, so the
arrays are the chunk's own. Then set the transforms between the root and the geometry to the
identity. `gl_ModelViewMatrix` then holds only the chunk's place, and the shader does not change.
Compute the bound after the bake.

A transform with a controller cannot be baked. Before the change, check the groundcover models in
use: a small scan of the data's groundcover entries for non-identity and controlled transforms.
If a controlled transform exists, keep that one geometry on its own path and log it. Do not guess.

The ray tracer does not change. Its picture is already the correct one.

### Steps

1. Scan the groundcover models (vanilla and the replacers you play) and count the shapes under a
   non-identity transform, and under a controlled one. This scan is not a code change.
2. After your yes: the bake in `InstancingVisitor`, as above.
3. Test in `openmw_tests`: a model with one shape under a translation of (10, 0, 0) and a 90° turn,
   in a chunk with one plant at (100, 200, 0) and a scale of 2. The instanced geometry's vertices,
   with the shader's arithmetic done by hand on the host, land where `Instance × Nodes × vertex`
   puts them.
4. Add a line to the *Accepted diff* list in `AGENTS.md` for the rasterizer's picture, beside the
   four that are there, as the fork's rules ask.

---

## 4. A measured run's host rows move between runs of one build

### Evidence

At `one-cell-walk`, six legs held to the performance cores at a steady clock read walk medians
of 1.02 to 1.53 ms, and the frame thread's cache misses per thousand instructions moved with
them, 3.14 to 4.75. All host rows move together, not only the walk.

### What the code shows

- The walk (`SceneExtractor`, `WorldMirror`) follows OSG nodes each frame. Those nodes are
  allocated when a cell loads, on the loader threads. glibc gives each thread an arena, and which
  arena a thread gets depends on timing. So the heap layout of the graph changes from run to run.
- The identity tables (`boost::unordered_flat_map` keyed by node address, `ByAddress`) put their
  entries by the address's hash. So the probe order changes with the layout.
- The kernel is booted with `transparent_hugepage=madvise`, and nothing in the tree calls
  `madvise(MADV_HUGEPAGE)`. So the heap is in 4 KiB pages, and their physical placement, which
  sets which L2 and L3 sets the data uses, changes on each run.

All three can move every row together. The data does not yet say which one does. Measurement bias
of this kind is well known (Mytkowicz et al. 2009, *Producing Wrong Data Without Doing Anything
Obviously Wrong!*). So the first step is experiments, not a design.

### Experiments

Each experiment runs six legs of `./omw release bench --views=one-cell-walk`. Start each in the
background on a quiet desktop and end the turn, as `AGENTS.md` says. Read the walk median, the p99,
and the miss rate per leg. The spread to beat is 1.02 to 1.53 ms.

| | Change for the run | If the spread shrinks, the cause is |
|---|---|---|
| E1 | `setarch -R` (no ASLR) | virtual layout and hash order |
| E2 | `GLIBC_TUNABLES=glibc.malloc.arena_max=1` | the loader threads' arenas |
| E3 | `GLIBC_TUNABLES=glibc.malloc.hugetlb=1` (THP for malloc) | physical page placement |
| E4 | E2 and E3 together | both |

For each leg, also record the frame thread's dTLB and L2 misses, beside the miss rate that the
harness's thread counters (`instruments/threadcounters.hpp`) gave for the issue. That shows which
level of the memory hierarchy moves.

### Designs, by result

- **E3 wins (page placement):** The renderer's host side asks for huge pages for the memory the
  walk reads: the mirror's tables and the `SceneDesc` rows, with `madvise(MADV_HUGEPAGE)` on
  allocations it owns, behind `Platform::` for the POSIX and Win32 split. The OSG graph is
  upstream's and stays as it is. The harness also reports the THP state in its run header, so that
  two runs that differ say so.
- **E2 wins (arenas):** The walk must not follow memory that a race placed. Move the per-frame
  reads to what the mirror owns: the frozen runs (`mFrozen`, `RunBuffer`) already flatten static
  subtrees. Extend that to the live parts the walk reads every frame, with contiguous storage and
  indices, not node pointers. This is a large change, and it gets its own plan after E2.
- **E1 wins (hash order):** Key the identity tables by a stable identity
  (`SceneUtil::StableIdentity`, which the walk already reads), not by address.
- **Nothing shrinks it:** The cause is outside the process (the scheduler, the compositor,
  firmware). Record that in the issue, and give the measured runs a check of the core and
  frequency state per leg.

---

## 5. A thin sun shadow loses its depth in the denoised frame

### Evidence

A temporary probe used the issue's scene: a bar 40 units wide, 100 units over a floor, an overhead
sun, 96 pixels square, 96 still frames, fixed exposure. It gave these minimum column means, in
8-bit values:

| upscale | indirect | filter | umbra | edge profile across the shadow |
|---|---|---|---|---|
| off | off | off | **17** | `137 137 21 19 18 17 17 17 18 24 84 137` |
| off | off | on | **31** | `133 130 52 46 40 32 31 36 46 65 103 127` |
| off | traced | on | 31 | the same as with indirect off |
| quality | off | off | 17 | — |
| quality | off | on | **66** | — |

With the indirect light off, no bounce exists and none of the bounce's filters run. The loss is
the same. So **the shadow denoiser causes it**, and the bounce filters do not. The lit side next to
the edge also goes dark (137 becomes 130 to 133). The edge is blurred in both directions.

### Cause

Two mechanisms in `shadowtiles.comp`, the temporal half (FidelityFX's design):

1. **The clamp box.** The history is clamped to the local mean of the frame's bits, ±0.5σ, over a
   Gaussian of `REACH` pixels. In a shadow only 7 to 9 pixels wide, the local mean near the edge is
   far from the pixel's own bit, which never changes. An umbra pixel with a local mean of 0.3 has
   its history of 0 held at about 0.07, and a lit pixel's history is held down by the same amount.
2. **The discontinuity.** A history that disagrees with the local mean loses samples
   (`momentsNow.z *= exp(-d²/20)`). At a hard edge that is every frame, so the history stays under
   16 samples. Its variance is then raised (`max(variance, spatialVariance) × (16 − z)`), and the
   spatial filter (`shadowfilter.comp`, taps at 1, 2 and 4) blurs across the edge.

Both mechanisms read a noiseless hard edge as noise. Two temporary variants of the temporal pass
were run against the same probe and the trail tests:

| Variant | Umbra (off / quality) | `theSunsShadowFollowsItsCaster` | Penumbra tests |
|---|---|---|---|
| Today | 31 / 66 | pass (tail 0.15) | pass |
| Box grown to hold the pixel's own bit, discontinuity from the box | **17** / 38 | **fail**, tail 0.37 > 0.25 | pass |
| Box grown to hold the pixel's own bit, discontinuity as today | 26 / 48 | pass | pass |

So each mechanism causes about half of the loss. A box that holds the pixel's own bit is not
enough without a cost: when the discontinuity also stops treating the edge as noise, a moving
shadow's tail grows. A local patch cannot tell a hard edge from noise, because the denoiser does
not know how wide the penumbra is.

### Redesign: a penumbra-sized reach

This is the field's established practice. NVIDIA's shadow denoiser, SIGMA (NRD), packs each
visibility ray's distance to the occluder with the light's angular size
(`SIGMA_FrontEnd_PackPenumbra`) into a penumbra radius, and uses that radius as its blur radius.
With several lights, it uses a radius weighted by each light's share. The pixel radius is the world
penumbra over the pixel's world footprint (NVIDIA, US 11,367,244). A hard shadow has a penumbra
under one pixel, so it gets no spatial blur and no spatial box, and a soft one gets its full reach.

1. **The trace keeps the penumbra.** Beside the one bit in `CHANNEL_SHADOWED`, write the penumbra
   radius at the receiver for the source whose bit was kept (`drawnOpen` already picks the sky's or
   the lamps'). Give it a 16-bit channel of its own:
   - the sun or a moon, blocked at `t`: `t · tan(angular radius)`;
   - a lamp of radius `r` at distance `D`, blocked at `t`: `r · t / (D − t)`;
   - an open ray: the half-float maximum, which SIGMA uses for "no occluder".

   The shadow ray meets its occluder already, so `t` costs no extra ray. But a ray that stops at
   its first hit gives the distance of an occluder, which is not always the nearest one. Step 1
   decides if that is accurate enough, from SIGMA's own input rule.
2. **The tiles pass spreads it.** An open pixel next to a blocker needs the blocker's penumbra. The
   tiles pass already reads every 8×8 tile. It keeps the tile's largest finite radius, in pixels
   over `footprintAlong`, beside the tile's classification.
3. **The temporal pass sizes its box by it.** The neighbourhood's reach is
   `clamp(penumbra pixels, 0, REACH)`. With a reach under one pixel, the box is the pixel's own
   temporal moments (mean ±kσ of its own history), grown to hold the frame's bit. The discontinuity
   is measured against that box. So a hard edge is no longer a spatial outlier, and a moving
   shadow still leaves the box at once, because its own bit changes.
4. **The spatial filter skips the levels the penumbra does not need.** A level whose step is wider
   than the tile's penumbra in pixels copies its input, as a cleared tile does today.

### Steps

1. Read SIGMA's source for `PackPenumbra`, the tile pass and the radius-to-step rule. Check its
   licence before any code is taken. Take its formulation the same way `shadowfilter.comp` took
   FidelityFX's, with the notice the licence asks for.
2. Add the penumbra channel: `gbuffer.h`, the trace's store, and the device test for the channel's
   values by hand (the sun at 100 units: `100 · tan(0.2665°)` world units).
3. Tiles, temporal and filter changes as above, one commit each, with the probe numbers after each.
4. Make the probe a permanent test (`RtxShadowDenoiseTest.aThinHardShadowKeepsItsDepth`): the
   denoised umbra stays within 1/255 of the raw frame at `upscale=off`, and the lit side next to it
   within 1/255 of the lit floor. Hold the `quality` figure at what the fix measures, and name the
   upscaler's share apart from the denoiser's.
5. Gates that must not regress: `RtxBounceTrailTest.theSunsShadowFollowsItsCaster` (lag and tail),
   `RtxPenumbraDenoiseTest.*` (noise and light), `./omw noise --suite=noise` at the pier and the
   pond, and `./omw kernels --against` names only the shadow passes.

---

## Order of work

1. **Issue 5.** The evidence is complete, and the fix is local to the shadow denoiser and one
   channel.
2. **Issue 1.** It needs the before-and-after shots.
3. **Issue 2.** Small. It needs time on the macOS laptop.
4. **Issue 4's experiments**, then the design they select. They need quiet desktop time and are
   independent of the code above.
5. **Issue 3**, after your yes and after the model scan.

Each issue is deleted from `.notes/ISSUES.md` when its fix is verified. A problem found on the way
goes to the log, not into the issue's change.
