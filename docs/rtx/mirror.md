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

### Phase 0 — carry the transform down instead of recomputing it up

The visitor is already standing at the drawable's depth when it calls `computeLocalToWorld(path)`,
which then walks back to the root and multiplies the chain again — O(depth) per drawable, for a
product every sibling below the same transform shares. A visitor that pushes on the way down and
pops on the way up makes it one multiply per node *entered*. The same applies to
`findOwnStateSet(path)`, which walks the path a second time for the material.

Costs nothing in design terms and is worth about 4.6% of frame CPU on its own — the matrix lines in
the profile (`Matrixf:78`, `Matrix_implementation.cpp:495`, `matrixtransform.hpp:18`) add to 0.9 ms.
**Land this first**, because it is pure win and it makes the phases after it easier to read.

### Phase 1 — an instance keeps its slot

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

Slots make holes when an anchor goes. Take the holes: a retired slot becomes `mask = 0`, costs the
build one skipped instance, and compaction happens on a threshold rather than every frame.

### Phase 2 — walk only what can move

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

### Phase 3 — write only the slots that changed

With a changed-set in hand, the two device-side instance arrays stop being rebuilt. The `GpuInstance`
table and the `VkAccelerationStructureInstanceKHR` array are allocated once at their high-water mark
and written sparsely — the 6.2 MB memcpy becomes a few kilobytes.

The top level still **rebuilds** every frame rather than refitting. `rtxmw` measured both and found
the rebuild free; ours costs 0.43 ms of device time; and a refit over instances that teleport
degrades the tree that every ray then walks. The build reads a buffer we barely touched, which is the
whole point.

This phase also retires the duplicated `makeInstanceRecords` on its own: there is one changed-set and
one pass over it, feeding both consumers.

## 4. What it is predicted to save

Phases 1–3 turn `extract`, `buildTopLevel` and `SceneBuffers::place` from O(instances) into
O(movers). On the measured frame that is 3.9 + 2.6 + 2.1 = **8.6 ms of 19.7**, and Phase 0 takes
another 0.9 off what remains.

`refitMeshes` is **not** in that number. It is already O(movers) — 167 bottom levels a frame — and
its 3.2 ms is driver-side build setup and a fenced submit. It is the next problem, not this one.

These are predictions from measured costs, not measurements. Each phase lands with a `profile.sh`
run against the same view, and the number goes in `plan.md` §7.6 beside the one it replaced.

## 5. Rejected

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

## 6. How it is kept honest

The existing property — a second pass over an unchanged graph adds no meshes and no materials, which
`scene --twice` prints and a test asserts — generalises to the one this design rests on:

**A second pass over an unchanged graph changes no slots.** Zero anchors walked, zero transforms
written, zero bytes uploaded. That is one assertion, it is exact, and it fails loudly the day
something starts reporting itself dirty when it is not.

Alongside it, the allocation test of §7.3 extends past the renderer to cover extraction, which it
does not reach today — and which is where 47,828 hash-map nodes a frame are currently being handed
back to the allocator and taken again.
