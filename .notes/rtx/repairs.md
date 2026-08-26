# Three repairs

What is left in `ISSUES.md`, each traced to a cause and given a fix that removes the class rather
than the occurrence. The four repairs this file used to carry are done and gone from it; what they
settled is written where it belongs — `distantland.md` §3.4 and §3.5 for the fog and the swept
ground, and the tests named below for the rest.

Everything here was read or measured, and each claim says which.

## 1. A table with nothing in it was bound to the shader as nothing at all — fixed

**What it was.** `RtxFrameCostTest.aSteadyFrameDoesNotTouchTheHeap` failed with
`VK_ERROR_DEVICE_LOST` about five seconds in, intermittently over hours, on a scene of one wall at
64
by 64.

**What said so.** That test is the only one in the tree taking an *unvalidated* device — it counts
allocations, and the layers allocate. Run against the validated harness they named it at once: three
storage descriptors, `ShadingMaps`, `SpriteTileOffsets` and `SpriteTileIndices`, bound to
`VkBuffer 0x0` and then dispatched through. Whether that faults is the driver's to decide, which is
the whole of the intermittency.

**Why they were null, which is not where the fix went.** Every growable table was written the same
way — grow if what is wanted does not fit — and nothing wanted is not too big, so nothing was made.
But flooring the request was not enough: the tables that were null are filled by calls a frame may
never make. A scene with no sprites never bins any, so `binSprites` never runs, so the buffer it
would have grown never exists. **Growth on write cannot carry the guarantee, because the write is
exactly what does not happen.**

**The fix.** The owner opens every table when it is built. `SceneBuffers` opens all fourteen in its
constructor and `TextureArray` opens its shading maps in its, so a table exists from the moment the
object does and growth only ever enlarges it. `growTo` is the one rule that grows one, and it floors
at a byte because Vulkan has no zero-sized buffer.

**What that deleted.** Five hand-written stand-ins — a one-element `noLight`, `noIndex`, `noSprite`,
`noEmitter` and a second `noIndex` — each existing to say "an empty table is one element" for one
table. They were the symptom being patched a table at a time; the one table nobody had noticed is
what cost the device. The tables now go over as they are, and what stops the shader reading an empty
one is its count, exactly as it always was.

**Steps**

1. **Done.** `growTo` in `hostbuffer`, used by every host-buffer growth site, and both owners open
   their tables at construction. **Checked:**
   `RtxSceneTableTest.aSceneWithNothingInItStillBindsATableForEverythingDeclared` builds a
   `SceneBuffers` and a `TextureArray` from an empty scene and asserts every handle is non-null —
   ten of them, because the rule was the same for all and only three were ever noticed.
2. **Done.** The five stand-ins are gone.
3. **Done.** The frame-cost test runs clean against the layers, and passes three times running on
   its
   own unvalidated device where it failed four times running before.
4. **Done differently.** The pass asserts that nothing it binds is null, which names the table
   rather
   than losing a device — and `RtxVisibilityTest` already traces frames against the layers and
   collects what they say, so the usage is watched next to the test that cannot watch it. **The
   assert is debug-only**; the construction test above is what holds in a release build.

**Two more the same run found, both that test's own and both now fixed.** Its target was
`VK_FORMAT_R8G8B8A8_UNORM` where the shader declares `Rgba32f` — undefined for the whole image — and
it never passed the shading table it had built, which is where the third null came from.

## 2. Distant statics never exist in the harness

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
under `object paging = true`, the shipped default) and `Groundcover` after it. The harness registers
neither: `apps/rtxtool/world.cpp` builds the quad tree and stops. With nothing to ask, only ground
can answer.

`distantland.md` §2 says distant statics "arrive through the same call". That is true of the game
and
was never true of the harness; this is the correction.

**The fix.** The harness builds the world the game builds. It already takes the terrain's numbers
from
the settings the game reads; the chunk managers come from the same place, so the two worlds cannot
answer different questions — which is the whole reason the harness exists.

**Steps**

1. `World` builds an `ObjectPaging` under the setting the game reads, registers it with the quad
   tree
   and with the resource system, exactly as `RenderingManager` does. **Check:** a components test
   asserting a paged region places merged statics past the active grid, and that a grid world places
   none.
2. What it costs is measured the moment it lands — triangles, structure bytes, textures, scene build
   — and written into `distantland.md` §7 beside the bake's figure. **Nothing is tuned on it yet.**
   Distant statics are real files, so §3.2's render-target problem does not touch them.
3. Groundcover is the third manager the game registers and stays out of scope, as `distantland.md`
   §7
   already says: it wants its own distance and probably its own answer.

## 3. Whether the game leaves creatures standing is answered, by reading

**What was open.** The harness left residents posed and placed after their cell unloaded. That is
fixed — an actor hangs under the group its cell hangs under, so it leaves when the cell does. What
was not known is whether the game does the same.

**Read, not measured.** `MWWorld::Scene::unloadCell` calls `mRendering.removeCell(store)`, and
`RenderingManager::removeCell` calls `mObjects->removeCell(store)`, which takes the cell's objects —
actors among them — off the graph. The mirror then retires whatever a walk did not meet, which is
`RtxRenderer`'s `advance` and `retire` every frame. So the game removes them from the graph and the
sweep drops them, by the same rule that made the harness wrong when its actors hung somewhere else.

**Steps**

1. Confirm it once in the game rather than leaving it on a reading: walk out of a town with
   `[RTX] enabled` and watch the instance count fall. **One run, and then the entry goes.**
2. If it does not fall, the cause is `mObjects->removeCell` and not the mirror, and it earns an
   entry
   of its own.

## Order

**1 first.** A renderer that binds null buffers is one whose every other measurement is taken on a
device that may be about to fall over, and the fix is small. **Then 3**, which is one run and closes
an entry. **2 last**, because it is the one that changes what a frame contains, and
`distantland.md`'s
remaining steps — the bake queue especially — want a frame that is not about to grow buildings.
