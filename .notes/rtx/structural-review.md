# The open issues, traced to their roots

`.notes/ISSUES.md` read one at a time looks like a list of jobs. Traced back it is **four roots**, and
this document is those roots and the order to take them in. It does not restate `plan.md` §10 (slots,
not compaction), §11 (the three roots) or §12 (the seam with the rasterizer); it continues them.

Entries that have been answered are deleted rather than marked, as in `ISSUES.md`. What is kept from
a finished piece is only what the next one needs to know.

| issue | root |
|---|---|
| the doll's scene names a texture with an empty path | **A** — probably expected now; wants a look |
| distant terrain is invisible to the mirror | **B** |
| `Inactive`/`SemiActive` skeletons refuse to move | **B** — closed, bar a test |
| the rasterizer's cull and the mirror both pose | **B** — stale; a real hazard stands where it did |
| `OSGTexture` allocates twice a frame and uploads whole | **C** |
| the world map overlay uploads whole on a cell crossing | **C** |
| `GuiTextures::add` waits on the queue per texture | **C** |
| `RtxTool::Chosen` warns under `-Wmissing-field-initializers` | **I** |

## A. The device never learned "slots, not compaction"

`plan.md` §10 changed what a scene *is*: a table of slots handed out, freed and taken over, where
nothing moves and no index is renumbered. `Rtx::SceneDesc` is built that way throughout, and it now
reports what changed — `getArrivedMeshes`, `getFreedMeshes`, `getArrivedTextures`, `getFreedTextures`,
**disjoint and each naming a slot once**, so a backend may apply arrivals and departures in either
order. Texture slots are already dropped on the strength of that.

**The geometry now lives in blocks that never move.** `Rtx::BlockedBuffer` holds each of the four
shared tables — positions, indices, normals, texture coordinates — as a list of buffers of
`Shaders::VERTEX_BLOCK` or `INDEX_BLOCK` elements, each **made at its full size** so that a block
filled part way is still appendable and every device address already handed out stays good.
`SceneDesc` places runs against the same two constants and never lets a mesh straddle one, so the
address of a run's first element covers the run. A shader is bound *where the blocks are* and
resolves `block[id / BLOCK][id % BLOCK]` through a `GL_EXT_buffer_reference` pointer; the slack is
one block per table, about 4.7 MiB on Balmora.

**The earlier reading of this was wrong, and the instrument is what settled it.** A pointer read does
not move the picture: `verify --against` calls all sixteen views byte-identical with every pointer
fetch live, and `RtxProbeTest` has since agreed with the device directly, across both memory kinds
and a three-block table. The day spent bisecting a whole traced frame was spent because nothing could
ask either question.

**Nothing the game loads reaches a second block.** Balmora is 165,536 vertices and 589,869 indices
against blocks of 262,144 and 1,048,576, so `id / BLOCK` is zero in every scene this fork renders and
`verify` cannot exercise the arithmetic at all. `RtxVisibilityTest.aMeshInTheSecondBlockIsShadedOutOfTheSecondBlock`
is what does: a filler mesh of one whole block of each pushes a lit, textured wall into block one, and
the picture must match the same wall alone. Forcing any of the three block lookups to zero fails it.

**And a picture inside the interface is one of these too.** `setViewScene` is gone: every call that
names a scene takes a slot — `Rtx::sWorld` or one `addViewScene` handed out — so an inventory doll
goes through `SceneUploader` exactly as a cell does and gets the same three branches. A race-creation
slider drag re-walks the same subject every frame and adds nothing, so every redraw after the first
is a placement rather than an acceleration structure and a texture array built from nothing.

**And a cell arriving now appends.** `Rtx::StructureStorage` holds the bottom-level structures in a
list of buffers with a `SpanAllocator` over each, so a released mesh gives its room back and the next
that fits takes it; `extendScene` destroys `getFreedMeshes()`, writes and builds `getArrivedMeshes()`,
and leaves everything else exactly where it is. All four geometry tables are host-written, which is
what makes an arrival a `memcpy` rather than a staged copy to order against the build reading it.
Over nineteen crossings of the streaming route, building fell from **1.1 s to 0.6 s** — 58 ms a
crossing to 32 — and what is left is the meshes that actually arrived, their textures, and the
instance rows. The route's own frame cost is dominated by *reading*: 5.9 s of the 6.5, which is the
harness parsing content files with none of `CellPreloader`'s threads under it.

