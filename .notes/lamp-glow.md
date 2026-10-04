# A lamp's glowing model lights the room twice — proposal and plan

## Summary

A `LIGH` reference is two things in the ray tracer: a lamp, which `gather` samples directly, and a
model, whose glowing parts (a lantern's paper, a candle's flame mesh) a diffuse bounce can hit. Both
deliver the same light to the room. The proposal: **a lamp's own model still glows to the eye and in
reflections, but a diffuse bounce takes no glow from it, because its lamp already delivers that
light.** This removes the double count and the firefly source together. A glow with no lamp (a
mushroom, a glow-mapped soul gem) keeps its bounce, because the bounce is the only path its light
has.

The second issue, the contradictory comment on `EMISSIVE_INTENSITY`, is fixed in the same change.

## Findings

### How the lamp delivers the lantern's light

- `SceneUtil::addLight` (`components/sceneutil/lightutil.cpp:95`) attaches the light inside the
  group it is given: at the model's `AttachLight` node, or at the group itself. A placed `LIGH`
  object gives its object root (`Animation::addExtraLight(getOrCreateObjectRoot(), …)`,
  `apps/openmw/mwrender/animation.cpp:2105`). An actor's carried light gives the shield part's node
  (`npcanimation.cpp:674`, `creatureanimation.cpp:178`). So the light always stands inside the
  model it belongs to.
- The walk meets the `LightSource` and makes a lamp of it (`SceneExtractor::addLight`,
  `components/rtx/mirror/sceneextractor.cpp:775`).
- A shadow ray to the lamp stops `mClearance` short of the flame, so it passes the lamp's own
  fitting (`lampPassage`, `lib/lights.glsl`). The lamp therefore lights every surface around it as
  if the lantern's paper were not there. The lamp's light **is** the lantern's light.

### How the bounce delivers it a second time

- `litSurface` adds `mAlbedo × mEmissiveColour × EMISSIVE_INTENSITY + mEmitted` to every surface
  (`lib/shading.glsl:351`).
- `bounceArriving` → `bounceLanding` → `lightAtPathEnd` → `litSurface` shades the bounce's far hit,
  glow included, on purpose: "Its glow is counted here, because this is the only path it takes"
  (`lib/shading.glsl`, `bounceArriving`).
- So a diffuse bounce that lands on the lantern's paper brings back the paper's glow, while the
  lamp inside the paper lights the same surface directly.

### Measured at the M[FR] guild's tree (the reported eye and hour, 1280×720, no upscaler, exposure 64)

