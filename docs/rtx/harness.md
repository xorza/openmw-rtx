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
| **unloading** | cells out of range are torn down | **never**; the `loaded` set only grows |
| **preloading** | background threads, ahead of the player | none, synchronous on the frame that crossed |
| **after a ring arrives** | `extendScene`, appending | `setScene`, rebuilding all of it |
| **per-frame walk** | the whole graph | ~~the actors~~ — **the whole graph, §3.1** |
| **`retire` and compaction** | every frame | never |
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

**2. Unloading, which is then about twenty lines.** Take the cell's group off the root; the next
sweep collects what it placed and compaction happens for the same reasons it does in the game.

**3. Append instead of rebuild.** A ring arriving takes the same decision `Tracer` takes — grown
means `extendScene`, renumbered means `setScene`.

**4. A camera path for `bench`.** Step the camera at a fixed speed between two viewpoints so the
crossings land in the same places on every run, and the load spikes show up in the p99 where they
can be seen. Uniform frame time is the target the spikes are measured against; see `CLAUDE.md`.

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

## 5. What else the two could share

The grid was the obvious duplication. The survey of what is left:

| in the game | the harness's version | worth sharing? |
|---|---|---|
| `iterateOverCellsAround`, `sortCellsToLoad` | its own square loop, in the wrong order | **copied**, not shared — they are private to `scene.cpp`, and a component to hold them cost three upstream files for twenty lines |
| `Constants::CellGridRadius` | `--radius`, default 4 | **done** — the option is gone |
| `MWWorld::Scene::changeCellGrid` — the arrival/departure diff | `forEachNewCell`, arrivals only | not directly: it is welded to the world model, physics and the loading screen. The *shape* is worth copying when unloading lands; the code is not. |
| `MWRender::Objects` — a node per reference under a transform | `getTemplate` and a transform passed beside it | not shareable, but step 1 should end up doing what it does |
| `CellPreloader` — background threads | nothing | no. The harness wants the load cost visible and on the frame, not hidden behind a thread. |
| `SceneManager::getInstance` | already used, for actors only | already shared |
| `Terrain::World` | already used | already shared |

The pattern went the other way from the obvious guess: **almost none of it is worth sharing.**
`Scene::changeCellGrid` reads as the thing to reuse and is the thing that cannot be — it needs
`MWBase::Environment`, a `WorldModel`, a navigator, a loading listener and the script machinery,
none
of which a standalone tool has or should grow. And what *could* be shared is small enough that a
marked copy beats a new upstream file: a header in `components/` conflicts on every merge, for
twenty
lines that have not changed in years.

What is genuinely shared is what was already a component before either side wanted it —
`Constants::CellGridRadius`, `SceneManager::getInstance`, `Terrain::World`, `components/esm3`.


## 6. What stays different, deliberately

**Determinism.** The harness steps a fixed sixtieth with no AI, no physics and no scripts, so two
runs are the same six hundred frames and a picture can be compared byte for byte. The game has all
three and cannot offer that. Closing this gap is not a goal: it is the harness's whole reason for
existing, and `bench.sh` over savegames is where the game's numbers come from instead.

**Cull.** Nothing here proposes running an `osgUtil::CullVisitor` over the harness's graph. It would
bring LOD, object paging and groundcover — which is closer to the game — at the cost of the harness
deciding what is visible, which a ray tracer must not depend on. If it is ever wanted it should be a
separate decision with its own reason, not a side effect of this.
