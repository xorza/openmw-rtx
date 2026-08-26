# Four repairs

The four entries standing in `ISSUES.md`, each traced to a cause and given a fix that removes the
class rather than the symptom. Companion to `distantland.md`, which owns the feature these were
found while building.

Everything below was measured or read, and each claim says which. Nothing here is a guess about what
the code probably does.

## 1. A view with no camera frames the ocean

**What you see.** `seyda-neen-shore`, `sadrith-mora` and `dagon-fel` render a bare water quad
against fog. They are exactly the three entries in `files/rtx/views.cfg` that give a cell and no
`pos`.

**The cause.** `placeCamera` (`placement.cpp:23`) puts the eye back far enough to fit the scene's
bounding sphere in the field of view:

```cpp
const float distance = bounds.radius() / std::tan(radians(fov) * 0.5f) * 1.15f;
```

`WaterPlane` is a sheet **a hundred and fifty cells across** (`waterplane.cpp:20`), because that is
what the game builds and agreeing with the game about what the sea *is* was worth more than the
quads. So the scene's bounds are the sea's, the radius is most of a million units, and the camera
goes there.

Measured: `--view=seyda-neen-shore` places the eye at **(1119060, 1061720, 662450)** looking at
(-12288, -69632, 2496) — 1.5 million units out. At that distance a cell is a speck and the sheet is
the picture.

**The fix.** The sea is a backdrop and not content: it is unbounded by construction, it is placed by
the world rather than by any cell, and it must not get a vote in what the camera is pointed at.
Framing asks what the cell holds.

`SceneDesc` already knows which materials are water — `MaterialKind::Water` is what
`resolveWaterMaterial` stamps and what `InstanceRecord` reads to lower the sheet. So the bounds a
camera is placed from are the bounds of everything that is not it. That is one predicate, in the
place that already owns the answer, and it fixes every future backdrop the same way — a sky dome
would otherwise do this again.

**Steps**

1. **Done, and it needed both halves.** `SceneDesc::getContentBoundsWithin(region)` leaves the
   backdrops out *and* clips to the region asked for. Leaving out the sea alone was not enough: with
   distant land on, everything-that-is-not-sea still reached four cells past the one being looked
   at,
   and the eye went two hundred thousand units out to frame it. `getBounds` is untouched — a far
   plane wants everything there is, and three callers ask it for exactly that. **Checked:**
   `RtxSceneDescTest.aRegionsExtentLeavesOutTheSeaAndStopsAtItsOwnEdge` — a unit square and a ten
   thousand unit sheet, asserting the extent is the square's; a chunk straddling the edge clipped to
   it; and a region with nothing in it coming back invalid rather than as a box at the origin.
2. **Done.** `StagedWorld` places the camera from the square `readRegion` staged, and an interior
   still frames everything it holds — a room has no backdrop and no region beyond itself.
   **Checked:** the three views place the eye about 32,000 units out at 14,000 up instead of 1.5
   million, and the views that give a `pos` are byte-identical, because an explicit camera never
   consulted the bounds at all.
3. **Done.** `seyda-neen-shore` renders what its note names — islands, a shoreline, the seabed
   through the water, and land at the horizon.

## 2. An unbounded fence wait turns a stall into a hang

**What you see.** `RtxFrameCostTest.aSteadyFrameDoesNotTouchTheHeap` stops after printing its two
pipeline lines, with the GPU at 100%, and never returns. It reproduced on a clean tree several times
running, then passed four times in a row an hour later. It is intermittent.

**The cause of the hang, which is not the cause of the stall.** The test submits forty frames and
waits on each with no timeout (`visibilitypass.cpp:3738`):

```cpp
vkWaitForFences(device.getHandle(), 1, &finished, VK_TRUE, ~std::uint64_t{ 0 });
```

A device that will never signal and a device still working are the same thing to that call, forever.
There are three such waits — the two above and `commands.cpp:93`, which is the shared submit path
every test and the renderer itself go through, plus `presenter.cpp:175`.

**Why the device stalled is still open**, and the point of this repair is that the next occurrence
leaves evidence instead of a wedged process. A bounded wait cannot fix a lost device; it can turn
"the suite never finishes" into "this submit did not complete in N seconds", which names the pass,
fails one test, and lets the rest of the suite run.

**The fix.** The shared submit path waits with a deadline and throws naming what it waited on. The
test stops rolling its own submit and uses it. A deadline generous enough that no honest frame
reaches it — seconds, not milliseconds — because this is a canary and not a budget.

**Steps**

1. **Done.** `awaitVk` sits beside `checkVk` — one bounded wait, throwing `Error` naming what was
   waited on — and `CommandPool::endAndWait`, the presenter's image wait and the swapchain's acquire
   all go through it. Ten seconds, which no honest submit approaches. **Checked:**
   `RtxDeviceTest.aWaitOnADeviceThatNeverAnswersEndsAndNamesItself` waits on a fence nothing submits
   against and gets the error rather than a hang; the deadline is a parameter so the test reaches
   the
   failure in a millisecond instead of sitting out the real one.
