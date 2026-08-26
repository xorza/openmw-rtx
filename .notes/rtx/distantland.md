# Distant land

Exteriors rendered past the cells the game keeps loaded: the active grid at full detail as it is
today, and a further ring of cells — four out — as terrain LOD. Companion to `plan.md` (the route)
and `backends.md` (the two backends); this one is about what has to change and in what order.

Everything in §2 and §3 was read or measured rather than assumed, and each claim says which.

## 1. What is being asked for

- The active grid keeps exactly what it has now: full-detail chunks, real ground textures, blend
masks, every static and every actor.
- Beyond it, out to **four cells** in every direction, ground exists and is drawn at decreasing
detail. That is a 9×9 block of cells against the 3×3 the game processes.
- Distant statics come with it, because a hillside with no towers on it is not distant land.
- It costs what LOD is supposed to cost. Nine times the area must not be nine times the frame.

## 2. What already exists

**`Terrain::QuadTreeWorld` is the whole LOD mechanism and it is already correct.**
`DefaultLodCallback::isSufficientDetail` (`quadtreeworld.cpp:54`) does exactly the split being asked
for: a node that intersects the active grid returns `Deeper` until it is at leaf size, so the active
grid is never LOD'd; everything else picks a level from its distance and stops. Leaves are an eighth
of a cell (`mMinSize`, `quadtreeworld.cpp:293`).

**`QuadTreeWorld::collect` is a ray tracer's question, not a cull.** It was written for this
renderer: it takes a view the caller owns, resolves it always, and hands over every chunk within the
view distance with nothing rejected by a frustum. `Rtx::TerrainResidency` holds that view — one
view, so the reflection and the primary ray cannot disagree about a chunk's level — and
`RenderingManager` follows it and sets its view point to the eye every frame
(`renderingmanager.cpp:892`). It is tested:
`aPagedWorldsGroundReachesTheMirrorAndOnlyBecauseItWasAskedFor`, with the converse test that a
`TerrainGrid` offers no residency so the ground is never placed twice.

**Distant statics arrive through the same call.** `loadRenderingNode` walks `mChunkManagers` and
asks each for its chunk, so `ObjectPaging`'s merged statics and groundcover come back from `collect`
alongside the ground.

**The extractor already understands a layer stack.** `resolveTerrainMaterial` reads a
`TerrainDrawable`'s passes into `MaterialKind::Terrain` with a `MaterialLayer` apiece — diffuse,
transform, blend mask — and the shader sums them at a hit with `maskWeight`.

So the paging, the LOD, the delivery and the near-field shading are all in place. What is missing is
narrower than it looks.

## 3. What blocks it

### 3.1 View distance is smaller than a cell

`viewing distance = 7168.0` (`settings-default.cfg:19`) against a cell of 8192 units.
`mViewDistance` is what `isSufficientDetail` cuts traversal at, and the active grid is exempt from
the cut — so **how much appears outside the active grid depends on how wide that grid is**, and in
the game it is almost nothing: a 3×3 grid reaches 1.5 cells from its centre, which 7168 units barely
leaves.

Measured at Balmora, where the grid is the game's: `--distant-terrain` renders the same 199,821
triangles as without it, and the frame differs in 940 pixels out of 2,073,600 — the difference being
the same ground packed into quadtree chunks rather than grid chunks (84 fewer materials, 144 fewer
instances). No distance appears because none is asked for.

**The harness stages one cell at a time**, so its grid is one cell wide and 7168 units leaves it at
once. One staged cell at the default distance already produces 45 chunks, the widest of them a whole
cell across — which is exactly the size §3.2 is about, and is why that section bites today rather
than in the future.

### 3.2 Distant chunks are textured by an OpenGL render target

`ChunkManager::createChunk` (`chunkmanager.cpp:258`):

```cpp
bool useCompositeMap = chunkSize >= mCompositeMapLevel;
```

