# The open issues, traced to their roots

`.notes/ISSUES.md` lists thirteen. Read one at a time they look like thirteen jobs. Traced back they
are **six roots, two stale entries and two one-line defects** — and four of the six already have
their answer named somewhere in `plan.md`, half-built and stopped at the device.

This document is the analysis and the plan. It does not restate `plan.md` §10 (slots, not
compaction), §11 (the three roots) or §12 (the seam with the rasterizer); it continues them, and says
where §12 has gone stale because the renderer split overtook it.

| # | issue | root | verdict |
|---|---|---|---|
| 1 | a freed texture slot keeps its image | **A** | design, small |
| 2 | a cell arriving rebuilds every bottom-level structure — 47 ms | **A** | design, large |
| 3 | the rasterizer's cull and the mirror both pose | **B** | **stale** — no cull runs under the ray tracer |
| 4 | `Inactive`/`SemiActive` skeletons refuse to move | **B** | **closed**, bar a test |
| 5 | distant terrain is invisible to the mirror | **B** | design, medium |
| 6 | `check_clang_format.sh` cannot be satisfied | **F** | process |
| 7 | `OSGTexture` allocates twice a frame and uploads whole | **C** | design, medium |
| 8 | a traced view rebuilds its whole scene per redraw | **A** | design, medium |
| 9 | the doll's scene names a texture with an empty path | **E** | **stale**, and the canary needs a name |
| 10 | `freezeFrame` hands back one black texel | **D** | unfinished wiring |
| 11 | no screenshot key on the ray tracing path | **D** | unfinished wiring |
| 12 | `TracedView` warns under `-Wreorder` | **E** | one line |
| 13 | the world map overlay uploads whole on a cell crossing | **C** | design, medium |

The order to do them in is at the end, and it is not this order.

## A. The device never learned "slots, not compaction"

`plan.md` §10 changed what a scene *is*: a table of slots that are handed out, freed and taken over,
where nothing is ever moved and no index is ever renumbered. `Rtx::SceneDesc` is built that way
throughout — `SpanAllocator` behind every variable-length run, a free list behind every table, a
`getArrivedTextures` list saying which slots a walk wrote.

**The backend was never taught the same thing.** `SceneAcceleration` and `SceneBuffers` are
constructed *from a whole scene* and there is no other way to change them: `VulkanRenderer::extendScene`
compares `getMeshRevision()` and, when it moved, throws both away and builds them again. That is
issue 2, and issues 1 and 8 are the same shape one level down — a resource whose lifetime is "the
scene it was built from" rather than "the slot it belongs to".

Four things stand between here and an append.

### A1. Arrivals and departures are a set for textures and a counter for everything else

`SceneDesc` reports `getArrivedTextures()` — the slots a walk wrote, wherever they sit — and that is
exactly what let `TextureArray::write` stop rebuilding. For meshes it reports `getMeshRevision()`, a
number that says *something* arrived and cannot say *what*. For departures it reports nothing at all:
`release` returns a bool.

So:

- `getArrivedMeshes()`, cleared by the same `clearArrivals` the textures use.
- `getFreedMeshes()` and `getFreedTextures()`, filled by `release` and cleared the same way.

This is not new design. It is `plan.md` §11 C's second half, and it is the precondition for
everything below. It is also, on its own, the whole of issue 1: a backend told which texture slots
went can destroy those images, and the descriptor it leaves behind is legal because the array is
already declared `VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT` and nothing indexes a slot no live
material names.

### A2. The geometry buffers are one allocation and move when they grow

`SceneAcceleration` holds `mPositions` (host-visible) and `mIndices` (device-local), each sized once
to `scene.getPositions().size_bytes()`. A cell arriving that does not fit in the holes the last one
left grows the scene's `std::vector`s, and the next construction makes new buffers at new addresses.
Every bottom-level structure was built from an address into the old ones, and the shader reads the
index buffer at a hit through a descriptor naming it.

`SpanAllocator` already takes a **block size** and already refuses to let a run straddle one — the
comment on its constructor says in as many words that this is what makes appending to a device buffer
possible. `SceneDesc` constructs all four allocators with block zero. Nothing uses the feature.

The change is in three places and they must agree on one number:

1. `SceneDesc` constructs `mVertexRuns` and `mIndexRuns` with a block size, and its `std::vector`s
   grow a block at a time rather than to `getEnd()`.
