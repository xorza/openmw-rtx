# The scene mirror

How an `osg` scene graph becomes a `Rtx::SceneDesc`, why the current shape costs what it does, and
what replaces it. `plan.md` §2 chose to mirror the graph and named the condition the mirror has to
meet — "**the mirror must be incremental; a full rebuild per frame is the naive version and it will
not hold a frame budget**". Geometry and materials met that condition from M1. Placements never did.
This is the document for making them.

## 1. What a frame does now

`Tracer::update` (`apps/openmw/mwrender/rtx/tracer.cpp:149`) and the harness both call
`SceneExtractor::extract` on the whole root, every frame. `SceneDesc::clearPlacement` has just
emptied the instance list, so the walk rebuilds all of it.

Per drawable met, and there are 47,828 of them on the deck at Seyda Neen:

| | |
|---|---|
| up to four `dynamic_cast`s | particle system, rig, morph, terrain |
| `osg::computeLocalToWorld(path)` | the whole chain from the root, in double precision, per drawable |
| `findOwnStateSet(path)` | the path walked again, for the material |
| `identify(path)` | an FNV hash over every pointer in the path |
| two hash lookups and one hash insert | `mMeshes`, `mStood`, `mStanding` — the last carrying a 64-byte matrix, on a node the allocator hands back and takes again every frame |
| one `push_back` of ~140 bytes | `MeshInstance`, including a `std::optional<osg::Matrixf>` |

Then both consumers walk the whole list again. `makeInstanceRecords` runs **twice** — once for the
top level, once for the shader table — and each pass asks "did this move" by comparing two 64-byte
matrices. 6.2 MB of instance rows are memcpy'd to the device. The top-level structure is rebuilt
from all 47,828.

## 2. What it costs, measured

`profile.sh --dwarf`, 360 frames at Seyda Neen, 1280×720 traced. The frame is **30.3 ms wall and
19.7 ms of CPU** — two thirds of one core, and the GPU does 8.7 ms of it, so the frame is neither
GPU-bound nor CPU-bound but serialised.

| | share of frame CPU | ms | |
|---|---|---|---|
| `SceneExtractor::extract` | 19.7% | 3.9 | ours |
| `SceneAcceleration::refitMeshes` | 16.5% | 3.2 | ours, and already O(movers) |
| `SceneAcceleration::buildTopLevel` | 13.1% | 2.6 | ours |
| `SceneBuffers::place` | 10.4% | 2.1 | ours |
| `osg::Group::traverse`, self | 9.4% | 1.9 | upstream, driven by us |
| `SceneUtil::RigGeometry::cull` | 5.2% | 1.0 | upstream |

Against that, what actually moves in the same scene: **167 deforming drawables and 165 emitters.**
Everything else is where it was. **Better than 99% of the per-frame work is rediscovering that
nothing happened**, and the two thirds of a core it takes is spent proving it.

The reference implementation reached the same conclusion from the other side. `rtxmw`'s
`docs/design.md` records TLAS instances marshalled through a CPU vector on every build as "**the
fatal cost**" for a per-frame top level over a hilltop's worth of statics — and separately measures
that the GPU rebuild itself is free: 477 resident cells "rebuild the top level once a frame without
showing up in the budget". Our own `tlas` timestamp agrees at 0.43 ms. **The device was never the
problem. The marshalling is.**

## 3. The shape that replaces it

Three layers. Each is separately landable and separately measurable, and the first does not change
the architecture at all.

### Phase 0 — carry the transform down instead of recomputing it up — **done**

The visitor is already standing at the drawable's depth when it calls `computeLocalToWorld(path)`,
which then walks back to the root and multiplies the chain again — O(depth) per drawable, for a
product every sibling below the same transform shares. A visitor that pushes on the way down and
pops on the way up makes it one multiply per node *entered*.

Landed, and **bit-identical**: six views across exteriors, an interior, a cave and Dwemer ruins
render to the same bytes. `computeLocalToWorldMatrix` is what `computeLocalToWorld` calls on each
transform it meets, and the visitor calls it with the same null visitor argument, so an absolute
reference frame still replaces the accumulation rather than adding to it. Two tests now hold the
part nothing tested: that a chain of three composes root-downwards, and that an absolute frame
discards what is above it while a relative sibling carries it.

