# The indirect light, the lamp glow and the cloud shadows — proposal and plan

Three parts: a lamp's glowing model that lights the room twice (part 1), the cloud deck's shadow,
which goes (part 2), and the indirect light's review, with what it costs and what to change
(part 3).

**The order of work** is step 15 first, then parts 2 and 1, then the rest of part 3 in its order.
Step 15 is the instrument that measures the first frames after a cut, which is where the fireflies
were reported; built first, it measures part 1's effect on them, and gives every later step its
baseline. Part 2 is next because it is the smallest and moves only exteriors. Part 3's own
measurements are taken after parts 1 and 2.

**After each part**, `./omw test`; after the last step, `./omw gate`.

**How a measured step runs from a working session**: a bench is started in the background and
waited on, and only its medians and p99 are read, because this session's own spinner moves the
host rows and the tail (`AGENTS.md`, "Measure on a quiet desktop"). A warm-up leg comes first, the
two sides run back to back, and each side once more after the other where the difference is under
a tenth of a millisecond. `noise` judges pictures and not times, so it runs beside anything.

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

**And the slot is the instance's own, not its template's.** OSG's copy shares a node's user-data
container between a template and its clones unless the copy asks for a deep one, and the NIF loader
gives every model root a container (the file hash). A model is instanced through
`SceneManager::cloneNode`, whose `SceneUtil::CopyOp` sets `DEEP_COPY_USERDATA`: each lamp's root
owns its container, so a marker written on one lamp is on no other instance of its model, and not
on the cached template.

### The walk

`Traversal::enterWalked` reads the marker as it enters a node, beside `mClass` and `mGlow`, and keeps
a scoped `mLampBody` for the subtree, restored on the way out. `addDrawable` hands it to the
placement.