2. `SceneAcceleration` holds `std::vector<HostBuffer>` and `std::vector<Buffer>` of one block each,
   appending a block when the scene reaches into one that does not exist. Existing blocks are never
   reallocated, so no address a structure was built from ever moves.
3. Whatever reads a vertex or an index by global id — the hit shader, `SceneBuffers`' normals and
   texture coordinates — indexes `block[id / blockSize][id % blockSize]`, or is handed an array of
   buffer device addresses and does the arithmetic itself.

The block size is bounded below by the largest single run a mesh can ask for, because
`SpanAllocator::allocate` asserts `count <= block`. A terrain chunk at full detail is a 65×65 grid;
a NIF mesh in Morrowind is smaller than that by a wide margin. **256 Ki vertices a block** is three
megabytes of positions per block and leaves four orders of magnitude of headroom over the largest
run; the index allocator wants its own number, and they need not match.

The cost of blocking is fragmentation at block boundaries: the tail of a block too short for the next
run becomes a hole like any other, and `SpanAllocator` already merges and reuses those. At 256 Ki a
block the tail wasted is a rounding error against a cell.

### A3. Bottom-level storage is one buffer, and structures are created and destroyed as a batch

`buildBottomLevel` sizes one `mBottomLevelStorage` to the sum of every structure in the scene,
creates each at an offset in it, and builds them all in one submit. There is no way to add a
structure to that buffer, and no way to take one out.

Same answer, one level up: **a list of storage blocks with a `SpanAllocator` over each**, and the
per-mesh operations the scene already has:

- `addMesh` → allocate a structure's worth of storage, create it, build it, ask its address once.
- `release` of a mesh → destroy the structure, give its storage back, zero its address.

The renderer is fully synchronous today — every submit in `rtxvulkan` goes through
`CommandPool::submitAndWait` — so a structure destroyed after a `release` cannot be in flight, and no
deferred-destruction queue is needed *yet*. When the frame stops waiting on the queue (M12), this is
one of the things that has to grow a fence-keyed retirement list, and the plan should say so now
rather than discover it then.

What `extendScene` becomes: build the structures for `getArrivedMeshes()`, destroy the structures for
`getFreedMeshes()`, write the texture slots that arrived, drop the texture slots that went, and place.
Nothing else. The top level is already rebuilt every frame and does not care.

### A4. Freeing a texture slot

Falls out of A1 with no further design: `TextureArray::drop(slot)` destroys the image and leaves the
descriptor stale. `plan.md` §11 C records the current behaviour as deliberate — "the array holds a
freed slot's image until something takes the slot over, which is what keeps every descriptor pointing
at something that exists" — and it was the right call while the scene could not name what it freed.
With A1 it stops being one, and the island route's 11 MiB settling point drops by whatever the
departing cells were still holding.

### A5. A traced view rebuilds its scene because pointer identity is unsound

`TracedView::rebuildSubject` throws away the extractor and the `SceneDesc` on every redraw. Its own
comment says why, and the reason is correct: `NpcAnimation::updateParts` frees the body parts that
changed and builds their replacements, and the allocator is free to put a new part exactly where a
retired one was. An identity map keyed on `const osg::Drawable*` then resolves the new part to the
retired part's mesh — the torn figure a change of clothes produced.

**The map holds a raw pointer, and that is the defect.** The same hazard is written down as a caveat
on `SceneExtractor::retire` for the world's scene, where it is survivable only because the whole graph
is walked and swept every frame — a window of one frame rather than of one redraw.

The structural answer is to make pointer identity *true*: **key the identity maps on
`osg::ref_ptr<const osg::Drawable>` and `osg::ref_ptr<const osg::StateSet>`.** An entry in the map
holds its subject alive, so its address cannot be handed to anything else while the entry exists, and
the sweep is what lets go. The map is exactly the thing that must not be fooled and exactly the thing
that can hold the reference. What it costs is that retired geometry outlives its owner by one sweep,
which is what a sweep is for.

With that, `rebuildSubject` becomes what the world's walk already is: clear the placements, walk,
advance, retire — and a slider drag redraws by re-placing the same meshes instead of re-uploading a
character and rebuilding every structure under it. That is issue 8, and it also removes the last
reason the doll's scene ever had a freed texture slot in it, which is issue 9.

### What A buys, as numbers to take

- A cell crossing on the streaming route: **47 ms → the cost of the meshes that actually arrived.**
  Measure it at the same place, on the same route, the frame after it lands.