**It is worth 0.28 ms, and the prediction here said 0.9.** `extract` went from 19.69% of frame CPU
to 18.28%; frame time did not move, because 1.4 points of 19.7 ms is under the 2.7% run-to-run
spread. The forecast came from adding up the matrix lines in the profile and assuming
`computeLocalToWorld` owned them; measured afterwards they had barely moved, because most of that
time belongs to OSG's own traversal and to the inverse inside `makeInstanceRecords`. OpenMW's node
paths are shallow, so there was less redundancy in the chain walk than the shape of the code
suggested.

Two things worth carrying forward. **A saving inferred from source-line attribution across function
boundaries is a guess**; the figures for the phases below come from whole-function inclusive costs,
which is a sounder basis, but they are still forecasts and the same caution applies. And the reason
to keep this phase is no longer its own number: it removes the per-drawable dependency on a node
path reaching the scene root, which **Phase 2 needs**, because a walk that starts at an anchor does
not have one.

`findOwnStateSet(path)` was in this phase too and came out of it. It walks from the drawable
*backwards* and returns the first state set it meets, which is almost always the drawable's own —
O(1) in practice, not O(depth). There was nothing there to win.

### Phase 1 — an instance keeps its slot — **done**

Today "nothing holds an index across a build" is an invariant the design leans on: instances are
rewritten by the walk that produced them, so `retire` can compact and everything downstream is
remade. That invariant is what forces the rebuild.

Invert it. **A placement is allocated a slot the first time it is seen and keeps it until it is
removed.** The slot index is already what the shader reads back at a hit — the TLAS custom index —
so nothing downstream has to change its meaning, only its lifetime.

What falls out:

- `mStanding` and `mStood`, the two per-frame hash maps of matrices, disappear. The previous
  transform lives in the slot, next to the current one.
- `MeshInstance::mPrevious` stops being a `std::optional<osg::Matrixf>`. 72 bytes per instance per
  frame to carry a boolean becomes a bit in a changed-set.
- `makeInstanceRecords` stops comparing two matrices to ask what moved. The walk that moved it says
  so.

Slots make holes when an anchor goes. Take the holes — though not as `mask = 0` rows in the end: a
gap contributes no top-level row at all, and the custom index is written as the *slot* rather than
the row, so nothing downstream has to be renumbered.

#### What it cost to land, and what it taught

**Worth 1.7 ms.** The frame went from 29.55 ms to 27.81 ms across four runs each, and the spread
tightened from 2.7% to 2.3%. `SceneDesc::addInstance` left the profile entirely — it was 2.7% of
frame CPU copying 140-byte placements — `addDrawable` fell from 3.11% to 2.44%, and
`makeInstanceRecords` from 6.54% to 5.91%.

**Phase 1 could not be built without Phase 2's anchor, and this document was wrong to separate
them.** A slot needs an identity, and the only one the extractor had was `identify(path)`, the
hashed node path — which the header is explicit is good enough for *motion history*, where "a
collision costs one object one frame of wrong motion". It is not an identity for a placement.
`CellScene` fetches **one shared template node per model** and extracts it once per reference, so a
hundred crates are a hundred walks down the same path: they collapsed into one slot and the scene
lost more than half its placements, 1239 down to 573. `extract` now takes an anchor — what the
caller is placing — and identity is the anchor and the path together.

That bug had a twin already in the tree. Because placements sharing a path also shared their entry
in `mStood`, an actor built from a shared root read *another actor's* previous transform. Stepped
frames render differently now for that reason and no other: at `--repeat=1`, where the world does
not move, all six views are bit-identical; with the world stepping, three of the six change. A test
holds the mechanism — two placements of one template node under two anchors keep separate histories,
and moving one leaves the other reporting nothing.

**And profiling caught two regressions I wrote myself.** `records.resize()` on a cleared vector and
`mInstanceScratch.assign()` both value-initialise the whole array before it is overwritten: eight
megabytes a frame of stores immediately replaced. `makeInstanceRecords` went *up*, 6.54% to 9.63%,
before that showed. Both are resized and written once now. A scratch buffer that keeps its size
between frames must be resized, never cleared and refilled — which is what the house rule about
persistent scratch already says, read properly.

### Phase 2 — walk only what can move — **not started, and not next**

**The game's own profile says it is not worth building yet.** Recorded over fourteen seconds on the
Seyda Neen quicksave, 67% of the process's CPU is `ioctl` into the graphics driver and 5% is CPU
texture decoding — both of them inside `setScene`. `SceneExtractor::extract`, which this phase
targets, does not reach 1.2%. Rebuilding is the cost; walking is not.

**Its anchor arrived early, because Phase 1 could not be built without it** — `extract` already
takes one, and identity is the anchor and the path together. What is missing is the rest: the
`addAnchor` / `removeAnchor` lifecycle, the classification, and the per-frame question that lets a
static anchor be skipped.

