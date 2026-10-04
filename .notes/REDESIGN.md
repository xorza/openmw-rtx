# Redesign for the open issues

This plan covers the open issues in `.notes/ISSUES.md`, investigated on 2026-10-04 on `master`
with `restir-gi` merged into the working tree. Each section gives the evidence, the cause, the
redesign and the steps. The last section gives the order of the work.

Issue 3 changes upstream code (`groundcover.cpp`), which you approved after a scan of the models.
Issue 4 starts with measurements; the design depends on what they show.

| # | Issue | Cause found | Fix | Approval |
|---|-------|-------------|-----|----------|
| 3 | Groundcover shapes under a transform | Upstream applies the instance transform before the node transforms | Bake the shape's own transform into the instanced geometry | Approved |
| 4 | Host rows move between runs | Not proven yet. Data layout for the whole process is the main suspect | Experiments first, then the layout fix they show | Decided |

---

## 3. Groundcover shapes under a transform (upstream code — needs approval)

**Approved:** scan the models first, then make the fix.

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

**Decided:** a session runs the experiments in the background and ends its turn while they run.

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

1. **Issue 3**, the model scan and then the fix.
2. **Issue 4's experiments**, last, then the design they select.

Each issue is deleted from `.notes/ISSUES.md` when its fix is verified. A problem found on the way
goes to the log, not into the issue's change.