- Texture memory on the island route: the settling point falls by whatever departed cells hold.
- A race-creation slider drag: from a full rebuild a frame to a placement a frame.
- Worst frame across a crossing, which is the number that matters and the one nobody has yet.

## B. The mirror is still downstream of decisions a rasterizer made

`plan.md` §12 named three things the mirror inherits from cull — skinned vertices, terrain LOD and
object paging — and said the target is to stop being downstream of any of them. Posing was taken
(the mirror carries its own `PoseCull` and hands it the two deforming drawable kinds). The other two
were not.

### B1. Distant terrain, object paging and groundcover are invisible

`Terrain::RootNode::accept` forwards to `Terrain::QuadTreeWorld::accept`, whose first two lines are

```cpp
bool isCullVisitor = nv.getVisitorType() == osg::NodeVisitor::CULL_VISITOR;
if (!isCullVisitor && nv.getVisitorType() != osg::NodeVisitor::INTERSECTION_VISITOR)
    return;
```

`MirrorTraversal` is a plain `osg::NodeVisitor`, so it is neither, and the chunks that would have been
its children are never children of anything: they are entries in a `ViewData` keyed on a camera,
resolved by `loadRenderingNode` inside that call and accepted straight into the visitor that asked.
`ObjectPaging` and `Groundcover` are `ChunkManager`s registered on the same quad tree, so with
`distant terrain` on the mirror sees no ground, no paged objects and no grass — the game runs, and
the world is a floor of nothing with the near cells' objects standing on it.

**The mirror must not become a cull visitor to fix this.** `PoseCull`'s own comment says why, and it
is `Terrain::TerrainDrawable::cull`: it puts the chunk in a render bin and never applies it, so a cull
walk over the whole graph makes the ground vanish rather than appear. It would also pick LOD from an
eye point such a walk has no business having.

So the fix is on the terrain side, and it is the smaller half of what `plan.md` §12 asked for:
**`Terrain::World` grows a residency API that is not a traversal.**

```cpp
/// Every chunk this world holds for `view`, at the detail `viewPoint` asks for, handed to `visitor`.
///
/// **Not a cull.** LOD is chosen by distance from `viewPoint` and nothing is rejected: a ray tracer
/// decides what exists and the answer is everything within the view distance.
virtual void collect(View& view, const osg::Vec3f& viewPoint, osg::NodeVisitor& visitor);
```

`QuadTreeWorld` implements it as the body of its own `accept` with the camera lookup replaced by the
caller's `View`, the water-culling callback dropped, and `entry.mRenderingNode->accept(nv)` reached
for every visitor type. `TerrainGrid` — which the harness uses, and which builds real children —
implements it by traversing itself, which is what it does today.

The mirror then holds a `Terrain::View` of its own, created once, and drives it from the player's
position rather than the camera's. Two properties follow that are worth having on their own account:
the LOD a reflection sees is the LOD the primary ray sees, and the harness and the game reach terrain
through one call instead of two paths that can disagree.

`Terrain::World::preload` already takes a `View*` and a view point, so the shape is not new to that
class; what is new is that the result can be walked by something other than a camera.

**Scope note.** This is a change to `components/terrain`, which the rasterizer also uses. The rule
in `CLAUDE.md` is that the rasterizer is not modified — adding a virtual that `QuadTreeWorld::accept`
is then written in terms of leaves its behaviour bit-identical, and that is the bar to hold to.

### B2. Issue 3 is stale, and there is a real hazard left where it stood

"The rasterizer's cull and the mirror both pose every deforming drawable" was written 43 commits ago,
before `03469d1671` put OpenGL behind `MWRender::Renderer` and `37f35a86a9` gave the ray tracer a
native path. **With `[RTX] enabled` there is no `osgViewer::Viewer`, no cull traversal and no draw
thread**: `RtxRenderer::renderFrame` runs the mirror, traces, draws the GUI and presents. Nothing
skins twice and nothing reads the buffer being written.

What survives is one design property worth naming: **`SceneUtil::Skeleton` and both deforming
drawables key "have I already posed this frame" on a single `unsigned int`, and this fork now has two
independent sequences feeding it.** The world's walk uses `frameNumber + 1`; `TracedView` uses its own
`mRedraws++` for the pose and the game's frame number for the update traversal. They do not collide
today because a `RigGeometry` is cloned per instance and no drawable is reached by both. That is a
property of `NpcAnimation`, not an invariant anything states, and it is one shared subtree away from
being a frozen pose nobody can explain.