with `composite map level = 0`, so `mCompositeMapLevel` is 1 and **every chunk of a cell or larger
is textured by one composite map**. A composite map is an `osg::Texture2D` with no image, drawn by
`Terrain::CompositeMapRenderer` — an `osg::Drawable` that renders the layer stack into a texture
through OpenGL.

**The RTX path has no GL context.** That is the fork's central decision, not an oversight: with
`[RTX] enabled` there is no `osgViewer` graphics window and no interop.

What that costs was measured rather than assumed, and it is not what it looks like.
`Surface::Material::setTexture` clears a role an imageless texture was bound to, so the composite
pass describes no diffuse at all; `resolveTerrainMaterial` skips the layer, runs out of layers, and
returns `sNoIndex`; and the chunk is **placed carrying no material**. Nothing ever reaches the
texture table, so `SceneTextures::getUnreadable` counts none of it — the number this section first
expected to watch never moves.

**Measured, before it was fixed:** one staged cell at the default view distance places five of
these, and four cells of distance places thirty-two. `instancerecord.cpp:63` turns a missing
material into a null one, so nothing reads past the material buffer — the chunk simply draws
untextured, which is grey ground reached by a different route than the stand-in texture this section
first guessed at.

### 3.3 Nothing is culled, so distance is paid for in full

The RT path has no frustum: rays go everywhere, so every chunk `collect` hands over is in the
acceleration structure whether or not it is behind the eye. Nine times the area is nine times the
*coverage*, and only LOD keeps it from being nine times the geometry — which is what LOD is for, and
it holds: four cells is known affordable and is a starting radius rather than a ceiling. What this
does mean is that the radius is paid for in full at every camera angle, so it is a memory and
structure question rather than a visibility one, and §6 step 6 is where that is held flat.

### 3.4 Fog was derived from the viewing distance, so distance could not be seen

`fogbuilder.cpp` measured extinction as a half-life across `Settings::camera().mViewingDistance`, so
light from beyond about 7168 units was gone whatever the acceleration structure held. **Ground built
four cells out then rendered identically to none at all** — which is what step 4 measured before
this was found: 0, 4 and 12 cells all gave 67.7778% of primary rays hitting, and the frames differed
by at most 30 of 255, which is the light grid moving rather than land appearing.

**Landed.** `fogExtinction` takes the distance its half-life is measured across, and
`Rtx::distantLandReach()` is the one number both the ground and the air are built to — `distant land
cells` in units, or `viewing distance` where that is zero. **Interiors keep `viewing distance`**,
because a room is measured against what the original engine measured it against and a cellar must
not clear because the sky got bigger; `arkngthand` is byte-identical across the change and both
exteriors are not, which is what says the split is where it should be.

### 3.5 A world walked twice swept the ground a quad tree hides

Fixed, and it is the reason none of the above could be seen. `SceneExtractor::extract` took the
residency as an argument, and a frame is walked by more than one owner: the harness's actor stepper
and `StagedWorld` walk the same root from the same extractor, and the game walks its precipitation
node beside its world. The sweep is global — anything a walk did not meet is retired — so the walk
that did not hand over what the graph does not parent dropped every chunk the other had placed. The
ground reached the mirror on the first frame and was gone by the second, which reads as a town
standing on open sea while the scene, the top level and the instance count all look correct.

**The residency now belongs to the extractor.** `follow` says once what hides its geometry,
`extractWorld` is the walk that means the whole of it, and `extract` is a subtree that cannot reach
the residency — which is what the precipitation node needs, since bringing it in would place the
ground twice. No caller can be the one that forgets, because there is no longer an argument to
forget.

`RtxCrossingTest.aPagedWorldsGroundSurvivesTheFramesAfterIt` is what says so: residents on and
terrain paged, it counted 60 chunks at frame one and 0 at frame two before the change. Its sibling
walks a region with nobody in it, which is exactly why this went unseen — with no actors the stepper
is the one that always carried the residency.

## 4. The design

### D1 — the RT path never asks for a composite map

