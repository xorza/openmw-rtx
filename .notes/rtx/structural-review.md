# The open issues, traced to their roots

`.notes/ISSUES.md` read one at a time looks like a list of jobs. Traced back it is **five roots**, and
this document is those roots and the order to take them in. It does not restate `plan.md` §10 (slots,
not compaction), §11 (the three roots) or §12 (the seam with the rasterizer); it continues them.

Entries that have been answered are deleted rather than marked, as in `ISSUES.md`. What is kept from
a finished piece is only what the next one needs to know.

| issue | root |
|---|---|
| a cell arriving rebuilds every bottom-level structure — 47 ms | **A** |
| a traced view rebuilds its whole scene per redraw | **A** |
| the doll's scene names a texture with an empty path | **A** — probably expected now; wants a look |
| distant terrain is invisible to the mirror | **B** |
| `Inactive`/`SemiActive` skeletons refuse to move | **B** — closed, bar a test |
| the rasterizer's cull and the mirror both pose | **B** — stale; a real hazard stands where it did |
| `OSGTexture` allocates twice a frame and uploads whole | **C** |
| the world map overlay uploads whole on a cell crossing | **C** |
| a `buffer_reference` read of normals changes the picture | **A** |
| `GuiTextures::add` waits on the queue per texture | **H** |
| `RtxTool::Chosen` warns under `-Wmissing-field-initializers` | **I** |

## A. The device never learned "slots, not compaction"

`plan.md` §10 changed what a scene *is*: a table of slots handed out, freed and taken over, where
nothing moves and no index is renumbered. `Rtx::SceneDesc` is built that way throughout, and it now
reports what changed — `getArrivedMeshes`, `getFreedMeshes`, `getArrivedTextures`, `getFreedTextures`,
**disjoint and each naming a slot once**, so a backend may apply arrivals and departures in either
order. Texture slots are already dropped on the strength of that.

**The geometry side of the backend still cannot be appended to.** `SceneAcceleration` and
`SceneBuffers` are constructed *from a whole scene*, so `VulkanRenderer::extendScene` throws both away
whenever `getMeshRevision()` moves. Three things stand between here and an append.

### A1. The geometry buffers are one allocation and move when they grow

`SceneAcceleration` holds `mPositions` (host-visible) and `mIndices` (device-local), each sized once
to the scene. A cell that does not fit the holes the last one left grows the scene's vectors, and the
next construction makes new buffers at new addresses.

`SceneDesc` already places its runs against a block: **256 Ki vertices and 1 Mi indices**, shared with
the shaders as `Shaders::VERTEX_BLOCK` and `INDEX_BLOCK`, and no run ever straddles one. A mesh longer
than a block is **thrown on by name rather than asserted** — a vertex count comes out of a content
file, and a run across two blocks is a wild write rather than a wrong picture. The host vectors are
deliberately *not* rounded up to a block: `getPositions()` is what the backend uploads, and the tail
would be megabytes of nothing sent to the device.

What remains is the device side: `SceneAcceleration`'s positions and indices, and `SceneBuffers`'
normals and texture coordinates, become lists of blocks; whatever reads a vertex or an index by global
id does `block[id / blockSize][id % blockSize]`.

**This was written in full and reverted; `7b-backend.patch` holds it.** What it established:

- **The block plumbing is bit-exact.** With the blocks in place and the shader still reading through
  storage-buffer descriptors, the renderer reproduced the baseline **byte for byte on all sixteen
  views**. The arithmetic, the sizing, the per-mesh addresses and the deformed-mesh writes are right.
- **Reading through a pointer is what moved the picture.** Swapping only the normal fetch to
  `GL_EXT_buffer_reference` changes the image; keeping the load and discarding its value leaves it
  byte-identical. Ruled out: array stride (SPIR-V says 12, which is right), load width (three floats
  behaves as a `vec3`), the block index (the scenes that differ are single-block), buffer size, and
  the uninitialised tail. Instruction counts are otherwise identical, so it is not reassociation.
- **Positions need no shader change at all** — they are a build input and the target of the
  per-frame host write, and a hit reads its triangle's vertices back out of the structure through
  `ALLOW_DATA_ACCESS`. Indices, normals and texture coordinates are the coupled half, at set 0
  bindings 3, 4 and 5.