Cheap and durable: give `RtxRenderer` one monotonic traversal counter, hand it out to the world walk
and to every traced view, and assert in `MirrorTraversal::begin` that the number it was given is
greater than the last. Then the sequence is a fact rather than a coincidence.

### B3. Issue 4 is closed bar its test

`7c58662b65` gave `SceneUtil::Skeleton` a `markReached` and called it from the mirror, which is what
`SemiActive` was actually asking — a skeleton stops animating once several traversals pass with
nothing reaching it, on the reasoning that only a renderer about to draw reaches one. `Inactive` is
the remaining half of the entry and it is not a defect: `MWMechanics::Actors` sets it only for actors
outside the processing range, and sets those actors' base node mask to zero in the same breath, so
the mirror cannot reach them and would have nothing to pose if it could.

Keep a test that says so — an actor at `Inactive` with a zero node mask contributes no deformed mesh
— so the day the node mask stops being zero, something fails rather than something freezes.

## C. The GUI image path has exactly one verb, and it is "replace everything"

`MyGUI::ITexture` offers `lock`/`unlock` over the whole surface and nothing narrower, and both
backends implement it literally:

- `MyGUIPlatform::OSGTexture::lock` allocates a whole `osg::Image`; `unlock` allocates a whole
  `osg::Texture2D` and hands it the image. A video frame is two allocations and a full re-upload
  every frame. That is issue 7.
- `MyGUIRtx::Texture` keeps its pixels, which is better, but `upload` sends all of them, and
  `Rtx::GuiTextures::write` stages them and waits on the queue.

The consumer that makes this hurt is the world map overlay. `GlobalMap::exploreCell` box-filters one
cell's local-map tile — `local map resolution` 256 down to `global map cell size` 18, which is cheap
— writes it into `mOverlayImage`, and then uploads **the entire overlay**: `cellSize × (maxX−minX+1)`
square, four bytes a texel, a little over two megabytes on Vvardenfell, on the frame a cell arrives.
It is a spike on exactly the frame that already has the most to do. That is issue 13, and issues 7 and
13 are one missing operation.

### C1. A region write, in both backends

MyGUI's interface cannot be changed, so the region write lives beside it:

```cpp
namespace MyGUIPlatform
{
    /// A texture that can be written in part.
    ///
    /// **Not something MyGUI can ask for.** `ITexture` hands out a buffer for the whole surface and
    /// takes it back filled; a picture that changes in one corner has nowhere to say so. Both
    /// backends can do better than the interface allows, and this is where they say it.
    class RegionTexture
    {
    public:
        virtual ~RegionTexture() = default;

        /// `rows` is `height` rows of `width` pixels, four bytes each, tightly packed.
        virtual void writeRegion(int x, int y, int width, int height, std::span<const std::uint8_t> rows) = 0;
    };
}
```

`OSGTexture` implements it by keeping its `osg::Image` across locks — which is the whole of issue 7,
independently of the region — writing the rows into it and calling `dirty()`, so the texture object
uploads the sub-image rather than being replaced. `MyGUIRtx::Texture` implements it by widening into
the same scratch it already has and calling a `writeGuiTexture` that takes a rectangle;
`Rtx::GuiTextures::write` grows one `VkBufferImageCopy` region parameter and nothing else.

`Rtx::Renderer::writeGuiTexture` gains the rectangle rather than gaining an overload —
there is no backward compatibility to keep and two entry points is two things to get wrong.

### C2. One owner for "pixels the game holds, shown in the interface"

`MyGUIPlatform::Picture` is already that class and its doc comment already says so. `GlobalMap` does
not use it: it has its own `createTexture` and its own `upload`, both of which are `Picture`'s job
with a different name.

So: `GlobalMap` holds two `Picture`s, `Picture` gains `setRegion(const osg::Image&, x, y, w, h)`
reaching the backend through a `dynamic_cast<RegionTexture*>` done once per call, and
`GlobalMap::createTexture` and `GlobalMap::upload` go. `exploreCell` then uploads
`18 × 18 × 4` bytes instead of two megabytes, on the frame it changed one cell.

The `memcmp` that recognises a repaint changing nothing stays: it is a kilobyte of comparison against
an upload, and it is now a kilobyte against a much smaller upload, which is still the right trade.

### C3. What is deliberately not proposed here

Making the overlay live only on the device, with the CPU copy dropped. It cannot: `GlobalMap::write`
serialises `mOverlayImage` into the savegame, so a main-memory copy is the source of truth and the
device copy is the derived one. That is the right way round and it should stay.