`composite map level` is raised, under RTX, above the largest chunk the quadtree will build, so
`useCompositeMap` is false everywhere and **every chunk arrives as a layer stack of real files and
real blend masks**. Forced by the renderer rather than asked of the user: it is not a tuning knob
here but a statement that this path cannot read a render target.

This is what makes everything below possible. The inputs to a composite — the ground textures, their
transforms, the masks — are then all present, readable, and already being read by
`resolveTerrainMaterial`.

### D2 — the RT path bakes its own composite, in the core, from that stack

A chunk past a threshold is baked once into a single texture and its material becomes a one-layer
terrain material — which is exactly the shape `createPasses` gives a GL composite chunk, reached
without a GL context.

**Why bake at all, rather than shade the stack live at distance.** The composite is not only a
texturing workaround, it is the *shading* LOD. A distant chunk covers many cells and carries every
ground type in them; shading it live is one `maskWeight` and one texture fetch per layer per hit,
and distant hits are the majority of the pixels once four cells are visible. Baking turns that into
one fetch. The near field keeps the live stack, where the layer count is small and the sharpness is
worth paying for.

**Why in the core and on the CPU.** It fits the seam the renderer already has — `components/rtx/`
decides what the scene *is* and hands a backend `TextureData`; the backend uploads it like any other
image. No new Vulkan pass, no second path through the frame, and it is testable with no device at
all: a components test bakes a stack and asserts texels. It is also written once for both backends,
which a Vulkan bake would not be.

**Why it does not go on the frame path.** A frame that bakes eight chunks is a dropped frame, and
this tree's rule is that work is made incremental rather than batched behind a threshold. The bake
is a queue drained a bounded amount per frame, with the chunk shading from its live stack — or from
a coarser ancestor's bake — until its own is ready.

### D3 — the distant radius is an RTX setting in cells

`viewing distance` is the rasterizer's fog-and-visibility knob and means something else. What the RT
path needs is *how much world exists*, which is a property of the acceleration structure and not of
the camera. A new `[RTX] distant land cells` (default 4) converts to `cells × 8192` units and is
what `setViewDistance` is given on this path.

### D4 — terrain residency is left alone

`TerrainResidency` already asks rather than walks, already holds exactly one view, and is already
tested. Nothing about it changes.

## 5. What has to be redesigned

### R1 — the scene's texture table is keyed by file path

`SceneDesc::addTexture(VFS::Path::NormalizedView)` dedups by path, and `SceneTextures` resolves each
path through `Resource::ImageManager`. **A baked composite is not a file**, so neither call can
express it: there is no path to dedup on and nothing for the image manager to open.

This is the one structural change the feature needs. The table has to be able to hold an image whose
bytes the core already owns, keyed by something stable, with the same reference counting and the
same slot reuse every other texture gets — because a distant chunk comes and goes as the player
moves and its composite has to go with it.

**Landed.** `SceneDesc::addBakedTexture(key)` sits beside `addTexture` and both take their row from
one `takeTextureSlot`: one table, one free list, one reference count, and a
`holdTexture`/`dropTexture` pair that does not know which kind a slot is.

Not the shape first proposed here. Writing `addTexture` in terms of a keyed description means
changing what `getTextures()` hands back, and around thirty readers compare its elements against a
`VFS::Path::NormalizedView` for a distinction only the uploader needs. So a slot carries two
parallel facts instead — the file it came from, empty where it came from none, and what made it,
empty for every file. Neither on its own means free; both empty does, which is what `isTextureFree`
now answers.

### R2 — `resolveTerrainMaterial` gains a decision it does not have

Today it always builds the stack. It has to choose per chunk between the live stack and the bake,
and the identity it already uses — the first pass's `StateSet` pointer — is enough to key the bake
cache, so no new notion of *which piece of world this is* has to be invented. What it needs from the
chunk is its size, to make the choice.

### R3 — the harness has to be able to see distant land

`openmw-rtxtool` builds a `QuadTreeWorld` behind `--distant-terrain` and then hands it
`Settings::camera().mViewingDistance` (`world.cpp:223`), so it inherits §3.1 exactly. It needs the
same cell-radius setting the renderer gets, and a view far enough out to look at.