**And the probe now says the pointer itself is innocent.** `RtxProbeTest` reads one buffer of packed
`vec3`s both ways — a storage-buffer descriptor and a `buffer_reference` at `buffer_reference_align =
4`, the same declaration `7b-backend.patch` used — and the two agree exactly, in resizable-BAR
host-visible memory and in staged device-local memory alike. So the defect is not "a pointer read
returns different data"; it is in what the patch built around it, and the untested part of that is
the **address table**: `normalBlocks[vertex / VERTEX_BLOCK]`, a `uint64_t` read out of a `HostBuffer`
through a descriptor. That is where to look first, and the probe is where to ask.

**The fallback, if it turns out to be the pointer after all.** Reach the blocks through a
**partially-bound descriptor array** of storage buffers indexed by block: no pointers, the same
arithmetic, and a binding model the texture array already uses.

### A2. Bottom-level storage is one buffer, created and destroyed as a batch

`buildBottomLevel` sizes one `mBottomLevelStorage` to the sum of every structure in the scene, creates
each at an offset in it and builds them in one submit. There is no way to add one or take one out.

Same answer one level up: **a list of storage blocks with a `SpanAllocator` over each**, and the
per-mesh operations the scene already has — `addMesh` allocates a structure's worth, creates, builds
and asks its address once; a released mesh destroys the structure and gives the storage back.

`extendScene` then becomes: build `getArrivedMeshes()`, destroy `getFreedMeshes()`, write the texture
slots that arrived, drop the ones that went, place. Nothing else. The top level is rebuilt every frame
and does not care.

The renderer is synchronous, so a structure destroyed after a release cannot be in flight and no
deferred-destruction queue is needed *yet*. When the frame stops waiting on the queue (M12) this is
one of the two places that has to grow a fence-keyed retirement list.

### A3. A view scene can only be replaced

`TracedView` now re-walks incrementally — the identity maps own their keys, so a body part freed and
replaced at the same address cannot be mistaken for its predecessor, and the texture descriptions are
held across redraws that changed no texture. The mirror is incremental and **the upload is not**:
`setViewScene` tears down the acceleration structures, the buffers and the texture array and makes
them again, because a view scene has no `placeScene` or `extendScene` of its own.

That is the doll issue in full. Give a view scene the same three branches the world's scene has; it is
the same code and it wants A1 and A2 under it first.

Freed texture slots in a doll's scene are now ordinary — a re-walk sweeps, and a swept slot is
described as the stand-in without being counted — so the empty-path entry is most likely a description
of that rather than of anything drawn wrong. Nobody has looked at a doll to say so.

### What A buys, as numbers to take

- A cell crossing on the streaming route: **47 ms → the cost of the meshes that actually arrived.**
  Same place, same route, the frame after it lands; median and worst.
- A race-creation slider drag: from a rebuild a frame to a placement a frame.

## B. The mirror is still downstream of decisions a rasterizer made

`plan.md` §12 named three things the mirror inherits from cull. Posing was taken. The rest:

### B1. Distant terrain, object paging and groundcover are invisible

`Terrain::RootNode::accept` forwards to `Terrain::QuadTreeWorld::accept`, whose first two lines return
for any visitor that is not a cull or an intersection visitor. `MirrorTraversal` is neither, and the
chunks that would have been its children are never children of anything: they are entries in a
`ViewData` keyed on a camera, resolved inside that call and accepted straight into the visitor that
asked. `ObjectPaging` and `Groundcover` hang off the same quad tree, so with `distant terrain` on the
mirror sees no ground, no paged objects and no grass.

**The mirror must not become a cull visitor to fix this.** `Terrain::TerrainDrawable::cull` puts the
chunk in a render bin and never applies it, so a cull walk over the whole graph makes the ground
vanish rather than appear — and it would pick LOD from an eye point such a walk has no business
having.

So the fix is on the terrain side: **`Terrain::World` grows a residency API that is not a traversal.**

```cpp
/// Every chunk this world holds for `view`, at the detail `viewPoint` asks for, handed to `visitor`.
///
/// **Not a cull.** LOD is chosen by distance from `viewPoint` and nothing is rejected: a ray tracer
/// decides what exists and the answer is everything within the view distance.
virtual void collect(View& view, const osg::Vec3f& viewPoint, osg::NodeVisitor& visitor);
```