## D. A frame that exists on the device has no way back into the game

Two entries, one cause, and neither is a design flaw — they are wiring that was left for later and
should be finished, because both are silently wrong rather than absent.

- **`freezeFrame` hands back one black texel.** The loading screen puts it up as the backdrop and
  fades from black instead of from the world the player was standing in.
- **`saveScreenshot` logs a warning.** The screenshot key does nothing on the ray tracing path.

Everything either needs already exists. `Rtx::Renderer::readPixels` reads the output-resolution
target and `RtxRenderer::capture` already uses it for the savegame thumbnail;
`SceneUtil::writeScreenshotToFile` is a free function over an `osg::Image` with no GL in it, and
`SceneUtil::AsyncScreenCaptureOperation` puts it on the work queue. What is GL-only is
`osgViewer::ScreenCaptureHandler`, which is the *capture* half and the half the ray tracer already
has its own version of.

So:

- `freezeFrame` reads the target into an `osg::Image` held on the renderer and sets `mFrozenFrame`
  from it. A load screen is exactly the moment a full readback is affordable.
- `saveScreenshot` reads the target, wraps it, and hands it to the same async writer the OpenGL
  renderer hands `osgViewer`'s captures to, honouring `Settings::general().mScreenshotFormat` and
  the screenshot path from `RendererSpec` — so the two renderers write the same files to the same
  place with the same names.

That leaves `OPENMW_RTX_SHOT`'s `keep()` as a separate, deliberate thing: it writes numbered PNGs for
a profiling corpus and is not the screenshot key.

## E. Two that are exactly what they look like

**Issue 12** — `MWRender::Rtx::TracedView`'s constructor initialises `mWidth` before `mSubjectMask`
while the declarations run the other way. Reorder the initialiser list to match the declarations.
One line, and every translation unit including `tracedview.hpp` stops warning.

**Issue 9 is stale, and its lesson is not.** It was logged at `9bbcbbcf30`, when a traced view kept
its extractor across redraws and called `retire()`; `SceneDesc::release` empties a freed texture
slot's path, and `SceneTextures` describes every slot of a scene it is asked to build whole — so a
freed slot arrived at the backend as one unreadable texture and one grey stand-in. `9a3b17ff1e` made
each redraw build a fresh scene, which cannot have a freed slot in it, so the symptom is gone.

The conflation is still in the code and still latent: **`SceneTextures` cannot tell "a slot the scene
freed" from "a file the decoder would not have", and reports neither by name.** `getUnreadable()` is
a count, `RtxRenderer` logs it as a count, and a count nobody can follow to a file is a canary that
cannot be answered. Two small changes make it one again:

- `describe` skips a slot whose path is empty, describing it as the stand-in without counting it.
- The unreadable ones are logged once each with their path, at `Debug::Warning`, on the frame they
  are described — which is a load and not a frame path.

## F. The formatter

`CI/check_clang_format.sh` runs whatever `clang-format` is on `PATH`; `.gitlab-ci.yml` sets
`CLANG_FORMAT: clang-format-14` and installs it from Debian. This box has 22, and the two disagree.

The issue as logged says the two versions cannot both be satisfied, and that is true and permanent:
clang-format's output is not stable across majors and no `.clang-format` makes it so. **A gate whose
answer depends on what happens to be installed is not a gate.** This fork does not upstream, so:

- Adopt the version the tree is already formatted for and say so in `.clang-format` next to
  `Standard: c++20`, as a comment naming the major.
- Have `CI/check_clang_format.sh` read `clang-format --version`, compare the major against a single
  constant in the script, and fail with "this tree is formatted by clang-format N; you have M" rather
  than producing a diff nobody can act on.
- Keep CI's installed version and the constant tied together, so a bump that touches one and not the
  other fails naming both.

**Which version, decided by sweeping the whole tree rather than the files the issue named.** Under 14
eleven files differ, every one of them written or touched by this fork. Under 22 about a hundred and
eighty do, almost all of them upstream's — `esm4`, `opencs`, `detournavigator`. The tree is an
upstream tree formatted at 14 with a fork's worth of files formatted at 22 on top, so 14 is what it
is already formatted for and 22 would mean reformatting upstream's code to gain nothing but a newer
formatter. 14's one visible cost is that a pure-virtual whose signature wraps puts `= 0` on its own
line; that is what upstream already lives with.

