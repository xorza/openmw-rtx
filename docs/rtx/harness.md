# The harness and the game

`openmw-rtxtool` and the game feed the same renderer by two different routes, and the routes have
drifted far enough apart that the harness stopped being able to see what costs the game a frame.
Every renderer defect found in the M12 stretch — the per-frame rebuild, the material churn, the
texture array, the mirror running before the cull — was invisible to `bench` and obvious within
seconds of measuring the game.

This is what the two do differently, which of those differences matter, and the order to close them
in. `mirror.md` is about the mirror itself; this is about what feeds it.

## 1. What each one does

The harness already streams cells as the camera crosses a boundary — that part is not missing, and
`view` has done it for a while. What differs is everything around it.

| | game | rtxtool |
|---|---|---|
| **active grid** | `Constants::CellGridRadius` — 3x3 | ~~`--radius`, default 4~~ — **the same constant, §4** |
| **what triggers a load** | the player crosses a cell boundary | the camera crosses a cell boundary — the same shape, and now the same fill order |
| **unloading** | cells out of range are torn down | ~~never~~ — **`dropCellsOutside`, objects and terrain both, §3.2 and §3.4** |
| **preloading** | background threads, ahead of the player | none, synchronous on the frame that crossed — deliberate, and now measured (§3.4) |
| **after a ring arrives** | `extendScene`, appending | ~~`setScene`, rebuilding all of it~~ — **the same decision, and the same code, §3.3** |
| **per-frame walk** | the whole graph | ~~the actors~~ — **the whole graph, §3.1** |
| **`retire` and compaction** | every frame | ~~never~~ — **every frame, §3.2** |
| **node identity** | `getInstance` — one node per reference | ~~`getTemplate`~~ — **`getInstance`, §3.1** |
| **cull traversal** | runs, and decides LOD, object paging and groundcover | none; `RigGeometry`'s cull is driven by hand, per actor |
| **lights** | `SceneUtil::LightSource` nodes off the graph | the cell's `LIGH` records |
| **water** | the rasterizer's animated geometry, mirrored | an analytic quad, tagged `MaterialKind::Water` |
| **sky** | present, and excluded by traversal mask | absent |

Two of those are worth stating plainly rather than as a row.

**The harness had no scene graph at all** — it walked each object's template node once at staging
and
kept nothing, which is why the per-frame walk was only the actors and why `retire` is never called.
Step 1 below fixed the first half of that.

**The game's water is not recognised as water.** `MaterialKind::Water` is set only by
`RtxBridge::addWater`, and only the harness calls it. So the surface the game mirrors is an ordinary
blended one: no `MASK_WATER`, no shadow pass-through, none of the wave or caustic treatment. That is
a defect rather than a difference, and it is in `.notes/ISSUES.md`.

## 2. Why unloading is the one that blocks the rest

For a benchmark that flies a camera through cells, the interesting cost is load *and* unload: the
game pays for a cell arriving, for one leaving, and for the compaction that follows. The harness
pays only the first, and its working set grows without bound, so its numbers are not the game's even
in shape.

Unloading cannot be added on its own. Dropping a cell means the scene must forget what that cell
placed, and the only mechanism for that is `retire`'s mark and sweep — which `SceneExtractor`'s own
header rules out for a caller like this one:

> Only where the walks were the whole world. This is mark and sweep: what makes it sound is that
> anything alive was met, so a caller that walks a region once and then mirrors only the movers
> would retire the region it is standing in.

So unloading needs either declared removal — the anchor lifecycle `mirror.md` Phase 2 describes —
or the harness has to start walking everything, the way the game does. The second is also what makes
Phase 2 measurable, and what makes the harness's frame the game's frame. It is the root change, and
the rest falls out of it.

## 3. The order to do it in

**1. Build a real graph.** `CellScene` makes one `osg::Group` per cell: every reference
`getInstance`'d under a `MatrixTransform` carrying its transform, terrain chunks alongside.
`StagedWorld` holds the root. The frame loop becomes the game's, line for line —
`clearPlacement()`, `extract(root)`, the `setScene`/`extendScene`/`placeScene` decision,
`advance()`,
`retire()`.

