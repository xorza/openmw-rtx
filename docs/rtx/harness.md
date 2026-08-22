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
| **active grid** | `Constants::CellGridRadius = 1` — 3x3 | `--radius`, default 4 — 9x9 |
| **what triggers a load** | the player crosses a cell boundary | the camera crosses a cell boundary — the same shape |
| **unloading** | cells out of range are torn down | **never**; the `loaded` set only grows |
| **preloading** | background threads, ahead of the player | none, synchronous on the frame that crossed |
| **after a ring arrives** | `extendScene`, appending | `setScene`, rebuilding all of it |
| **per-frame walk** | the whole graph | the actors, and nothing else |
| **`retire` and compaction** | every frame | never |
| **node identity** | `getInstance` — one node per reference | `getTemplate` — one node per *model*, shared |
| **cull traversal** | runs, and decides LOD, object paging and groundcover | none; `RigGeometry`'s cull is driven by hand, per actor |
| **lights** | `SceneUtil::LightSource` nodes off the graph | the cell's `LIGH` records |
| **water** | the rasterizer's animated geometry, mirrored | an analytic quad, tagged `MaterialKind::Water` |
| **sky** | present, and excluded by traversal mask | absent |

Two of those are worth stating plainly rather than as a row.

**The harness has no scene graph at all.** It walks each object's template node once at staging,
with the reference's transform passed in beside it, and keeps nothing. There is no root to re-walk,
which is why the per-frame walk is only the actors and why `retire` is never called.

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

## 4. The decision this needs

**Step 1 has to come with the radius, not after it.** `getInstance` per reference at `--radius 4` is
47,828 real node instances where there are now a few thousand shared templates — memory and staging
time for a scene the game would never hold. At the game's radius it is about 6,800, which is what
the
game holds.

So: **`--radius` defaults to 1, and the wide views become opt-in.** Measurement then happens at the
grid the game actually runs, and `shot` and `sheet` pass a larger radius explicitly when the point
is
a picture rather than a number.

## 5. What stays different, deliberately

**Determinism.** The harness steps a fixed sixtieth with no AI, no physics and no scripts, so two
runs are the same six hundred frames and a picture can be compared byte for byte. The game has all
three and cannot offer that. Closing this gap is not a goal: it is the harness's whole reason for
existing, and `bench.sh` over savegames is where the game's numbers come from instead.

**Cull.** Nothing here proposes running an `osgUtil::CullVisitor` over the harness's graph. It would
bring LOD, object paging and groundcover — which is closer to the game — at the cost of the harness
deciding what is visible, which a ray tracer must not depend on. If it is ever wanted it should be a
separate decision with its own reason, not a side effect of this.
