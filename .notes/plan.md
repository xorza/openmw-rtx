# Lamp glow counted once, and no cloud shadows — proposal and plan

Two changes, each in its own part: a lamp's glowing model that lights the room twice (part 1), and
the cloud deck's shadow, which goes (part 2). One gate runs after both.

# Part 1: a lamp's glowing model lights the room twice

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

### Measured on vanilla content (an upper bound)

Every glow taken off the diffuse bounce's far hit, `shot --upscale=off --exposure=64`, debug build.
This is option C below, so it bounds the chosen change from above: the two agree where every
glowing material in the room stands on a lamp's model, which step 6's report confirms per place.

| place | glowing materials | lamps | frame mean | pixels >10% darker |
|---|---|---|---|---|
| `balmora-mages-guild` | 2 | 30 | −1.9% | 5.0% |
| `probe-guild-planter` (the guild at night) | 2 | 30 | −2.5% | 11.6% |
| `ahemmusa-yurt` | 2 | 4 | −7.7% (green −14%) | 26.5% |
| `seyda-neen-customs` | 0 | 27 | 0.0% | 0.0% |

The yurt darkens most around its hanging lantern, where one lamp lights the room. No pixel
anywhere darkens by more than a quarter, so the change moves the balance near a lantern and not the
room's level, and under the measured exposure a player sees less still.

### Not a second path: a lamp's particle flame

A torch's flame is often a particle system. Particles are sprites and not instances, and
`solidMask` leaves `MASK_PARTICLE` out of every ray but the eye's, so no bounce meets a flame and
none counts it beside its lamp. Nothing here changes for them.

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
goes into AGENTS.md's accepted diff.

**The marker takes the node's user-data slot (`osg::Object::setUserData`), which is free on both
nodes it lands on.** Checked: `StableIdentity` stamps the slot on the reference's base node and the
cell roots (`objects.cpp`), and neither is a node `addLight` is given. A placed lamp's object root
is a fresh `osg::Group` or the model's root (`getOrCreateObjectRoot`); the NIF loader writes user
values and descriptions into a node's container and never its slot. The shield part's container is
cloned from its template when the part is attached (`attach.cpp`, `mergeUserData`), before
`addExtraLight` marks it. `addLight` has one caller, `Animation::addExtraLight`.

### The walk

`Traversal::enterWalked` reads the marker as it enters a node, beside `mClass` and `mGlow`, and keeps
a scoped `mLampBody` for the subtree, restored on the way out. `addDrawable` hands it to the
placement.