`QuadTreeWorld` implements it as the body of its own `accept` with the camera lookup replaced by the
caller's `View`, the water-culling callback dropped, and the rendering node accepted for every visitor
type. `TerrainGrid` — which the harness uses — implements it by traversing itself, which is what it
does today. The mirror holds a `View` of its own, driven from the player's position rather than a
camera's, so the LOD a reflection sees is the LOD the primary ray sees and the harness and the game
reach terrain through one call instead of two that can disagree.

**Scope note.** This touches `components/terrain`, which the rasterizer also uses. Adding a virtual
that `QuadTreeWorld::accept` is then written in terms of leaves its behaviour bit-identical, and that
is the bar.

### B2. Two traversal sequences feed one "have I posed this frame" counter

The logged issue — cull and the mirror both posing — is stale: with `[RTX] enabled` there is no
viewer, no cull traversal and no draw thread. What survives is worth naming. `SceneUtil::Skeleton` and
both deforming drawables key "already posed" on a single `unsigned int`, and this fork feeds it from
two independent sequences: the world's walk uses `frameNumber + 1`, a traced view uses its own redraw
count. They do not collide because a `RigGeometry` is cloned per instance and no drawable is reached
by both — a property of `NpcAnimation`, not an invariant anything states, and one shared subtree away
from a frozen pose nobody can explain.

Give `RtxRenderer` one monotonic traversal counter, hand it to the world walk and to every traced
view, and assert in `MirrorTraversal::begin` that the number is greater than the last.

### B3. The `Inactive` half is closed, and wants a test

`Inactive` is not a defect: `MWMechanics::Actors` sets it only for actors outside the processing
range and zeroes their base node mask in the same breath, so the mirror cannot reach them. Keep a test
saying so — an actor at `Inactive` with a zero node mask contributes no deformed mesh — so the day the
mask stops being zero something fails rather than freezes.

## C. The GUI image path has exactly one verb, and it is "replace everything"

`MyGUI::ITexture` offers `lock`/`unlock` over the whole surface and nothing narrower, and both
backends implement it literally: `MyGUIPlatform::OSGTexture::lock` allocates a whole `osg::Image` and
`unlock` a whole `osg::Texture2D`, so a video frame is two allocations and a full re-upload every
frame; `MyGUIRtx::Texture` keeps its pixels but sends all of them.

The consumer that makes this hurt is the world map. `GlobalMap::exploreCell` box-filters one cell's
local-map tile — 256 down to 18, which is cheap — and then uploads **the entire overlay**, a little
over two megabytes on Vvardenfell, on the frame a cell arrives. Both entries are one missing
operation.

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

`OSGTexture` implements it by keeping its `osg::Image` across locks — which is the whole of the
allocation entry, independently of the region — writing the rows and calling `dirty()`, so the texture
uploads the sub-image rather than being replaced. `MyGUIRtx::Texture` widens into the scratch it
already has; `Rtx::GuiTextures::write` grows one `VkBufferImageCopy` region and nothing else.
`Rtx::Renderer::writeGuiTexture` gains the rectangle rather than gaining an overload.

### C2. One owner for "pixels the game holds, shown in the interface"

`MyGUIPlatform::Picture` is already that class. `GlobalMap` does not use it: it has its own
`createTexture` and `upload`, both of which are `Picture`'s job under another name. So `GlobalMap`
holds two `Picture`s, `Picture` gains `setRegion`, reaching the backend through one
`dynamic_cast<RegionTexture*>` per call, and `GlobalMap::createTexture` and `upload` go. `exploreCell`
then uploads 18 × 18 × 4 bytes instead of two megabytes.

The `memcmp` that recognises a repaint changing nothing stays: a kilobyte of comparison against an
upload is still the right trade.

**Deliberately not proposed:** dropping the CPU copy and keeping the overlay only on the device.
`GlobalMap::write` serialises `mOverlayImage` into the savegame, so main memory is the source of truth
and the device copy is derived. That is the right way round.

## H. Every resource is its own queue round trip

`Rtx::GuiTextures::add` clears each new texture through a submit it waits on. That is the logged
entry; it is one of fourteen `submitAndWait` sites, and what makes it structural is the ones inside a
loop:

- **`Texture::Texture` submits and waits per texture.** Balmora's 361 textures are 361 round trips
  through the driver on the frame the cell lands.
- `uploadBuffer` submits and waits per buffer.
- `GuiTextures::add` and `write`, once per texture the interface creates or changes.

`commands.hpp` already says it — *"One submit per call, which is right for a load path and wrong for
anything else"* — and it is not right for a load path either, once that is a cell boundary.

**This is not M12's asynchrony and must not wait for it.** M12 is about the frame no longer waiting on
the queue; this is about not asking the queue three hundred times to do one thing. They compose: the
batch that submits once and waits is the same object that later submits once and does not.

- `CommandPool::batch()` hands back a `Batch` holding one open command buffer. `flush()`, and the
  destructor, submits and waits once.
- **The batch owns the staging.** `uploadBuffer` and `Texture` keep a staging buffer as a local and
  rely on the wait happening before it leaves scope; batched, it has to live until the flush. That is
  the whole of the design content.
- `Texture` and `uploadBuffer` take a `Batch&` where they take a `CommandPool&`.

Measured against the build figure the bench prints: 410 ms on the island route, 704 ms at Balmora.

## I. One that is exactly what it looks like

`RtxTool::Chosen` is designated-initialised in `chooseView` without `mView` or `mNote`. Those two are
assigned a few lines later, which is the actual defect: the aggregate is built in two stages, so its
initialiser cannot name everything and the compiler is right to say so. Resolve the view first and
build it once.

## The plan

Ordered by what unblocks what. Each step is landable on its own and leaves the tree working.

**The two instruments are in.** `openmw-rtxtool verify --against=<directory>` renders all sixteen
views with upscaling off, one frame at seed zero, and reports each against a previous run as a worst
channel delta and a share of the pixels; it exits non-zero when anything moved, and the whole run
takes sixteen seconds. `apps/components_tests/rtx/probe.cpp` puts one question to the device through
`shaders/probe.comp`, which the build compiles only when the tests are on.

Both were checked against themselves. Two runs of one build call all sixteen views the same; a tone
curve scaled by 1.004 is reported as *worst 1 of 255* on between 5% and 93% of each view, and by 1.08
as *worst 3 to 9* on nearly every pixel. That difference between the two is the finding a bare
*differs* could not carry.

**1 — H, the batch.** Independent of everything, and it shortens every load the steps below are tested
through. Whole suite unchanged; the bench's build figure at Balmora and on the island route.

**2 — A1, the geometry blocks.** `7b-backend.patch` is the starting point and it is known bit-exact
apart from the normal fetch. Start at the address table, which the probe has not yet been asked
about; the descriptor-array fallback stands if it turns out to be the pointer. Still a full rebuild
per arrival. The picture must be unchanged, which is now a command.

**3 — A2, bottom-level storage, then `extendScene`.** Build the arrivals, destroy the departures.
Measure the crossing on the streaming route, median and worst, against 47 ms.

**4 — A3, view scenes get the same three branches.** Look at a race-creation slider drag in the
window; that is what the frame time was.

**5 — C, the region write.** `RegionTexture`, both backends, `Picture::setRegion`, `GlobalMap` onto
`Picture`. The GUI texture tests plus walking across a cell boundary with the world map open.

**6 — B1, terrain residency.** Verify `QuadTreeWorld::accept` is unchanged by running the OpenGL path
with `distant terrain` on and comparing a frame; verify the mirror by turning `distant terrain` on and
taking a shot of an exterior with ground in it.

**7 — B2 and B3, the traversal counter and the `Inactive` test.** Insurance rather than repair.

**8 — I.** Ten minutes.

## What this does not touch

**The renderer is synchronous end to end** — every submit in `components/rtxvulkan` waits on a fence
before returning. That is why A2 needs no retirement queue, why `GuiTextures::write` can destroy and
recreate freely, and why none of the above has to reason about frames in flight. It is also the single
largest thing standing between this renderer and its frame budget, and it is M12's. The plan above
should not be built in a way that assumes it stays true: **A2 and the texture drop are the two places
that will need a fence-keyed retirement list the day it stops**, and that is written here so that day
is a change rather than a bug.
