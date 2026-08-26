# Three repairs

What is left in `ISSUES.md`, each traced to a cause and given a fix that removes the class rather
than the occurrence. The four repairs this file used to carry are done and gone from it; what they
settled is written where it belongs — `distantland.md` §3.4 and §3.5 for the fog and the swept
ground, and the tests named below for the rest.

Everything here was read or measured, and each claim says which.

## 1. A table with nothing in it is bound to the shader as nothing at all

**What you see.** `RtxFrameCostTest.aSteadyFrameDoesNotTouchTheHeap` fails with
`VK_ERROR_DEVICE_LOST` about five seconds in, on a scene of one wall at 64 by 64. Intermittent over
hours — four runs passing and four failing the same session — and reliable while it lasts.

**The cause, which the validation layers name outright.** That test is the only one in the tree that
takes an *unvalidated* device: it counts allocations, and the layers allocate. Run the same test
against the validated harness and they say it in one line:

```
binding 19, "ShadingMaps"        is using VkBuffer 0x0 that is invalid or has been destroyed
binding 30, "SpriteTileOffsets"  is using VkBuffer 0x0 that is invalid or has been destroyed
binding 31, "SpriteTileIndices"  is using VkBuffer 0x0 that is invalid or has been destroyed
```

Three null buffers bound to descriptors the shader declares, then dispatched through. Whether that
faults is up to the driver, which is the whole of the intermittency.

**Why they are null.** Every growable table in this renderer is written the same way — grow if what
is wanted does not fit:

```cpp
if (held.getSize() >= bytes)     // scenebuffers.cpp:176
    return;
if (mShading.getSize() >= values.size_bytes())   // texture.cpp:443
    return false;
```

**Nothing wanted is not too big, so nothing is made.** A scene with no textures asks for nought
bytes
of shading maps and gets no buffer; the same for a frame with no sprites. There are seven of these
sites and the rule is identical in all of them.

It is already known to be wrong in exactly one place. `binSprites` carries a hand-written stand-in:

```cpp
// Nothing may be bound to a descriptor a shader declares, and a frame with no sprites in it
// has an empty list — the offsets are all nought, so the shader reads none of this.
static constexpr std::uint32_t noIndex = 0;
```

One table was noticed and patched; the other two were not, and the rule that let it happen was left
alone.

**The fix.** The growth rule guarantees a buffer rather than a size. A table asked for nothing gets
the smallest one that can be bound, because *a descriptor a shader declares must have something in
it* — that is a property of the pipeline, not of any one table, and every table gets it from the
same
line. The stand-in in `binSprites` then has nothing to stand in for and goes.

**Steps**

1. The growth rule floors its request rather than returning early, so no path leaves a null handle.
   **Check:** a components test that builds `SceneBuffers` and a `TextureArray` from an empty scene
   and asserts every handle it hands out is non-null — which is the assertion that would have failed
   for three of them.
2. `binSprites` drops its one-element stand-in. **Check:** the sprite tests still pass, and a frame
   with no sprites still reads nothing.
3. The frame-cost test runs against the layers once to confirm they are quiet. It keeps its
   unvalidated device — the allocation count is the point of it — but it stops being the only place
   nothing is watching: **step 4.**
4. A sibling test builds the same scene and the same passes on the *validated* harness and traces
   one
   frame. **What makes step 1 stay fixed**: the allocation test cannot see invalid usage by
   construction, so the usage is checked next to it rather than in it.

**Also found by the same run, and neither is the device loss.**

- The frame-cost test's own target is `VK_FORMAT_R8G8B8A8_UNORM` where the shader declares
  `Rgba32f`.
  The layers call it undefined for the whole image. The renderer's own colour target is the wider
  format; the test made a narrower one and nothing said so.
- `vkDestroyDevice(): VkDevice has 1 leaked object`. One object outliving the device in that test.

Both are that test's, both were invisible without the layers, and both belong to step 4.

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