**One trap, and it cost a device.** A blocked table's address table is *made again* whenever a block
is added, so a handle to it copied once is a handle to a destroyed buffer the first time the scene
grows past a block. `VisibilityInputs` takes the index table fresh every frame; nothing may cache
one. A still never finds this — only a route does.

### The doll's empty texture path

Freed texture slots in a doll's scene are now ordinary — a re-walk sweeps, and a swept slot is
described as the stand-in without being counted — so the empty-path entry is most likely a description
of that rather than of anything drawn wrong. Nobody has looked at a doll to say so.

### What A has left

The picture is unchanged on all sixteen views throughout, which `verify --against` says in sixteen
seconds. What has not been looked at by hand is a race-creation slider drag in the window — the
uploader is asserted to place rather than rebuild, but nobody has watched one.

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

**And this owner is where the last queue round trips live.** `Rtx::Batch` took the scene's load path
down to one submit; `GuiTextures::add` and `write` were left out of it because MyGUI hands over one
texture at a time, through `createTexture` and then `lock`/`unlock` — two waits for one picture, with
nothing between them that knows both are coming. Whatever owns a GUI texture properly is what can
open a batch across the pair.

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

**The batch is in.** `Rtx::Batch` holds one open command buffer and the staging recorded against it,
and `setScene` opens exactly one for a whole cell — every structure, every table, every texture. It
also turned up `TextureArray`'s constructor uploading each texture twice, once in `uploadAll` and
again in the `write` that follows it; `uploadAll` is gone and `write` is the only upload. Balmora
went from 727 submits to one, and its build from **555 ms to 249**; the island route's from 538 to
243. Sixteen views byte-identical, whole suite unchanged. The crossing figure barely moved, because
a crossing that appends builds nothing and its cost is reading content files.

**The geometry is blocked.** See root A: the four shared tables are lists of fixed blocks, the
shader resolves a global id through a pointer out of a table of block addresses, and the picture is
unchanged on all sixteen views. Balmora's build is 231 ms against 249 before it, so the 4.7 MiB of
block slack costs nothing measurable. A cell arriving still rebuilds everything — that is the next
step, and it is now the only thing in the way.

**The append is in.** See root A. What is left of root A is the view scenes, which have no
`extendScene` of their own.

**Root A is done.** What is left of it is one entry in `ISSUES.md` that is probably a description of
correct behaviour, and a window nobody has opened.

**1 — C, the region write.** `RegionTexture`, both backends, `Picture::setRegion`, `GlobalMap` onto
`Picture`. The GUI texture tests plus walking across a cell boundary with the world map open.

**2 — B1, terrain residency.** Verify `QuadTreeWorld::accept` is unchanged by running the OpenGL path
with `distant terrain` on and comparing a frame; verify the mirror by turning `distant terrain` on and
taking a shot of an exterior with ground in it.

**3 — B2 and B3, the traversal counter and the `Inactive` test.** Insurance rather than repair.

**4 — I.** Ten minutes.

## What this does not touch

**The renderer is synchronous end to end** — every submit in `components/rtxvulkan` waits on a fence
before returning. That is why A2 needs no retirement queue, why `GuiTextures::write` can destroy and
recreate freely, and why none of the above has to reason about frames in flight. It is also the single
largest thing standing between this renderer and its frame budget, and it is M12's. The plan above
should not be built in a way that assumes it stays true: **`StructureStorage`, the texture drop and
`Batch`'s kept staging are the three places that will need a fence-keyed retirement list the day it
stops**, and that is written here so that day is a change rather than a bug. `Batch` is already shaped for it: what it
holds is released by `flush`, and a `flush` that signals rather than waits is the same object.