This also retires the shared-template problem that Phase 1 had to invent an anchor for: with a node
per reference, the node path identifies the placement again, the way it does in the game.

**2. Unloading — done, and it was about twenty lines.** `dropCellsOutside` takes the group of every
cell outside the active grid off the root; the walk that follows does not reach them, so the sweep
takes their placements, meshes and materials, and a compaction happens for the same reasons one does
in the game. `LoadedCells` is a map from cell to group now rather than a set of names, which is the
only new state it needed.

`retire()` went into the frame with it, which needed one thing solved first: the analytic water is
placed straight into the scene, so a sweep keyed on what the walks met would take its mesh out from
under a placement still standing on it. `SceneExtractor::hold` pins a mesh and material against
every
sweep and carries them through a compaction, and `addWater` now returns the indices to pin. The
sweep
costs about 0.15 ms a frame here.

**Terrain did not unload, and I called that a hole in `World::buildTerrain` that upstream did not
offer a way out of. That was wrong** — `Terrain::World::unloadCell` has been there all along and the
harness was not calling it. Closed in §3.4.

**3. Append instead of rebuild — done, and the decision is now written once.** It moved out of
`Tracer` into `RtxBridge::SceneUploader`, which both sides call: it holds which revision of the scene
the renderer was built from, describes only the textures that arrived, and picks between
`placeScene`, `extendScene` and `setScene`. `view`, `shot`, `bench` and `Tracer` all hand their scene
over through it, so there is no longer a version of the decision that can be wrong in one of them.

That also closed a real hole in the harness. `shot`, `bench` and `view` all called `placeScene`
unconditionally after a motion step — but a step walks the whole graph and sweeps it, so an actor who
draws a weapon brings a mesh with no structure behind it, and a sweep that closed a gap renumbers
what the last frame was built from. Both reach the frame as geometry naming something else.

**A crossing in a town appends, and that was not the expected answer** — nor, as §3.4 found by
flying one, is it the usual one. Walking one cell east out of Balmora drops three columns as it gains
three, and dropping a cell was supposed to compact the tables and force a full rebuild. In a town it
does not: a town is a few dozen models placed hundreds of times, the resource cache hands the same
nodes to every cell, and the three columns that left took no mesh with them the six that stayed were
not still using. `retain` finds every mesh still met, drops nothing, renumbers nothing.

Measured at Balmora (`RtxCrossingTest`): **1,397 meshes to 1,665, and 50 textures described where a
rebuild would have decoded and shading-estimated all 231 again.** For scale, the full build that
`shot` reports at the same spot is 667 ms.

Out in open country the ring that leaves does *not* share its models with the ring that stays, so the
sweep compacts and the answer is `setScene` after all. §3.4 has the count.

What the append still costs is the bottom levels: `VulkanRenderer::extendScene` keeps the texture
array but makes `SceneAcceleration` and `SceneBuffers` again whenever the mesh table grows, about 12
ms of spike on a frame that wanted 16. That is in `.notes/ISSUES.md` and is the next thing in the way
of a uniform frame time across a crossing.

**4. A camera path for `bench` — done, and it found more than it was built to measure.** A view in
`views.cfg` can now name a `to` and a `speed`: the camera walks in a straight line from its own
`pos`/`look` to the named view's, off the frame index rather than the clock, so the crossings land on
the same frames on every machine and on every build. It runs over the measured frames and not the
warm-up, and clamps at the far end rather than sailing into the sea. `[island-crossing]` flies the
Bitter Coast to the Ashlands at 12,000 units a second — nineteen boundaries in ten seconds of world,
which is fast enough that what a ring costs is what the run measures.

Getting there needed the streaming itself moved. `runWindow` had its own copy of loading, the ring,
the sweep and the actor snapshot; that copy is gone and both sides cross cells through
`StagedWorld::moveTo`, which is what stops them drifting apart a second time.

**Two bugs fell out of the first run, and neither was visible standing still.**

`readRegion` re-read *every* cell in the square on every call, not the ones that arrived. The grid
walk adds each square to `loaded` as it goes, so asking it again against a fresh map found all nine —
instancing them again, parenting the new groups under the root, and orphaning the six that were
already there. Two thirds of a grid leaked per crossing: a walk of eight cells north out of Balmora
took the working set from 6,238 placements to 29,648. It now tracks the content, 6,238 in the town
and 3,385 out in the wilderness, and `RtxCrossingTest` asserts it settles rather than climbs.