- **The record's half of the decision at the marker, the lamp at its source.** The marker is
  always an ancestor of its `LightSource`, because `addLight` attaches the source inside the group
  it marks (at `AttachLight` or at the group). But the lamp's position is the source's own world
  matrix, which the walk knows only when it reaches the source: an `AttachLight` node can stand
  below the marker under a transform a controller moves. So `makeLight` splits in two: what the
  record decides (a radius over nought, and a colour of one sign: the refusal and the sign) and the
  placement (the position, and the finite test on the made lamp). At the marker the walk asks the
  first half of the marker's source, sets `mLampBody` from it, and keeps the answer in the scope
  beside the source it is for. At the source the walk places the lamp from the kept answer and
  decides nothing again. A source with no marker over it (a test's bare source) asks both halves
  where it is met, as now. A lamp whose placement is not finite is refused at the source while its
  model keeps the bit, and loses its glow in the bounce with no lamp to give the light instead. That
  needs a source under a transform that is not finite, which is a fault before this rule; the
  refusal is reported as every refused lamp is, so a run says where it happened.
- **An expired marker is no lamp body.** The marker names its source by `osg::observer_ptr`; where
  the source has gone (an equipment change detached the part's light before the part), the marker
  answers nothing and the subtree glows as any other.
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
2. **Walk.** `components/rtx/scene/lightbuilder.cpp`: `makeLight` splits into the record's half
   and the placement, both behind the one call they make today for a bare source.
   `components/rtx/mirror/sceneextractor.cpp`: the walk asks the slot at entry with an exact type
   test, as `StableIdentity::find` does. At a marker it asks the record's half once, keeps the
   answer in the scope beside its source, and sets the scoped `mLampBody`; the `LightSource` branch
   of `enterWalked` places the lamp from the kept answer where the source is the scope's own. `addDrawable` →
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
     negative lamp marks nothing; the walk adds one lamp for the marker's source, not two, at the
     source's own position under an `AttachLight` node that stands apart from the marker; and a
     marker whose source is gone marks nothing.
   - Light builder (host), `rtx/scene/lightbuilder.cpp`: the record's half refuses exactly what
     `makeLight` refuses today for a radius and a mixed sign, and the two halves together make the
     lamp `makeLight` made, field for field.
     (`OffDefault` is the game's: `Light::insertObjectRendering` passes `allowLight` false and
     `addLight` is never called, so no mirror test reaches it.)
   - Ring (host): a `LIGH` reference's placements stand as lamp bodies, and a static's do not.
   - GPU: a floor under a glowing quad, once as a lamp body and once not, with no other light.
     Hand-computed: the floor's bounce is the glow's form factor without the bit and nought with
     it. A glossy floor reflects the quad the same either way.
   - GPU: the validation re-shades a kept sample on a lamp body to what the trace shaded.
8. **Measure.** `./omw kernels` before and after. `./omw release shot --views=all --map
   --upscale=off` against a baseline: every place with a glowing lamp model moves, and it is
   expected to darken where the paper was counted twice, and nowhere more than the vanilla table
   above bounds. `noise --cut=1`, `--cut=2` and `--strafe=150` at the guild, the planter and the
   yurt, before and after: the firefly share step 15 counts, which the change is for. The M[FR]
   tree only where M[FR] is installed (the bench's data folders hold vanilla alone today): its
   converged mean, and its fireflies with `--antifirefly=false`, which should fall as far as with
   the glow removed (0.08 in a thousand). `./omw release noise --ab=antifirefly` again, because the
   ring then has less to cut and its trade may change.

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
    thunder view stays as it was within `sDenoiserNoiseLevels`, the bound `shot --against` holds a
    denoised picture to (`architecture.md` says why a denoised frame is not bit-exact on this card). `./omw release bench --suite=exteriors` before and
    after, back to back.
14. **Notes.** Delete "remove cloud shadows" from `.notes/todo.txt` when the change stands.

# Part 3: the indirect light, measured and reviewed

## Summary

The indirect light is most of the frame. At 1920×1080 under FSR's quality mode (1280×720 traced),
release build, hot card, it is 4.1 ms of the guild's 5.6 ms on the device and 4.0 ms of the ship's
5.8 ms. It splits into three nearly equal parts: the bounce's share of the trace, the bounce's
reuse, and the denoiser.

The review held the code against NVIDIA's NRD (ReLAX, ReBLUR), RTXDI and ReSTIR PT Enhanced (Lin,
Kettunen, Wyman 2026). The resampling arithmetic is correct. One fault was found: a short history's
variance is an absolute number, so the wavelet treats a fresh pixel differently by day and by night.
Three savings were measured and decided: a lighter wavelet (0.28 ms at the guild, 0.24 at the
ship, less noisy in every leg), the reuse by the sky (temporal in rooms, none outdoors: 0.54 ms at
the guild, 1.09 at the ship, no noise lost), and cheaper lamp sampling at the far hit (whose walk
is 0.30 ms in a lit room). Of three noise techniques from the literature, one still applies once
the spatial reuse leaves the default (N3); the other two serve the spatial reuse alone and wait.

The fireflies in the first frames at the guild come mostly from the lantern glow that part 1
removes. The tree has no instrument for the first frames after a cut in a still world, so the
first step of this part builds one.

## Where the frame goes

Zone medians in ms, `./omw release bench`, 10 s a place, back to back. "Off" is `--indirect=off`.

| zone | guild | guild off | ship | ship off |
|---|---|---|---|---|
| device frame (`finish`) | 5.60 | 1.48 | 5.79 | 1.79 |
| trace | 1.66 | 0.59 | 2.79 | 1.09 |
| reuse: resolve, pairs, temporal, validate | 0.44, 0.40, 0.31, 0.16 | — | 0.38, 0.35, 0.24, 0.12 | — |
| denoiser: filter, accumulate, clamp | 0.99, 0.27, 0.21 | — | 0.79, 0.23, 0.16 | — |

At native 1080p the same split holds at twice the size: the guild's trace 3.80, filter 2.32 and
reuse 3.08 ms.

**The bounce's far hit is half of its trace.** Each part taken off the far hit's shading alone
(`PATH_INDIRECT`), release, beside a baseline built and run just before it:

| taken off the far hit | guild trace | ship trace |
|---|---|---|
| the lamp walk (`weighLamps`, every lamp in the grid cell; the guild has 30) | −0.30 | −0.18 |
| the sky source's ray | −0.03 | −0.40 |
| the ambient occlusion ray | −0.18 | −0.28 |

So the far hit costs 0.51 ms of the guild's 1.07 ms of bounce and 0.86 of the ship's 1.70. The rest
is the bounce ray's traversal and the far surface's resolve.

**What each reuse mode costs** (guild / ship): spatiotemporal 1.31 / 1.09 ms, temporal 0.77 / 0.66,
own 0.20 / 0.17, off nothing.

**What the reuse buys**, `noise --ab=bounce-reuse=off,spatiotemporal --suite=bounce --still`, frame
noise against 16 frames averaged, then bias against a converged reference:

| place | still | strafed in | walked in | bias, still |
|---|---|---|---|---|
| mages' guild | 0.69 → 0.59 | 1.34 → 1.24 | 1.49 → 1.32 | 1.80 → 1.85 |
| guild's planter | 0.89 → 0.69 | 1.62 → 1.42 | 1.60 → 1.42 | 2.08 → 2.26 |
| Ahemmusa's yurt | 0.54 → 0.61 | 1.71 → 1.75 | 1.85 → 1.94 | 2.70 → 1.81 |
| Seyda Neen's pier | 0.49 → 0.49 | 1.16 → 1.16 | 1.18 → 1.18 | 1.70 → 1.75 |
| Seyda Neen's pond | 0.48 → 0.49 | 1.21 → 1.21 | 1.17 → 1.17 | 1.59 → 1.57 |

Indoors under lamps the reuse takes 10 to 20% off the noise. Under the sky it does nothing: the sky
is a large source that one bounce already finds. At the yurt it trades noise for bias.

## Fireflies in the first frames

- **The first measured frame at the guild**, which is the second after the cut (a stop's first
  frame draws the world and is not measured, `Stager`), isolated bright pixels counted against each
  run's own 5×5 median (`bench --warmup=0 --pictures`, upscaled): 518 with the anti-firefly ring and
  638 without; 522 with the reuse off, and the same with the history fix off. The ring works, and
  neither the reuse nor the history fix makes them. By the fourth every run is down to its settled
  count. The count is rough: lamp flicker moves pixels in a world `bench` does not freeze.
- **The guild's lanterns are the source that part 1 removes**: on vanilla `probe-guild-planter`
  strafed in 150 units, the frame's noise falls from 1.33 to 1.02 with the glow off the bounce.
- **No instrument measures the first frames.** `bench` does not freeze the world, so lamp flicker
  moves every pixel count; `noise` measures after 128 warm frames or after a flight, never N frames
  after a cut. Step 15 adds that leg.
- **Under the upscaler a fresh frame has no history either**, so FSR shows whatever the denoiser
  hands it. The ring is the only guard; there is no clamp on a sample, by design (`accumulate.comp`
  says why).

## Findings

### Correctness

- **F1. A short history's variance is an absolute radiance.** `accumulate.comp` writes
  `NO_HISTORY_VARIANCE = 1` for every pixel under `ACCUMULATE_SETTLED` (4) frames, and the history
  fix's level hands the wavelet `1/n` of it. The wavelet's brightness weight is
  `exp(-|Δl| / (4 σ))`, so σ = 1 means "blur freely" only where the light is much dimmer than one.
  At exposure 64 the guild's light is about 0.02 and every tap passes; out of doors the sun's
  irradiance is `DAYLIGHT × DAYLIGHT_GAIN` = 80, a bounce is tens of units, and a fresh pixel's taps
  are refused. The same pixel is filtered fully at night and hardly at noon. Practice: ReLAX
  estimates a short history's variance spatially (`spatialVarianceEstimationHistoryThreshold = 3`:
  a 5×5 normal-weighted estimate, boosted by `max(1, 4 / (N + 1))`), as SVGF does over 7×7. Here
  the clamp pass already holds the 5×5 of fast means in shared memory and computes their deviation
  (`fastDeviation`), so the estimate costs a few instructions.
- **Verified and correct, no change:** the pairwise MIS weights with confidences
  (`canonicalWeight`, `neighbourWeight`, Wyman et al. 2023 Algorithm 7); the reconnection Jacobian
  with the sheet's side (Ouyang et al. 2021, eq. 11); the pair bits, read both ways because each
  link's texture is its own inverse; the validation, which replaces a stale radiance and keeps `W`,
  a valid contribution weight because the sample's distribution did not change; the anti-firefly
  ring's barriers.

### Noise

- **N1. Colour noise.** The resolve shades the one kept sample, `F(Y) W`, and resampling weighs by
  luminance alone, so hue is poorly sampled. ReSTIR PT Enhanced (§6.3) shades with the sum of the
  vector weights `Σ m_i F(Y_i) W_i |J_i|` of every input, which the resolve already evaluates.
  Each input needs its visibility from this pixel: a partner's is its pair bit, the fresh
  candidate's is its own trace, and the pixel's carried sample needs the final ray the resolve
  traces now only where it is kept.
- **N2. Correlation.** The boiling filter (`BOUNCE_BOILING_LIMIT`) drops a carried sample worth 41
  times its group's mean. ReSTIR PT Enhanced (§5, Fig. 9) finds RTXDI's boiling filter loses
  energy and replaces it with a duplication map: the share of a 17×17 neighbourhood that holds
  the same sample lowers that pixel's temporal confidence cap, `lerp(20, 1, D^0.1)`.
- **N1 and N2 serve the spatial reuse, which decision 3 takes out of the default.** N1's sum runs
  over the spatial merge's inputs; with the temporal reuse alone the resolve has one input, and the
  sum would need the temporal pass to hand on both of its own and a visibility ray for the carried
  one. N2's correlation is fireflies spread across pixels by spatial reuse. Neither is planned while
  no default frame runs the spatial reuse; both stand here for the day it comes back.
- **N3. Disocclusion.** Newly visible surfaces start with no history in the reuse and in the
  accumulator. Dual motion vectors (Zeng et al. 2021) reproject a disoccluded pixel by the motion of
  the surface that was behind the occluder, which ReSTIR PT Enhanced (§6.4) applies to temporal
  resampling.

### Performance

- **P1. The wavelet reads about 102 taps a pixel.** Three 5×5 levels, each with a 3×3 variance
  prefilter, over a 32-bit bounce and a 32-bit fill. ReLAX runs 3×3 levels that read the centre's
  variance only, after one shared-memory level. Measured: levels two and three as 3×3 with the
  centre's variance take the filter from 0.99 to 0.58 ms at the guild and 0.85 to 0.46 at the ship.
  Their reach then falls from 14 pixels to 8, so the change is four levels (reach 16): 0.72 ms at
  the guild and 0.58 at the ship, less noisy in every leg (decision 4). The prefilter alone on the
  first level saves 0.03 ms, too little to plan on its own.
- **P2. The reuse costs 1.1 to 1.3 ms for 0 to 20% less noise** (the tables above). Decision 3
  runs it temporal in rooms and not at all under the sky.
- **P3. The far hit's lamp walk weighs every lamp in its grid cell**, up to `LAMPS_AT_A_POINT`
  (256), for a term the wavelet filters: 0.30 ms of the guild's trace. RTXDI weighs a few
  candidates drawn by power (its local light presampling) and lets resampling do the rest. Here: at
  `PATH_INDIRECT`, `K` candidates drawn from the cell's list, each weighed as now, the estimate
  divided by the draw's chance — unbiased, more noise at the far hit, which the wavelet filters. A radiance cache would take this, the sun ray and the ambient ray
  off the far hit together; decision 5 leaves it out.
- **P4. The candidate loop reads three vertex positions and a matrix for every candidate**
  (`RTX_READ_CANDIDATE`) before `candidateStops` learns most candidates need neither: a material
  with no mask returns at once, and a shadow ray's cone is nought. Unmeasured; moved into the
  masked path it may save traversal time on every ray.
- **P5. Three passes match the same four history taps** against the accumulator's surface: the
  accumulator, the shadow tiles and the glossy filter (`heldSurfaceMatches`). The accumulator can
  write its four bits once and the other two read them. Small, and the tree's rule.

## Implementation plan

Every step: `./omw kernels` before and after, `./omw release shot --views=all --map --upscale=off`
against a baseline outside `/tmp`, the `noise` A/B legs that step names, `./omw release bench`
back to back for any step that claims time, and `./omw repeat --pairs=10` where a frame's content
can move. A step whose A/B fails its bar is reverted. **Every step's numbers go to the Results
section at the end**, kept or reverted, because a finished step leaves this list.

**The bar a step must pass**, unless its decision says otherwise: no place of `--suite=bounce`
noisier by more than 0.02 or more biased by more than 0.05 in any leg it is run on, still,
strafed (`--strafe=150`) and walked (`--walk=150`).

16. **F1: a short history's variance, estimated.** In `accumulateclamp.comp`, under
    `ACCUMULATE_SETTLED` frames, the slow mean's variance becomes `fastDeviation² × max(1, 4 / (N +
    1))` from the 5×5 it already holds, where it stores the slow mean (`blended`'s alpha, which it
    passes through today), with `N` the count it already reads for the ring. The accumulator's
    constant stays only for a pixel the clamp passes over (no surface), and is renamed to say so.
    The history fix's level reads the new variance as every level does. A shader function in
    `accumulate.h` beside `ringHeldLuminance`, with a test by hand; a GPU test that a fresh pixel's
    wavelet weights are the same at two exposures of the same scene (the fault's own shape). A/B:
    `--cut=1`, `--cut=2` and `--strafe` at the pier and the pond (daylight, where the fault bites)
    and the guild.
17. **P1: the lighter wavelet** (decision 4). `atrous.comp`: past the first level a 3×3 kernel
    (ReLAX's Gaussian, 0.44198 and 0.27901) and the centre's variance; the first level, with the
    history fix, unchanged; `ATROUS_LEVELS` 4, so the steps are 1, 2, 4 and 8. ReLAX's two other
    rules for its later passes, a normal test tightened by the step and a random offset past step
    4 against ringing, are not taken: the A/B passed without them. `look.h`'s `ATROUS_LEVELS` and
    `architecture.md`'s denoiser paragraph say what the levels are now, with the measurements. The
    GPU tests that quote measured filter figures (`rtxvulkan/trace/visibility/filter.cpp`) are run
    and their figures updated where they moved; a claim that fails is a finding, not a number to
    retune.
18. **P2: the reuse by the sky** (decision 3). `BounceReuse` gains a mode, `rooms`, the default:
    temporal where the frame's sky lights nothing (`mAmbientFromSky` nought: an interior) and off
    where it does. **`Reconstruction::resolve` already runs once a frame** in
    `VulkanRenderer::renderFrame`, with the frame's camera in hand: it takes whether the sky lights
    this frame and resolves `rooms` there, so every reader of the frame's `Reconstruction`, the
    trace chain, `composedByTrace` and `FrameResult`, reads the effective mode. A picture's
    reconstruction (`forPicture`) keeps no reuse. The other modes stay for A/Bs (`--bounce-reuse=`);
    the reuse is not a player setting, so no settings page changes. A door is a cut, so the history
    the temporal pass reads after one is empty either way. The world's chain keeps its reservoirs
    whatever a frame runs (`TraceChain::mReuses`, set when the chain is made), so a door costs no
    allocation. Tests: `reconstruction.cpp`, the mode resolves to temporal under no sky and to off
    under one. Every GPU test that builds a `ReconstructionRequest` without naming the reuse takes
    the new default, and under a sky that is no reuse: `trail.cpp`'s second test (line 193) is one,
    and the rest are found by a search for requests that name no `mBounceReuse`. Each names the mode
    its claim was measured under. The bench's report names the effective mode per place, since a run
    now differs by place. `architecture.md`'s reuse paragraph says when the reuse runs. Measure:
    `bench --suite=default` and `--suite=interiors` against today, and one `noise
    --ab=bounce-reuse=rooms,spatiotemporal --suite=bounce --still` to confirm the tables.
19. **P3: the far hit draws its lamps** (decision 5). At `PATH_INDIRECT`, `weighLamps` weighs
    `min(n, K)` of the cell's `n` lamps: where `n ≤ K`, every lamp, as now, and exact; past it, `K`
    drawn uniformly with replacement, each offered to the reservoir at its target over `K / n`
    (resampled importance sampling with a uniform source, unbiased). One loop with the index
    selected, not two paths. The draws come after the sequence's fixed places, so no other draw
    moves. `PATH_SEEN` keeps the full walk. **Uniform first, a power table only if it fails**: a
    table per grid cell is a new structure the host builds and the device reads, and is planned
    only if uniform draws fail the bar. `K` from an A/B of 4 and 8 on `--suite=bounce` and
    `--suite=interiors`, and the time on `bench --suite=interiors`. A test by hand on three lamps
    and `K = 2`: the estimate's mean over the draws equals the full walk's.
20. **N3: dual motion vectors**, designed before coded. Read Zeng et al. 2021 (*Temporally
    Reliable Motion Vectors for Real-time Ray Tracing*) and how ReSTIR PT Enhanced (§6.4) applies it
    to temporal resampling: what the second vector is, which pass computes it and from what, and
    what it costs a pixel. Write that design into this step, with what the trace must write beside
    `CHANNEL_MOTION`, and only then build it: for the reuse's temporal pass and the accumulator,
    where the surface's own reprojection is refused (a disocclusion), the history is fetched along
    the second vector and held to the same surface test. A/B walked and strafed, and `--cut` legs to
    show the still frame does not move. A design that needs a second trace of the previous frame's
    geometry is reported and not built.
21. **P4 and P5**, each measured and kept only where it saves time.

N1 and N2 are not steps: the finding above says why.

# Gate

After the last step: `./omw test`, then `./omw gate`.

# Decisions

Decisions 1, 2, 4 and 5 were taken as recommended, and 4's legs all passed. Decision 3 was
delegated, and the rule below chose the reuse mode from the measurements.

1. **The lamps keep their brightness.** `sIntensity` (`lightbuilder.cpp`) stays. Part 1 darkens a
   lantern-lit room by 2 to 8% of its frame mean on vanilla content (the table in part 1), and a
   room with no glowing lamp model not at all, so a global raise would brighten rooms that lost
   nothing. Step 8's pictures show the result, and a retune is its own look change.
2. **Glows that have no lamp get no light sampling here (option D).** After part 1 a glow lights by
   the bounce only where it has no lamp: a mushroom, a glow map. The four places measured hold none
   outside lamp models (step 6 confirms the guild's and the yurt's two), and the anti-firefly ring
   holds their fireflies down while the eye moves. Sampling emissive triangles is its own proposal
   if it is ever wanted.
3. **The reuse runs temporal in rooms and not at all under the sky** (P2, delegated to the
   measurements, which chose it). Spatiotemporal costs 1.31 ms at the guild and takes 10 to 20% off
   the noise indoors and nothing under the sky; temporal alone costs 0.77 ms; off costs nothing. The choice reads the
   three legs of `noise --ab=bounce-reuse=temporal,spatiotemporal` beside the off A/B above:
   - each mode's noise gain over off is summed over the indoor places and legs, and divided by its
     cost, so every mode is judged by the noise it removes per millisecond;
   - a mode is out where, at any place and leg, it is noisier than off by more than the bar (0.02)
     without being less biased by more than its bar (0.05), or more biased by more than 0.05
     without being less noisy by more than 0.02: a mode may trade one error for the other, as the
     spatiotemporal reuse does at the yurt (noise 0.54 → 0.61, bias 2.70 → 1.81), but not lose
     on one and gain nothing on the other;
   - of the modes left, the cheaper one is taken unless the dearer one removes at least as much
     noise per extra millisecond as the cheaper one removes per millisecond of its own: the last
     millisecond spent must buy as much as the first.
   **The temporal A/B** (`--ab=bounce-reuse=temporal,spatiotemporal`, frame noise, then bias):

   | place | still | strafed in | walked in | bias still / strafed / walked |
   |---|---|---|---|---|
   | mages' guild | 0.63 / 0.59 | 1.27 / 1.24 | 1.34 / 1.32 | 1.91 / 1.85, 2.14 / 2.10, 2.56 / 2.52 |
   | guild's planter | 0.76 / 0.69 | 1.43 / 1.42 | 1.50 / 1.42 | 2.30 / 2.26, 2.89 / 2.74, 3.11 / 3.08 |
   | Ahemmusa's yurt | 0.63 / 0.61 | 1.75 / 1.75 | 1.91 / 1.94 | 2.07 / 1.81, 3.75 / 3.49, 4.45 / 4.13 |
   | Seyda Neen's pier | 0.49 / 0.49 | 1.17 / 1.16 | 1.18 / 1.18 | 1.76 / 1.75, 1.44 / 1.43, 2.15 / 2.15 |
   | Seyda Neen's pond | 0.49 / 0.49 | 1.21 / 1.21 | 1.17 / 1.17 | 1.59 / 1.57, 1.72 / 1.71, 1.60 / 1.60 |

   **The rule applied.** Noise removed against off, summed over the three rooms and three legs:
   spatiotemporal 0.75, temporal 0.51. Per millisecond at the guild: spatiotemporal 0.57,
   temporal 0.66; spatiotemporal's extra 0.54 ms buys 0.24, which is 0.44 a millisecond. So indoors
   the temporal reuse wins: the last half millisecond of the spatial half buys two thirds of what
   the first does. Neither is out indoors: the yurt is noisier under both (still 0.54 → 0.63 and
   0.61) but its bias falls by 0.6 to 0.9, the trade the rule allows.

   **Under the sky both are out.** At the pier walked in, either reuse raises the bias by 0.07 (2.08
   → 2.15) and lowers the noise by nothing; still, by 0.05 and 0.06. Off is the best balance where
   the sky lights the frame by day. A night exterior, lit by its lamps, could have behaved as a
   room, so it was measured (`--views=balmora,balmora-fog-night --hour=23`, with `balmora` and
   `seyda-neen-ship-dawn` by day beside it).

   **Out of doors it is the pier everywhere** (`--ab=bounce-reuse=off,temporal`, noise then bias):

   | place | still | strafed in | walked in | bias still / strafed / walked |
   |---|---|---|---|---|
   | Balmora at 23:00 | 0.18 / 0.19 | 0.33 / 0.34 | 0.35 / 0.36 | 0.38 / 0.39, 0.40 / 0.41, 0.40 / 0.41 |
   | Balmora, fog at night | 0.08 / 0.08 | 0.14 / 0.14 | 0.14 / 0.14 | 0.26 / 0.26, 0.26 / 0.26, 0.28 / 0.28 |
   | Balmora by day | 0.37 / 0.37 | 0.88 / 0.88 | 0.96 / 0.97 | 1.07 / 1.17, 0.89 / 0.98, 0.90 / 0.96 |
   | the ship at dawn | 0.47 / 0.47 | 1.22 / 1.24 | 1.27 / 1.29 | 1.12 / 1.14, 1.14 / 1.15, 1.23 / 1.24 |

   No exterior gains anything from the reuse, by night or by day, and by day Balmora's bias rises by
   0.06 to 0.10. **Decided: temporal reuse where the frame's sky lights nothing, and none where it
   does.** At the guild that is 0.77 ms in place of 1.31, for 0.51 of spatiotemporal's 0.75 of
   noise removed; at the ship, 0 ms in place of 1.09, for nothing lost and bias gained back.
4. **The lighter wavelet is taken if it passes the bar in every leg** (P1, decided): the first level
   as now, then three 3×3 levels reading the centre's variance, a reach of 16 pixels against 14.
   Three such levels took the filter from 0.99 to 0.58 ms at the guild. Its still leg is in and
   passes: the guild 0.59 → 0.57, the planter 0.69 → 0.65, the yurt 0.61 → 0.59, the pier and the
   pond 0.01 lower, and the bias 0.01 higher at most. **The moving legs are in and pass too, so it is
   taken.** Its four levels cost 0.72 ms at the guild against 1.02 and 0.99 for the baselines run
   before and after it, and 0.58 ms at the ship against 0.86 and 0.78: 0.28 and 0.24 ms saved.
   Noise now → lighter, then bias:

   | place | strafed in | walked in | bias, strafed / walked |
   |---|---|---|---|
   | mages' guild | 1.24 → 1.22 | 1.32 → 1.30 | +0.00 / +0.00 |
   | guild's planter | 1.42 → 1.38 | 1.42 → 1.39 | +0.01 / +0.01 |
   | Ahemmusa's yurt | 1.75 → 1.74 | 1.94 → 1.92 | +0.01 / +0.00 |
   | Seyda Neen's pier | 1.16 → 1.16 | 1.18 → 1.18 | +0.00 / +0.01 |
   | Seyda Neen's pond | 1.21 → 1.21 | 1.17 → 1.17 | +0.00 / +0.00 |
5. **The far hit gets cheaper lamp sampling, and no radiance cache** (P3, decided). It is half of the
   bounce's trace: 0.51 ms at the guild, 0.86 at the ship, of which the lamp walk is 0.30 ms at
   the guild. Step 19 draws `K` lamps by a per-cell power table at `PATH_INDIRECT` only; the sun
   and ambient rays stay. A radiance cache (SHaRC) would take more of the cost but adds bounces and
   changes the look, and is not planned.

# Results

What each step measured, in the order of work, whether the step was kept or reverted: its
before and after figures, the kernels it moved, the pictures that changed, and why it was kept.
Nothing here is deleted when a step is done.

## Step 15: the first frames after a cut — kept

`noise --cut=N` judges the frame `N` frames after the cut a stop begins with, standing, against
a bar of the `N + 1` frames its history holds (`noiseFrameFor`), and every place's line now counts
its fireflies: pixels whose light, after the tone curve, is four times the reference's and over it
by at least a level-16 grey (`fireflyShare`). `noise --ab --cut=N` adds a leg for each `N`.

The baseline every later step is held to, release, `--suite=bounce`, FSR quality: frame noise
against its bar, bias, and fireflies in a thousand pixels.

| place | `--cut=1` noise / bar | bias | fireflies |
|---|---|---|---|
| mages' guild | 2.19 / 8.45 | 2.35 | 0.38 |
| guild's planter | 1.83 / 14.07 | 2.92 | 0.41 |
| Ahemmusa's yurt | 2.08 / 8.35 | 2.84 | 0.27 |
| Seyda Neen's pier | 2.26 / 25.68 | 1.74 | 0.10 |
| Seyda Neen's pond | 1.87 / 10.59 | 1.19 | 0.12 |

At `--cut=1` under FSR quality the bar is one frame (`2 × 921600 / 2073600` rounds to 0, held to
1), so every frame is far cleaner than its bar: the reconstruction does more than averaging two
frames could. The rooms carry three to four times the exteriors' fireflies.