- **The lamp is made once, at the marker.** The marker is always an ancestor of its
  `LightSource`, because `addLight` attaches the source inside the group it marks (at
  `AttachLight` or at the group). So the walk makes the lamp where it meets the marker
  (`lightColour`, the radius and `makeLight`, as `SceneExtractor::addLight` does now), sets
  `mLampBody` from the answer, and keeps the answer in the scope beside the source it is for. When
  the walk meets that `LightSource`, it adds the kept lamp and makes nothing again. A source with no
  marker over it (a test's bare source) is made where it is met, as now.
- **Positive intensity only**: the bit is set where the lamp was made and its intensity is over
  nought in every channel, so a negative lamp's model keeps its glow.
- **The decision is fixed for the placement's life.** `makeLight` refuses only for the record's
  radius or a colour of mixed sign. Flicker scales the colour and never changes its sign, so a
  flickering lamp cannot move its model in and out of the rule from frame to frame.
- **No frozen run to replay.** A reference root that holds a `LightSource` is never frozen: meeting
  the source sets `mChangeable` (`enterWalked`), so the root is walked every frame and the bit is
  set every frame.
- **Stood again where it changes.** `addDrawable` stands a placement again where its mesh, material,
  class or winding changed; `mLampBody` joins that comparison, so a slot whose identity a new part
  reuses (a shield where a torch was) cannot keep the torch's bit.

### The cell ring

`CellReader::readStatic` already knows `givesLight` (the reference's record made a lamp). It goes
into `PreparedRef` and on to the placements the ring stands, as `givesLight && !record.mNegative`
so the walk's positive-intensity rule holds here too. `readLamp` is where both are known.

### The device

- `InstanceRecord` gets `bool mLampBody`, and `GpuInstance::mClass` carries it as
  `INSTANCE_LAMP_BODY = 0x100u`, above the class bits, as `GpuLight::mTraits` packs a fill bit beside
  its classes. Every reader of `mClass` masks it with an 8-bit ray mask
  (`medium.glsl:204`, `spriterects.comp:260`, `traversal.glsl:370`), so the bit changes no traversal.
  The structure's own mask is `InstanceRecord::mMask`, a field apart that `placeRow` never ORs the
  bit into, so the bit cannot reach the eight-bit mask of a `VkAccelerationStructureInstanceKHR`.
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
   `osg::observer_ptr<LightSource>`. `SceneUtil::addLight` puts it in the user-data slot of the
   group it was given. Add the hook to AGENTS.md's accepted diff.
2. **Walk.** `components/rtx/mirror/sceneextractor.cpp`: the walk asks the slot at entry with an
   exact type test, as `StableIdentity::find` does. At a marker it makes the lamp once, keeps it in
   the scope beside its source, and sets the scoped `mLampBody`; the `LightSource` branch of
   `enterWalked` adds the kept lamp where the source is the scope's own. `addDrawable` →
   `MeshInstance::mLampBody` → `InstanceRecord::mLampBody`, and `mLampBody` joins the comparison that
   stands a placement again.
3. **Ring.** `cellreader.cpp`: `readLamp` answers whether the reference gives a lamp of positive
   intensity, and `PreparedRef::mLampBody` takes that answer; `cellplacer.cpp` carries it to the
   placement it stands.
4. **Device.** `components/rtx/shaders/scene.h`: `INSTANCE_LAMP_BODY`, documented on
   `GpuInstance::mClass`. `scenebuffers.cpp`'s `placeRow` ORs it in. A static assert holds it above
   `MASK_EVERY_CLASS | MASK_ADDITIVE | MASK_MEDIUM`.
5. **Shader.** `lib/shading.glsl`: `bounceLanding` takes the glow off a lamp body on
   `PATH_INDIRECT`. Rewrite `bounceArriving`'s glow paragraph and `EMISSIVE_INTENSITY`'s comment.
6. **Report.** `scene` reports how many placements stand as lamp bodies, beside "emissive
   materials", so a run says what the rule caught.
7. **Tests.**
   - Mirror (host), beside the light tests in `rtx/mirror/extractor/lights.cpp`, which already seed
     the fallbacks `createLightSource` reads: a group given to `SceneUtil::addLight` marks every
     drawable under it and only those, with the light at an `AttachLight` node and at the group; a
     negative lamp marks nothing; and the walk adds one lamp for the marker's source, not two.
     (`OffDefault` is the game's: `Light::insertObjectRendering` passes `allowLight` false and
     `addLight` is never called, so no mirror test reaches it.)
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

# Part 2: the cloud deck casts no shadow

## Summary

Requested in `.notes/todo.txt` ("remove cloud shadows"). The deck stays in the sky and in
reflections exactly as it is. What goes is the one place it dims a light:
`cloudShadow`, which `skyPassage` multiplies into every ray to the sun or a moon. The weather still
dims the sun, because the content's own `Sun_*_Color` for each weather does that, and nothing here
changes it.

## Findings

- **One reader.** `skyPassage` (`lib/lights.glsl`) multiplies `cloudShadow(position,
  sky.mDirection)` into the passage's translucent half. Every sky ray goes through `skyPassage`: a
  surface's sun or moon ray (`gather`), a froxel of the air, and a step of a water shaft. So one
  deleted factor takes the shadow off the ground, the fog and the water together, and none of them
  can keep a shadow the others lost.
- **What the shadow is.** `cloudShadow` (`lib/sky.glsl`) reads the deck's sheet where the ray to the
  light crosses the layer at `sCloudAltitude`, and darkens by `exp(-CLOUD_SHADOW_DEPTH × max(alpha
  - mCover, 0) × mOpacity)`, where `mCover` is the sheet's mean alpha.
- **Only three weathers cast today**, measured on the eleven sheets in `Morrowind.bsa` and
  `Bloodmoon.bsa`: clear (mean alpha 0.253, from 0 to 1), cloudy (0.738, from 0.016 to 0.996) and
  foggy (0.787, from 0.310 to 1). The other eight — overcast, rainy, thunder, stormy, ashstorm,
  blight, snow and blizzard — have an alpha of one in every texel, so `alpha - mCover` is nought
  everywhere and they cast nothing already.
- **What stays in use.** `deckOver`, `cloudSheetAt`, `CloudDeck::mAltitude` and `mPerTile` draw the
  deck itself (`cloudDeck`, read by the eye's sky and by `reflectedSky`). `MeanTexel::mAlpha` stays:
  `opaque()` and the emitters read it.
- **What goes.** `cloudShadow`, `CLOUD_SHADOW_DEPTH` (`look.h`), `CloudDeck::mCover` (`sky.h`),
  `CloudSheet::mCover` (`skybuilder.hpp`) and the two lines that fill them (`skybuilder.cpp`, where
  the sheet is averaged and where two sheets cross). Nothing else reads `mCover`.
- **Cost.** One texture read on each sky ray. The saving is expected to stay inside the bench's
  noise. The `exteriors` suite checks that it is not slower.

## Implementation plan

9. **Shader.** `lib/lights.glsl`: `skyPassage` returns `lightPassage`'s answer as it is. Its doc
    loses "the deck is the one occluder no ray finds" and "and the deck let through". `lib/sky.glsl`:
    delete `cloudShadow` and its doc. `look.h`: delete `CLOUD_SHADOW_DEPTH`.
10. **Host.** Delete `CloudDeck::mCover` and `CloudSheet::mCover` and the lines that fill them.
    `CloudDeck`'s assert goes from 96 to 92 bytes. `VisibilityConstants` stays 1600 bytes with
    `mTables` at 1416: measured, the fields above `mTables` end at exactly 1416 today, so four bytes
    fewer leave four bytes of padding in front of the eight-aligned tables, on both sides alike, as
    `mTables`' own comment states. Its two asserts stay as they are and prove it.
11. **Comments.** `visibility.h`'s fog drift paragraph argues from "cloud shadows crossing the ground
    one way while the air moves another": it argues from the deck drifting instead, which is still
    true. `CloudSheet::mMean` and `CloudDeck::mMean` stay; their docs name no shadow.
12. **Tests.**
    - `theDeckShadowsWhatStandsUnderIt` (`rtxvulkan/trace/visibility/sky.cpp`) becomes the new
      claim on the same fixture: a solid sheet over the floor at full opacity leaves the sun's
      `0.5 × 2 / π = 0.31831` whole. Its other cases (no deck, a stand-in sheet) fold into that one
      assertion, since every case now reads the same number.
    - `skybuilder.cpp`'s opened-sheet test loses its cover expectation and the doc's "and a cover of
      0.4".
    - `frameworld.cpp`: the hidden sky's `mClouds.mTexture` assertion goes. Its message says why it
      was there, "the deck's shadow went with its picture", and the shadow was its only reader:
      `skyRadiance` returns the fog colour before it reaches the deck where `mSkyDrawn` is nought,
      and `reflectedSky` goes through `skyRadiance`.
13. **Measure.** `./omw kernels` before and after: the kernels that trace a sky ray move, and
    nothing else does. `./omw release shot --views=all --map --upscale=off` against a baseline: only
    exteriors under a sheet that casts today move, and every interior and every overcast, rain or
    thunder view stays byte-identical. `./omw release bench --suite=exteriors` before and
    after, back to back.
14. **Notes.** Delete "remove cloud shadows" from `.notes/todo.txt` when the change stands.

# Gate

After both parts: `./omw test`, then `./omw gate`.

# Decisions

Both decided as recommended.

1. **The lamps keep their brightness.** `sIntensity` (`lightbuilder.cpp`) stays. Part 1 darkens a
   lantern-lit room by 2 to 8% of its frame mean on vanilla content (the table in part 1), and a
   room with no glowing lamp model not at all, so a global raise would brighten rooms that lost
   nothing. Step 8's pictures show the result, and a retune is its own look change.
2. **Glows that have no lamp get no light sampling here (option D).** After part 1 a glow lights by
   the bounce only where it has no lamp: a mushroom, a glow map. The four places measured hold none
   outside lamp models (step 6 confirms the guild's and the yurt's two), and the anti-firefly ring
   holds their fireflies down while the eye moves. Sampling emissive triangles is its own proposal
   if it is ever wanted.