**And the terrain hole in §3.2 was not a hole.** `Terrain::World::unloadCell` has been upstream all
along; the harness simply never called it. `dropCellsOutside` now drops both halves of what a cell
brought — its group off the root, and its chunks out of the node `TerrainGrid` accumulates into.

**What the route then measured, and it overturns §3.3.** Nineteen crossings, *nineteen of them
rebuilds*. The append path exists and is correct, and a real journey never takes it: `retire` calls
`retain` whenever a mesh or material goes, a departing cell almost always takes one with it, and a
compaction renumbers everything. Balmora appends because a town is a few dozen models placed
hundreds of times and the ring that leaves shares them all with the ring that stays. Open country
does not.

So the crossing cost is dominated by the renderer rather than by the disk — of the run's crossing
time, roughly a quarter was reading content and three quarters was building what arrived, each one a
full `setScene` that also re-describes the whole texture table, which is append-only and has been
growing since the run started. Both halves are in `.notes/ISSUES.md`. The wall-clock figures are not
quoted here because the machine was not quiet when they were taken; the counts are.

## 4. The grid — done, and there is no longer a knob

**`--radius` is gone.** Not defaulted to the game's value: removed, along with the parameter it was
threaded through — `readRegion`, `loadRegion`, `runWindow` and the request fields that only carried
it. The game has no such option and decides its grid from `Constants::CellGridRadius`; so does the
harness now, and nothing can pass a different one.

**And the same logic, not only the same number.** The harness filled its square in scanline order;
the game sorts nearest-first, breaking ties by distance to the origin. A crossing timed here was
filling the grid in an order the game never uses, which for a benchmark whose whole subject is a
camera crossing a boundary is the measurement itself. `squareAround` now does what
`Scene::iterateOverCellsAround` and `Scene::sortCellsToLoad` do between them.

**Copied rather than shared, and the copy says so.** The originals sit in an anonymous namespace in
`scene.cpp`, so nothing outside that file can link to them; lifting them into `components/` was
tried
and cost three upstream files to share twenty lines of arithmetic. Twenty lines is the cheaper
copy —
**but it can drift, and nothing here will notice.** If `Scene` ever changes how it orders a load,
this
has to follow by hand. That is what the copy costs, and it is written where the copy is.

At Seyda Neen the harness went from **47,828 placed instances to 6,445**, against the **6,835** the
game holds standing in the same place; the rest is the actors and the paging the harness does not
have yet. It was nine by nine and is three by three, which is what the game runs.

What is lost is the wide vista for screenshots, and that is the right trade: a picture taken over a
grid the game never loads was never a picture of the game.

## 5. What else the two could share — surveyed, and the answer is a wall

The grid was the obvious duplication and it is closed (§4). The question of what is left was asked
again once §3 and §4 had moved the streaming into `StagedWorld`, and this time it was measured rather
than guessed. The harness is **6,797 lines**. Here is what each part of it would take to replace.

| harness code | lines | the game's version | what stops it |
|---|---|---|---|
| `npc.cpp` — body parts, auto-equip, the drawn weapon | 698 | `MWRender::NpcAnimation` + `InventoryStore::autoEquipWeapon` | takes an `MWWorld::Ptr`, and reaches `MWBase::World`, `SoundManager` and `MechanicsManager` thirteen times |
| `actor.cpp`, `posedactors.cpp` — skeletons and keyframes | 878 | `MWRender::Animation` | **already shares everything shareable**: `KeyframeManager`, `SceneUtil::Skeleton`, `KeyframeController`, `Misc::ResourceHelpers`. What is left is `Ptr` glue |
| `cellscene.cpp` — a node per reference under a transform | 463 | `MWRender::Objects` — **zero** `Environment::get()` calls | every method takes a `Ptr` |
| `world.cpp` — content files, records, references | 508 | `MWWorld::ESMStore` — also **zero** `Environment::get()` calls | nothing, but it buys nothing: this is already thin glue over `components/esmloader`, which is upstream's own shared loader |
| `terrainstorage.cpp` | 154 | `MWRender::TerrainStorage` | `Environment::get().getESMStore()` ×5, and `LandManager` wants `getWorld()` |
| `window.cpp` — an SDL window and a fly camera | 215 | `components/sdlutil` | different thing: that is OSG's GL window and the game's input bindings, this is a bare surface for Vulkan |
| the active grid and its fill order | 20 | `Scene::iterateOverCellsAround`, `sortCellsToLoad` | **copied**, §4 — private to `scene.cpp`, and a component for them cost three upstream files |