**And the benchmark cannot see this one.** `PosedActors::unplace` used to clear the placements and
copy a static snapshot back in, then walk only the actors — a hand-rolled version of exactly this,
which Phase 1 deleted. So the harness never walked the whole graph, and the cost Phase 2 removes is
only paid by the game. Measuring it needs the game path or a harness that mirrors the way the game
does.


The mirror stops tracking drawables and starts tracking **anchors**: the subtree root the engine
attaches a reference under. There are thousands of those where there are tens of thousands of
drawables, and the owner already knows when one arrives or leaves — `Tracer` on the game side,
`CellScene` in the harness. So `SceneExtractor` gains `addAnchor` / `removeAnchor`, and the full
walk becomes what happens to an anchor once, when it arrives.

Per frame, each anchor is asked two questions:

1. **Does anything under you animate?** Answered once, when the anchor is first walked, by whether
   the subtree holds a `SceneUtil::Skeleton`, a `RigGeometry` or `MorphGeometry`, an
   `osgParticle::ParticleSystem`, or any node carrying an update callback. This is the set the
   engine's own update traversal has already touched this frame.
2. **Did your root move?** One 64-byte comparison against the anchor's last known local-to-world.
   This is the net that catches what question 1 cannot: a reference the engine repositions without
   an animation on it — an item dropped, a body ragdolled, a platform scripted across a room.

An anchor that answers no to both is skipped entirely. Its slots are not touched, its transform is
not recomputed, its drawables are not visited. The per-frame walk becomes O(anchors) plus O(what
moves under the ones that did).

**And mark-and-sweep goes away with it.** `retire`'s epoch stamping exists because the walk is the
only thing that knows what still lives, and its own header names the hazard it guards: identity maps
keyed on raw `osg` pointers, where an address the engine freed can come back as something else. With
removal *declared* rather than inferred, an anchor holds the meshes, materials and textures it
reached for, and dropping it decrements them. That is a soundness improvement, not only a cheaper
one.

### Phase 3 — write only the slots that changed — **not started**

`SceneDesc::getMoved` exists and feeds the motion vectors, so the changed-set Phase 1 promised is
there. Nothing uploads against it yet: `SceneBuffers::place` still writes every `GpuInstance` and
`SceneAcceleration::buildTopLevel` still writes every top-level row.


With a changed-set in hand, the two device-side instance arrays stop being rebuilt. The
`GpuInstance`
table and the `VkAccelerationStructureInstanceKHR` array are allocated once at their high-water mark
and written sparsely — the 6.2 MB memcpy becomes a few kilobytes.

The top level still **rebuilds** every frame rather than refitting. `rtxmw` measured both and found
the rebuild free; ours costs 0.43 ms of device time; and a refit over instances that teleport
degrades the tree that every ray then walks. The build reads a buffer we barely touched, which is
the
whole point.

This phase also retires the duplicated `makeInstanceRecords` on its own: there is one changed-set
and
one pass over it, feeding both consumers.

## 4. What it is predicted to save

Phases 1–3 turn `extract`, `buildTopLevel` and `SceneBuffers::place` from O(instances) into
O(movers). On the measured frame that is 3.9 + 2.6 + 2.1 = **8.6 ms of 19.7**, less whatever floor
those three keep — and the floor is not zero.

Against forecast, so far: Phase 0 was predicted at 0.9 ms and delivered **0.28**; Phase 1 was not
given a figure and delivered **1.7**. Both were measured with `profile.sh` on the same view.

`refitMeshes` is **not** in that number. It is already O(movers) — 167 bottom levels a frame — and
its 3.2 ms is driver-side build setup and a fenced submit. It is the next problem, not this one.

These are predictions from measured costs, not measurements. Each phase lands with a `profile.sh`
run against the same view, and the number goes in `plan.md` §7.6 beside the one it replaced.

## 5. What the investigation found that the plan did not

Three defects came out of profiling and running the game that are not phases of this plan, and two
of them mattered more than the phases did.

**The revision conflated three rates of change — fixed.** `SceneDesc` had one counter, bumped by a
mesh, a texture, a material, a layer or a mask alike, and `Tracer` answered any bump by rebuilding
every acceleration structure and the whole texture array — throwing the temporal history away with
them. OpenMW hands the water a new `osg::StateSet` every frame as it cycles
`textures/water/waterNN.dds`, so the mirror saw two new materials a frame and the game rebuilt
itself **at 2.8 frames a second**. It is now two counters: `getStructureRevision` for meshes and
textures, which earns a rebuild, and `getShadingRevision` for materials, layers and masks, which
earns a 28 KB table write. The game traces at 24–31 ms.