Two of the nine byte-comparison views — `seyda-neen-shore` and `dagon-fel` — already carry an open
issue about rendering a bare water quad, and that is the same neighbourhood: a view that gives a
cell and no camera. Worth settling alongside, since they are the views that would show this feature.

## 6. Implementation steps

Each leaves the tree working and names its own check. The frame changes shape at step 4, when
distant ground first arrives textured, so any figure taken before it is a figure about a different
frame.

**1 — let the scene hold an image that is not a file (R1). Done.** `addBakedTexture` beside
`addTexture`, one slot allocator under both, the lifetime unchanged; the shape it settled on is in
R1. **Checked:** a test that adds a baked image, sees it is not free though its path is empty, drops
it, and watches the row come back to a *file* — a freed slot is a row and not a kind.

**2 — bake a layer stack into one texture, in the core. Done.** `Rtx::TerrainComposite` takes a span
of `CompositeLayer` — a decoded diffuse, its painted light, and the two transforms and the mask
`MaterialLayer` already carries — and hands back one `TextureData` with a chain built down to a
single texel. **Everything is summed in light**: a texel is decoded, has its painted light divided
out per tile, is weighed by its mask and only then re-encoded, so half of one ground type and half
of another land on 188 and not on the 128 a sum of stored bytes gives. The four helpers this needed
were already written once in `apps/rtxtool/contactsheet.cpp`; they are now `texelreader.{hpp,cpp}`
and `paintedLight` beside `ShadingMap`, and the sheet reads them. **Checked:** four tests asserting
exact bytes — the linear sum, the two masks placing their ground types and the chain averaging them,
an identity transform copying texels while a quarter offset rolls them around the repeat, and the
delight strength at nought, half and full.

**3 — stop the GL composite being made at all (D1). Done.** `Terrain::sNoCompositeMap` — infinity,
declared beside the `chunkSize >= mCompositeMapLevel` it defeats — is what the harness always passes
and what the game passes when `[RTX] enabled`. With RTX off the expression yields exactly what it
did before, so the rasterizer is not compiled around, merely not asked. The harness also gained
`World::setTerrainViewDistance`, which steps 4 and 5 need and which is what made §3.1 and §3.2
measurable. **Checked:** two placements of one cell differing only in how far the quad tree may go,
asserting that distance adds no placement with nothing to point at, and that every ground layer's
diffuse names a real file. Against the unforced tree it fails on exactly that count — 5 against 32.

**4 — choose the bake at distance (R2, D2). Done.** `resolveTerrainMaterial` builds the stack as
before and, for a chunk at least a cell across with more than one layer, also takes a baked slot and
puts it on `material.mDiffuse`. The layers stay: they are the recipe. `SceneTextures` is where the
bake happens, because that is where a texture becomes bytes — it resolves the layer diffuses through
the same `ImageManager`, estimates each one's painted light, and hands the stack to
`TerrainComposite`. **No new lifetime**: `holdMaterialTextures` already holds `mDiffuse`, so a
composite is reference-counted with the ground that names it and freed when the chunk goes. **No new
GPU field**: a flattened chunk falls through to the same single-fetch branch as any other surface,
because that is what it now is. The harness gained `--distant-cells`, which is the only way to reach
the path at all and which step 5 folds into a setting.

**Not R2 as written.** The doc had `resolveTerrainMaterial` taking the chunk's size as a parameter;
it reads the drawable's own bounding box instead, so nothing above it had to learn to pass anything
down.

**Checked, and the check is weaker than it looks.** The three byte-compared views are unchanged —
the near field builds no composite, so it must not and does not move a pixel — and a components test
bakes a region's worth, asserts `SceneTextures` describes every one rather than counting it
unreadable, and asserts they do not all average to the same colour. The end-to-end path was then
confirmed by instrumentation: at twelve cells the scene carries 4046 placed instances against 3974
at none, and the top level is built with every one of them.