**The wall is `MWWorld::Ptr`, and it is the same wall every row hits.** A `Ptr` is a
`LiveCellRefBase*` and the `CellStore` it lives in; making one goes through
`WorldModel::registerPtr`, and *using* one dispatches through `MWWorld::Class`, whose implementations
call `MWBase::Environment::get()` freely. So a caller needs the environment, and the environment is:

| interface | pure virtual methods |
|---|---|
| `MWBase::World` | 186 |
| `MWBase::WindowManager` | 154 |
| `MWBase::MechanicsManager` | 80 |
| `MWBase::LuaManager` | 46 |
| `MWBase::InputManager` | 37 |
| `MWBase::SoundManager` | 36 |
| `MWBase::DialogueManager` | 27 |
| `MWBase::StateManager` | 17 |
| `MWBase::Journal` | 14 |
| `MWBase::ScriptManager` | 7 |
| | **604** |

Stubbing those is not a reduction of 6,797 lines, it is an addition of several thousand — and every
upstream change to any of the ten breaks the harness build. Booting them for real is worse: the
loading screen alone means MyGUI over an OSG viewer with a live GL context, which `shot` and `bench`
do not have and exist not to need. `MWWorld::Scene::changeCellGrid` calls the window manager fifteen
times; `worldimp.cpp` calls it twenty-three.

**So the verdict is: the harness is already reusing everything reachable.** What it shares —
`components/esmloader`, `SceneManager::getInstance`, `KeyframeManager`, `SceneUtil::Skeleton`,
`Terrain::TerrainGrid`, `ESMTerrain::Storage`, `Constants::CellGridRadius`, `components/esm3` — is
everything upstream put below the `apps/openmw` line. What it duplicates is what upstream put above
it, and above that line every function's first argument is a `Ptr`.

The one thing that would change this is upstream moving body-part assembly and reference iteration
below the line, which is not a change this fork is going to make (`CLAUDE.md`: no merge-back
discipline, so a `components/` header this fork invents conflicts on every merge for as long as it
exists).

### What can still come out, and it is internal rather than shared

- `StagedWorld::mirror` and `StagedWorld::getRoot` are public with **no callers** — the streaming
  moved inside and nothing outside walks the graph any more.
- `main.cpp` is 742 lines and holds `runScene`, `runFind` and `runTextures` inline beside the
  subcommand dispatch. `runScene` also builds its own root, scene and extractor rather than staging
  one.
- `shot`, `bench` and `view` each assemble the same `VisibilityConstants` block, and it has already
  drifted: `shot` and `view` honour `--albedo`, `bench` does not; `bench` and `view` advance the sea
  and `shot` deliberately does not. One of those three differences is a decision and the other two
  are omissions, and nothing says which is which.

None of that is a reuse question, and none of it is large. It is listed here so the next person to
ask "can the harness be smaller" finds the measurement rather than repeating it.

## 6. What stays different, deliberately

**Determinism.** The harness steps a fixed sixtieth with no AI, no physics and no scripts, so two
runs are the same six hundred frames and a picture can be compared byte for byte. The game has all
three and cannot offer that. Closing this gap is not a goal: it is the harness's whole reason for
existing, and `bench.sh` over savegames is where the game's numbers come from instead.

**Cull.** Nothing here proposes running an `osgUtil::CullVisitor` over the harness's graph. It would
bring LOD, object paging and groundcover — which is closer to the game — at the cost of the harness
deciding what is visible, which a ray tracer must not depend on. If it is ever wanted it should be a
separate decision with its own reason, not a side effect of this.
