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

## Order of work

1. **Issue 1.** It needs the before-and-after shots.
2. **Issue 2.** Small. It needs time on the macOS laptop.
3. **Issue 4's experiments**, then the design they select. They need quiet desktop time and are
   independent of the code above.
4. **Issue 3**, after your yes and after the model scan.

Each issue is deleted from `.notes/ISSUES.md` when its fix is verified. A problem found on the way
goes to the log, not into the issue's change.