- Seven materials glow there: three lantern-paper materials and one glass pot with emissive colour
  1, one scrib with a trace of it, and two with glow maps (M[FR]'s `phosphorus`, a grand soul gem).
- With the glow taken off the bounce's far hit, the converged frame is 9.6% darker. The lost light
  is cool blue and lies over the whole tree.
- The glow maps alone move nothing measurable. The emissive colours do all of it.
- The same glow is the tree's fireflies: after a 150-unit strafe, pixels over 4× the reference
  fall from 1.15 to 0.08 in a thousand without it, and the frame's noise from 3.70 to 1.81. On
  vanilla `probe-guild-planter`, noise falls from 1.33 to 1.02.
- Not checked: whether the glass pot (`tx_item_pot_glass_peach_03`) is the model of a `LIGH` record
  under M[FR]. The three lanterns are, as vanilla's paper lanterns are.

### Precedent in the tree

A magic effect has the same shape and is already handled the proposed way: its sheets and flames
light the world as one lamp (`Glow::makeLight`), and its additive sheets carry `MASK_ADDITIVE`, which
no shading ray meets. The effect's lamp stands for its glowing geometry, and nothing counts it twice.

### What the field does

In a path tracer with next-event estimation, an emitter that light sampling covers is either
weighted by multiple importance sampling when a BSDF ray hits it, or, where light sampling covers it
completely, its emission is not added on a BSDF hit at all. Here the lamp is a proxy for its model:
the two are different geometry with different radiance, so MIS between them has no common density
to weigh. The second rule applies: the lamp covers the model's light, so a BSDF-sampled diffuse ray
adds none of it. A reflection is not an estimate of the lamp's light on a surface but a picture of
the lamp, so it keeps the glow, as `PATH_SEEN` already says for every reflected far hit.

## Options considered

| Option | Verdict |
|---|---|
| **A. A per-placement trait, "lamp body", and the diffuse bounce drops a lamp body's glow** | Chosen. Exact to what the game attached the light to. One bit, one branchless factor in one function. |
| B. A per-material flag | Wrong: materials are deduplicated by content, and the same paper texture can stand on a lamp and on a static with no light. |
| C. Drop every glow from the diffuse bounce | Wrong: a mushroom with no `LIGH` then lights nothing at all. |
| D. Sample emissive triangles as lights (RTXDI's mesh lights, with MIS) | Correct for glows that have no lamp, and a large separate feature. It does not replace A: a lamp body's glow must still not be counted beside its lamp. |
| E. Turn each lamp down by its model's glow | No physical basis, and it dims the room where the bounce does not find the paper. |

## Design

### Which surfaces are a lamp body

Every drawable under the group `SceneUtil::addLight` was given, **where the light attached there is
made into a lamp** (`makeLight` has a value). An `OffDefault` lamp attaches no light, so its glowing
paper is the only light it gives and keeps its bounce. A light the renderer refuses or makes nothing
of leaves its model's glow in the bounce for the same reason.

### Game graph: one hook

`SceneUtil::addLight` marks the group it attaches to: a small `SceneUtil::LampBody` object that names
its `LightSource` (an `osg::observer_ptr`). This is an integration hook, which the rules allow, and it
goes into AGENTS.md's accepted diff. Where the marker lives needs one check before coding:
`StableIdentity` takes the node's user-data slot on the base node (`objects.cpp:53,64`), and the
object root and the shield part are other nodes, but the marker must not take a slot something else
writes there. If anything does, the marker goes into the node's user-data container instead.

### The walk

`Traversal::enterWalked` reads the marker as it enters a node, beside `mClass` and `mGlow`, and keeps
a scoped `mLampBody` for the subtree: set where the marker's light source would make a lamp (the same
`lightColour`, radius and `makeLight` that `addLight` uses, asked once at the marker), and restored
on the way out. `addDrawable` hands it to the placement, and `FrozenRun` replays it with the rest of
what a frozen root recorded.

### The cell ring

`CellReader::readStatic` already knows `givesLight` (the reference's record made a lamp). It goes
into `PreparedRef` and on to the placements the ring stands. This is exact by the same rule as the
walk.

### The device

- `InstanceRecord` gets `bool mLampBody`, and `GpuInstance::mClass` carries it as
  `INSTANCE_LAMP_BODY = 0x100u`, above the class bits, as `GpuLight::mTraits` packs a fill bit beside
  its classes. Every reader of `mClass` masks it with an 8-bit ray mask
  (`medium.glsl:204`, `spriterects.comp:260`, `traversal.glsl:370`), so the bit changes no traversal.
  `GpuInstance` keeps its 64 bytes.
- `bounceLanding` takes the glow off a hit whose instance carries the bit, on `PATH_INDIRECT` only:
  `hit.mEmissiveColour *= keep; hit.mEmitted *= keep;` with `keep` 0 or 1, no branch. The surface
  already holds `mInstance`, and `resolve` has just read that row.
- The lobe's far hit (`PATH_SEEN`), the water's legs and the panes keep the glow: they are pictures.
- `bouncevalidate.rgen` shades a kept sample through the same `bounceLanding`, so the reuse's
  reservoirs and its validation agree with the trace.

### Edge cases

- **An actor's carried light**: the marker is on the shield part, so only the carried model is a
  lamp body, not the actor.
- **A negative lamp** subtracts light. Its model's glow is not light it delivers, so the marker is
  honoured only for a lamp of positive intensity.
- **A lamp a script or a gate takes down**: the light and the model leave together.
- **A flickering lamp**: the lamp flickers and the paper's glow stays steady. That is the same as
  before for the eye, and the bounce now follows the lamp alone, which is the correct light.

### Issue 2: the comment

`EMISSIVE_INTENSITY`'s last paragraph in `look.h` becomes, in the file's voice:

> **A glow has no lamp of its own, and lights only by the bounce that lands on it** (`bounceArriving`):
> a lamp for every glowing shape is hundreds of lights in the grid per cell, and what they buy is the
> warm ring under a mushroom's cap. **A lamp's own model does not light even so**: its `LIGH` lamp
> already lights for it, through its fitting (`INSTANCE_LAMP_BODY`).

## Implementation plan

1. **Marker.** `components/sceneutil/lampbody.hpp` (new): `SceneUtil::LampBody`, holding an
   `osg::observer_ptr<LightSource>`. `SceneUtil::addLight` attaches it to the group it was given.
   Check first what writes the object root's and the shield part's user data. Add the hook to
   AGENTS.md's accepted diff.
2. **Walk.** `components/rtx/mirror/sceneextractor.cpp`: `NodeKind` learns the marker (or the walk
   asks `getUserData` at entry, as `StableIdentity::find` does), the scoped `mLampBody` beside
   `mGlow`, and `addDrawable` → `MeshInstance` → `InstanceRecord::mLampBody`. `FrozenRun` replays it.
3. **Ring.** `cellreader.cpp`: `PreparedRef::mLampBody = givesLight`, carried through
   `cellplacer.cpp` to the placement it stands.
4. **Device.** `components/rtx/shaders/scene.h`: `INSTANCE_LAMP_BODY`, documented on
   `GpuInstance::mClass`. `scenebuffers.cpp`'s `placeRow` ORs it in. A static assert holds it above
   `MASK_EVERY_CLASS | MASK_ADDITIVE | MASK_MEDIUM`.
5. **Shader.** `lib/shading.glsl`: `bounceLanding` takes the glow off a lamp body on
   `PATH_INDIRECT`. Rewrite `bounceArriving`'s glow paragraph and `EMISSIVE_INTENSITY`'s comment.
6. **Report.** `scene` reports how many placements stand as lamp bodies, beside "emissive
   materials", so a run says what the rule caught.
7. **Tests.**
   - Mirror (host): a group given to `SceneUtil::addLight` marks every drawable under it, and only
     those. An `OffDefault` lamp and a light that makes no lamp mark nothing.
   - Ring (host): a `LIGH` reference's placements stand as lamp bodies, and a static's do not.
   - GPU: a floor under a glowing quad, once as a lamp body and once not, with no other light.
     Hand-computed: the floor's bounce is the glow's form factor without the bit and nought with
     it. A glossy floor reflects the quad the same either way.
   - GPU: the validation re-shades a kept sample on a lamp body to what the trace shaded.
8. **Measure.** `./omw kernels` before and after. `./omw release shot --views=all --map
   --upscale=off` against a baseline: every place with a glowing lamp model moves, and it is
   expected to darken where the paper was counted twice. The M[FR] tree: the converged mean, and the
   fireflies with `--antifirefly=false`, which should fall as far as with the glow removed (0.08 in
   a thousand). `./omw release noise --ab=antifirefly` again, because the ring then has less to cut
   and its trade may change.
9. **Gate.** `./omw test`, then `./omw gate`.

## Open questions for you

- **The look will darken near lanterns.** The lamps' intensity is "set by eye, and provisionally"
  (`lightbuilder.cpp`, `sIntensity`), and it was set while the paper's glow was counted twice. After
  the fix, a room lit by paper lanterns may want the lamps brighter. That is a look decision, so the
  plan measures it and does not retune it.
- **Glows with no lamp still make fireflies** (option D). The anti-firefly ring covers them while
  the eye moves. Sampling emissive triangles directly is the full answer, and is its own proposal.