**No picture shows it, and §3.4 and §3.5 are why.** Fog is tuned to `viewing distance`, so ground
past seven thousand units is invisible however much of it exists; and paged terrain loses ground the
grid renders, at any radius. Step 4's own check asked for "distant ground textured rather than grey"
and what can honestly be said is that it is described, uploaded and traced — not that it has been
seen. Both blockers were found by chasing exactly that gap.

**What it costs, measured at Balmora with `--distant-cells=4`** against the same view with none,
both `--distant-terrain`:

| | textures | texture bytes | structures | scene build |
|---|---|---|---|---|
| no distance | 377 | 15 MiB | 19 MiB | 530 ms |
| four cells | 450 | 67 MiB | 40 MiB | 2613 ms |

**73 composites, 2083 ms — 28.5 ms each**, and 52 MiB of texture memory. It was 56 ms each until the
bake stopped decoding a compressed block at every tap: the level it reads is the one whose texels
are the size of a composite texel, which for ground tiling sixty times across a chunk is a handful
of texels square, so both levels are now decoded once up front and every tap is an array lookup.
Byte-identical, and the only optimisation taken — the rest waits for M12 and a finished frame.

**28.5 ms is still a dropped frame**, which is what step 6 exists to answer. It is stated here
rather than acted on: a cell boundary bringing eight distant chunks is a quarter of a second, so the
queue has to be bounded in something smaller than a whole composite.

**5 — give the radius its own setting (D3). Mostly done.** `[RTX] distant land cells` exists,
defaults to four, and is what `Rtx::distantLandReach()` answers with; `--distant-cells` writes it,
so the harness's terrain and its air are one number rather than two. **What is left is the game**,
which still hands `RenderingManager::setViewDistance` the camera setting
(`renderingmanager.cpp:1504`) and has to hand it this instead. **Check:** 1, 2 and 4 cells each
produce visibly more ground, and the trace timer and structure bytes are reported at each so the
shape of the growth is on record before anyone raises the default.

**6 — hold the frame flat.** Chunks arriving and leaving must not spike: the bake queue is bounded,
the structures append and recycle slots as they already do, and composites are released with their
chunks. **Check:** the worst frame and the p99 across a walk that crosses several cell boundaries,
beside the median — the tree's rule is that a spike is a dropped frame however good the average is.

**7 — settle the two views (R3).** `seyda-neen-shore` and `dagon-fel` are the views this feature is
visible in and both currently render a bare water quad. **Check:** the open issue in `ISSUES.md`
closes, and both show terrain.

## 7. What is not known yet

- **How many layers a large chunk's stack has** once composites are off. `createPasses` gathers what
the area holds; if an eight-cell chunk comes back with twenty passes, the bake is still fine but the
interim shading before a bake lands is not, and step 4's fallback has to be a coarser ancestor
rather than the live stack.
- **What object paging costs at radius 4.** It arrives through the same `collect` and is merged
geometry, so its textures are real files and nothing in §3.2 applies to it — but it is geometry in
the acceleration structure, and step 6 should count it separately from the ground.
- **Groundcover.** Another chunk manager on the same path. Out of scope here; it wants its own
answer and probably its own distance.
- **What one bake costs, and so what step 4's queue can be bounded in.** The bake is a trilinear
fetch per layer per output texel, so a 256-square composite over a six-layer stack is over a million
of them — a dropped frame if a frame does one whole composite. The unit therefore has to be smaller
than a composite, rows being the obvious slice, and how large a composite should be in the first
place is the same measurement. Both wait on step 3, which is what makes real stacks available to
measure against.
- **A composite is baked at one delight strength and then cached.** The painted light has to come
off during the bake, because the estimate repeats with a texture's tiling and a composite has none —
which is why `describe()` hands back a neutral shading map and the shader can no longer do it.
Changing `delight` at runtime therefore leaves every standing composite at the old strength. Whether
that wants a rebake or wants the setting to stop being live is a step 4 question.