**The mirror ran before the cull — fixed.** `traceFrame` sat between `updateTraversal` and
`renderingTraversals`, so it read node transforms from this frame and skinned vertices, terrain
detail and object paging from the last one: `RigGeometry::getDeformedGeometry` is
`getGeometry(mLastFrameNumber)`, and the cull that writes the current pose had not run. A
character's
bone-attached parts arrived a frame ahead of the arms they hang off. It runs after the traversal
now,
which is also where §2 has it ending up.

**The sky was mirrored — fixed.** It hangs off a `CameraRelativeTransform`, which zeroes its
translation against the eye, so mirroring it into a world-space top level put a dome around the
origin that followed the player. The extractor takes an `osg` traversal mask and the game excludes
`Mask_Sky | Mask_Sun`.

## 6. Where the rebuild actually goes, and what to do about it

Timed inside `setScene` on the quicksave, per rebuild:

| | |
|---|---|
| every bottom-level structure, 1460 meshes | **12 ms** |
| the geometry and shading buffers | **4 ms** |
| the texture array, 327 images | **150–225 ms** |

**The acceleration structures were never the problem.** Nine tenths of a rebuild is the texture
array being made again from nothing, and it is made again because one body texture appeared — the
count climbs by one per rebuild as actors stream in. On top of that the bridge re-describes and
re-computes a `ShadingMap` for all 327 every time, which is the 5% of CPU the game profile shows in
`ShadingMap` and `ColourBlock::read`.

So the work was **an appendable texture array**, not incremental geometry — **done**:

1. `TextureArray::extend` takes new images without disturbing the ones it holds. The descriptor set
   is allocated at the 4096 the layout declares rather than at the scene's count, so appending
   writes the new range and nothing else; the shading buffer grows in blocks with a host-side mirror
   so a grow needs no description it has already seen.
2. `SceneTextures` takes a `from`, so nothing is decoded or shading-estimated twice.
3. `Renderer::extendScene` uses them, and `Tracer` calls it whenever the tables grew rather than
   moved — which `getCompactionRevision` is what distinguishes.
4. **The texture table stopped being compacted at all.** A backend holds it as one bindless array a
   material indexes by position, so reclaiming a slot renumbers the rest and the array is made
   again. `retain` keeps every texture; they go when the scene does.

**Measured on the quicksave: 34 full rebuilds in fourteen seconds became 2, and the game went from
about 4 frames a second to 1080 frames in fourteen — roughly 77 — with the trace at 6.8 ms.**

A test appends a texture, checks the surface wearing it samples *that* one, and checks the texture
already uploaded still reads correctly afterwards; it fails if the descriptor is written to the
wrong element, which is a defect that otherwise produces a plausible picture rather than an error.

**Still open**, and written up in `.notes/ISSUES.md`:
the game's water is the rasterizer's geometry mirrored as an ordinary surface, so it is never
`MaterialKind::Water`; and materials are still keyed on a state-set address that OpenMW recreates.

## 7. Rejected

**Extract during cull.** The obvious idea: OpenMW's cull traversal already walks the graph and
already maintains a transform stack, so ride it and delete our traversal. No — cull culls. It
visits the view frustum, and a ray tracer needs what is behind the camera for reflections, what is
outside it for shadows, and everything for indirect light. A mirror built from the visible set is a
mirror with holes in exactly the places rays go looking.

**Keep the full rebuild and make it fast.** SIMD the matrix chain, flatten the maps, shrink
`MeshInstance`. All of it helps and none of it changes the exponent: the work is still proportional
to a number that grows with view distance, and the thing being computed is still "nothing moved".

**Refit the top level instead of rebuilding it.** Measured free on the device by both this renderer
and the reference; refitting trades tree quality for a build cost that is not the cost.

## 8. How it is kept honest

The existing property — a second pass over an unchanged graph adds no meshes and no materials, which
`scene --twice` prints and a test asserts — generalises to the one this design rests on:

**A second pass over an unchanged graph changes no slots.** Zero anchors walked, zero transforms
written, zero bytes uploaded. That is one assertion, it is exact, and it fails loudly the day
something starts reporting itself dirty when it is not.

Alongside it, the allocation test of §7.3 extends past the renderer to cover extraction, which it
does not reach today — and which is where 47,828 hash-map nodes a frame are currently being handed
back to the allocator and taken again.