Ubuntu 24.04 packages `clang-format-14`, so CI needs no new mechanism — the gate checking the major
is what made the version reproducible, not how it is installed. Arch does not package it, and
`pipx install 'clang-format==14.*'` is a wheel with a real binary in it, which is what the failure
message points a developer at.

## The plan

Ordered by what unblocks what, not by size. Each step is landable on its own and leaves the tree
working.

**1 — F, the formatter.** Half an hour, and until it is done every other step's diff is arguable.
Verify with `CI/check_clang_format.sh` and `CI/check_file_names.sh`.

**2 — E, the two small ones.** The `-Wreorder` fix, the empty-path skip, and the per-file warning
for an unreadable texture. Verify with `components-tests --gtest_filter=*TextureBuilder*` plus a new
case: a scene with a freed texture slot describes it as the stand-in and reports zero unreadable.

**3 — D, the frame's way back.** `freezeFrame` and `saveScreenshot`. No test can assert what a load
screen looks like; assert instead that `freezeFrame` returns a texture of the target's extents and
that the screenshot writer is reached with an image of the right size. Look at one load in the window
because that is the only thing that shows it.

**4 — A1, arrivals and departures as sets.** `getArrivedMeshes`, `getFreedMeshes`,
`getFreedTextures`, cleared by `clearArrivals`. Pure addition; nothing reads them yet. Verify with
`components-tests --gtest_filter=*SceneDesc*`, extending the existing release and reuse cases rather
than adding a file: a walk that frees a cell names exactly the slots it freed, in any order, once
each.

**5 — A4, freeing a texture slot.** `TextureArray::drop`, driven from `getFreedTextures` through
`SceneUploader`. Small, self-contained, and it is the proof that A1's lists are right. Measure the
island route's texture bytes at the same seven crossings the 685-texture figure came from.

**6 — A5, sound identity, and the traced views with it.** `osg::ref_ptr` keys in `SceneExtractor`,
then `TracedView::rebuildSubject` becomes an incremental re-walk. Verify with the existing
`components-tests --gtest_filter=*SceneExtractor*` plus a case that frees a drawable and allocates
another, asserting the second does not resolve to the first's mesh. Look at a race-creation slider
drag in the window, which is what the frame time was.

**7 — A2 and A3, blocks.** The large one, and it wants its own sequence:

   a. `SpanAllocator` with a block size in `SceneDesc`, with the host vectors growing a block at a
      time. Nothing on the device changes; the existing tests should pass unaltered, and a new one
      asserts a run never straddles a block and that a block's tail is reused.
   b. `SceneAcceleration`'s position and index buffers become block lists, and everything that reads
      a vertex or an index by global id is taught the arithmetic. Still a full rebuild per arrival;
      the picture must be pixel-identical, which `openmw-rtxtool shot` over the views in
      `files/rtx/views.cfg` is exactly the instrument for.
   c. Bottom-level storage becomes a block list with per-mesh create and destroy.
   d. `extendScene` builds `getArrivedMeshes` and destroys `getFreedMeshes` instead of rebuilding.

   Measure at (d): the crossing on the streaming route, median and worst frame, against 47 ms.

**8 — C, the region write.** `RegionTexture`, both backends, `Picture::setRegion`, `GlobalMap` onto
`Picture`. Verify with the GUI texture tests in `components-tests` — a region write leaves the rest of
the texture alone, and a widened one-channel region widens correctly — and by walking across a cell
boundary with the world map open.

**9 — B1, terrain residency.** `Terrain::World::collect`, `QuadTreeWorld`'s implementation written by
moving its own `accept` body, the mirror holding a `View` of its own. Verify that `QuadTreeWorld::accept`
is unchanged in behaviour by running the OpenGL path with `distant terrain` on and comparing a frame;
verify the mirror by turning `distant terrain` on and taking a `shot` of an exterior that has ground
in it.

**10 — B2 and B3, the traversal counter and the `Inactive` test.** Small, and last because they are
insurance rather than repair.

## What this does not touch

**The renderer is synchronous end to end** — every submit in `components/rtxvulkan` waits on a fence
before returning. That is why A3 needs no retirement queue, why `GuiTextures::write` can destroy and
recreate freely, and why none of the above has to reason about frames in flight. It is also the
single largest thing standing between this renderer and its frame budget, and it is M12's, not this
document's. The plan above should not be built in a way that assumes it stays true: A3 and A4 are the
two places that will need a fence-keyed retirement list the day it stops, and that is written here so
that day is a change rather than a bug.