2. **Done, and not as written.** The frame-cost test keeps its own submit — a command buffer reused
   against a fence is the shape it exists to measure, and `submitAndWait` allocates a buffer per
   call
   — but its wait is now `awaitVk`. **Checked:** the allocation count it asserts is unchanged.
3. **The stall is named, and it is a device loss.** With the deadline in place the test fails in
   about
   five seconds with `VK_ERROR_DEVICE_LOST` rather than hanging. That is a real fault on a scene of
   one
   wall at 64 by 64, not a slow frame, and it is its own investigation — recorded in `ISSUES.md`
   with
   what it says now that it says anything.

## 3. Distant statics never exist in the harness

**What you see.** Past the loaded cells the ground arrives bare — no buildings, no trees, no rocks —
where the same hillside inside the grid carries all three.

**The cause.** `QuadTreeWorld::loadRenderingNode` asks **every registered chunk manager** for its
chunk and adds what comes back:

```cpp
for (QuadTreeWorld::ChunkManager* m : mChunkManagers)
    if (osg::ref_ptr<osg::Node> n = m->getChunk(...))
        pat->addChild(n);
```

The game registers `ObjectPaging` beside the terrain's own manager (`renderingmanager.cpp:1445`,
gated on `object paging = true`, which is the shipped default). The harness registers nothing:
`apps/rtxtool/world.cpp` builds the quad tree and stops. With no manager to ask, no distant static
can arrive — the ground is the only thing that answers.

`distantland.md` §2 says distant statics "arrive through the same call", and that is true of the
game and was never true of the harness. This is the correction.

**The fix.** The harness builds the world the game builds. It already takes the terrain settings
from the same place; the chunk managers come from the same place too, so the two worlds cannot drift
into answering different questions — which is the whole reason the harness exists.

**Steps**

1. `World` builds an `ObjectPaging` under the same setting the game reads and registers it, and
   registers it with the resource system as the game does. **Check:** a components test asserting a
   paged region places merged statics past the active grid, and that a grid world places none.
2. What it costs is measured the moment it lands — triangles, structure bytes, textures, scene build
   — and written into `distantland.md` §7 beside the bake's figure. **Nothing is tuned on it yet**;
   distant statics are real files, so §3.2's render-target problem does not touch them.
3. Groundcover is the third manager the game registers and stays out of scope, as `distantland.md`
   §7
   already says: it wants its own distance.

## 4. An actor does not know which cell placed it

**What you see.** Creatures stay standing after the camera leaves the cells that hold them.

**The cause.** `PosedActors` keeps `std::vector<std::unique_ptr<Actor>> mActors` — a flat list with
no cell on it. `CellPerson` (`cellscene.hpp:34`) carries a record and a transform and no cell
either, so the association is lost before the actor is built. Their nodes are added to the **run's
root** rather than to the cell's group, so `dropCellsOutside` — which takes the cell's node off the
root and unloads its terrain — has nothing that would take them, and nothing to tell.

The ground and the statics leave correctly because both are under the cell's group. The actors are
the one thing placed beside it.

**The fix.** An actor belongs to the cell that placed it, and saying so once is what makes it leave
with everything else. Parent an actor's node under its cell's group rather than the root: the drop
already removes that group, `PosedActors` walks the whole root and so still finds and poses whoever
is left, and no second lifetime is invented for a thing that already had one.

Where that turns out not to hold — an actor that must outlive its cell — the fallback is the weaker
statement: record the cell on the actor and have the drop tell `PosedActors`. It is worth trying the
first, because the second adds a rule someone has to remember, which is the shape of the bug that
swept the paged ground.

**Steps**

1. `CellPerson` and `CellProp` carry the cell they came from, and `PosedActors::addResidents`
   parents
   each actor under that cell's group. **Check:** a components test that stages a region, crosses
   out
   of a cell, and asserts the actors that stood in it are gone and the rest still stand.
2. **Whether the game does the same is not known** and is checked before anything is concluded from
   the harness: `MWWorld::Scene` unloads a cell's references through its own path, and the mirror
   may
   already lose them correctly.
3. `ISSUES.md`'s fourth entry closes on the harness, and the game either needs nothing or gets its
   own
   entry.

## Order

**2 first**, because a suite that can hang has to stop hanging before anything else is trusted to
have been run. **Then 1**, which is small and makes three views usable for looking at the others.
**Then 4**, which is a lifetime bug and cheap. **3 last**, because it is the one that changes what a
frame contains, and `distantland.md`'s remaining steps — the bake queue especially — want a frame
that is not about to grow buildings.
