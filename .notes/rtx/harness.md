# The harness against the game

`openmw-rtxtool` and `openmw` drive the same renderer over the same content and are supposed to
produce the same picture. Where they do not, `shot` stops predicting the game — and `shot` is the
surface nearly all iteration happens on (`CLAUDE.md`, *Verification*), so a divergence there is not a
cosmetic difference between two tools. It is the harness quietly lying about what the game will look
like.

This file is what the two do differently, why, and which of those to close. `plan.md` is the route
for the renderer; this is the route for the thing that checks it.

---

## 1. What must not converge

The harness is worth having because of four properties, and every proposal below is measured against
them before anything else.

- **It starts in under a second warm** and needs no GL context, so it runs over ssh and inside a test
  binary under the validation layers.
- **It has no `MWBase::Environment`** — no script VM, no physics, no sound, no save games.
- **It is deterministic.** A `shot` renders the same pixels twice: the sea is still, the frame index
  is zero, the world is at an hour it was told rather than one it measured.
- **It layers below the game.** `apps/openmw` depends on `openmw-rtx-bridge`; the harness must never
  become something the bridge needs, or the dependency inverts.

**Linking the harness against `openmw-lib` is therefore not on the table.** It would buy every
derivation below for free and cost all four properties at once. What *is* on the table is moving the
pure parts — arithmetic over records and settings, with no world to ask — down into components both
sides read.

## 2. Where they differ

### 2.1 The frame was described twice — closed

Both sides now fill one `RtxBridge::FrameWorld` and call one `applyWorld`
(`components/rtxbridge/frameworld.hpp`). What each does to *reach* those numbers still differs and
always will — the game decodes what a live weather system settled on, the harness derives them from
the content files at an hour it was told — but the list of what a frame's world half contains, and
the writing of it, are one thing.

**`WorldState` could not itself be that struct, and the reason is worth keeping.** Its colours are
display-encoded, because `MWRender::PostProcessor` reads the same struct and OpenMW's own pipeline
works in that space end to end. The ray tracer decodes; the rasterizer must not. So `WorldState`
stays the game's faithful report in the world's own numbers, and `FrameWorld` is the renderer's
units — linear colours, an extinction rather than two distances, the weather blend already turned
the right way round, the moons placed.

Three defects of exactly this shape had already happened by the time it was closed, and the last was
found while closing it: the sea's clock filled by the harness and left at zero by the game, so waves
stood still in the game alone; the weather, the wind and both moons added twice in one sitting; and
**`mFogUniform` written only by the harness, so every interior in the game ran the outdoor banked-fog
field** a room is far too small for.

### 2.2 Water is different geometry in the two

The game has no analytic water at all. `MWRender::Water` builds a plane, and the mirror recognises it
during the walk by its node mask — `isWater(drawable.getNodeMask())`,
`components/rtxbridge/sceneextractor.cpp:724` — and gives it `MaterialKind::Water`. The harness
synthesises a quad per cell straight into the scene (`components/rtxbridge/waterbuilder.cpp`), which
no walk can find, which is why it has to be held against the sweep and released by hand when a cell
leaves.

The two are different extents, different tessellation and a different shoreline. Anything measured
against water in the harness — caustics, the glitter path, Fresnel at a grazing angle, the shore's
own depth ramp — is measured against a surface the game does not have.

### 2.3 Lights arrive by different routes

The game's lights are `SceneUtil::LightSource` nodes in the graph, met by the walk and re-placed
every frame — which is how a torch in someone's hand works at all. The harness reads `LIGH` records
and pushes them into the scene directly.

That difference produced a real defect: `SceneDesc::release` empties the light table on the frames a
cell departs, on the sound understanding that the walk which comes next refills it. True of the game.
False of a lamp that came out of a record, and false in a harness that only walks when the ring
moves — so every lamp went out on the first crossing and stayed out. It is fixed by hand
(`StagedWorld::placeCellLights`), and the fix is a second bookkeeping path that exists only because
the first one is not the game's.

It also means **the harness cannot show a carried or a moving light at all**, since only a record has
a position it can read.

### 2.4 The sky and the moons are re-derived, not reported

`MWWorld::Weather` runs a real simulation: ten weathers, a transition counted down between two of
them, per-hour ramps through sunrise and sunset, moons on their own clock. The harness has no weather
system, so `RtxBridge::makeDaylight` re-derives the sun and the sky from the `Weather_*` fallback
keys, and `RtxBridge::makeMoon` re-derives the moons from the `Moons_*` ones.

Both are honest about what they are — `makeDaylight`'s own comment says it steps between the four
phases where the game ramps — but the consequences compound:

- **The harness never runs a transition at all**, so the blend between two weathers — the field the
  shader now carries — is exercised only in the game, which is the surface nobody iterates on. This
  is what is left of §2.4; the sun's ramp and the moons' clock are shared now (step 2), so an hour
  renders the same in both and there is one copy of each arithmetic.

### 2.5 The harness walks the graph only when the ring moves — closed

The game re-walks its whole scene graph every frame and sweeps every frame, which is what makes mark
and sweep sound (`plan.md` §6, M11). The harness used to walk in its constructor and on a cell
crossing, keeping a snapshot of the still world between them — and **anything the sweep cleared
stayed cleared** until the ring next moved. The lamps were one victim; the sprites, the emitters and
the deforming set are cleared by the same call and were others waiting to happen.

It walks every frame now (step 5). Determinism did not have to be traded for it: a walk cannot change
a world that did not change, and the frames prove it byte for byte.

### 2.6 Time does not pass

The game's `DateTimeManager` advances the clock, so weather transitions run, moons cross the sky and
`mGameHour` moves. The harness is at whichever hour it was told; the window can now step it by hand,
but nothing advances on its own and nothing ever transitions.

### 2.7 The loader reads a subset of the records

`EsmLoader::Query` is opt-in by design — it skips record types nobody asked for. That is not a fault,
but it means each new thing the renderer needs is a new flag and a new field, discovered when
something turns out to be missing. Regions were the most recent: nothing had asked for them, so
`nextRegionWeather` had nowhere to read a weather chance from until `mLoadRegions` was added.

### 2.8 What the harness does not place at all

Object paging and groundcover. The game has `ObjectPaging` and a `Groundcover` root; the harness
pages **terrain** only (`World::pageTerrain`). Distant land is therefore a different scene in the two,
which is worth knowing before anything in `plan.md` §8's *Distant land* is measured here.

### 2.9 Actors are a posed row, not the game's people

`PosedActors` stands a row of creatures and NPCs in front of the camera and steps their animation by
a clock. That exercises skinning, rigs and the deforming path. It does not exercise what the game's
mechanics actually ask for — an idle chosen by AI, a weapon drawn mid-swing, an NPC turning to face
the player — so a pose bug that only the game produces cannot be reproduced in `shot`.

## 3. The plan

Ordered by what each buys against what it costs. Every step stands alone and leaves the tree working.

### Step 1 — One frame description, written once — **done**

`RtxBridge::FrameWorld` and `applyWorld`, filled by `RtxRenderer::renderFrame` on one side and
`RtxTool::applyLighting` on the other, with `RtxFrameWorldTest` asserting that every number the world
decides reaches the frame, that the camera's half is left alone, and that an unfilled world is a
frame with no sky in it.

It also finished the moons: the game reports the two `MoonState`s it was already given
(`WorldState::mMoons`) and `RtxBridge::placeMoon` turns them into the same placements the harness
derives, so both surfaces draw the same moons from one piece of geometry code.

### Step 2 — The weather arithmetic into a component — **done**

`components/sky/` holds a moon's clock (`moonmodel`) and the day's own ramp (`timeofday`), and both
renderers ask it. `MWWorld::MoonModel` is an adapter that turns a `Sky::MoonMoment` into the
`MWRender::MoonState` the sky was already being handed; `RtxBridge::makeMoon` asks the same clock
directly, because the harness has no weather system to go through.

**`makeDaylight` reads the engine's four-point ramp now**, not whichever of four phases an hour fell
in. Each quantity crosses dawn over a window of its own — the sun can be up before the sky has
finished turning — so the step was wrong for most of sunrise and most of dusk, and it was wrong in
the direction of jumping. Two things went with it: the four-phase colour read, and the harness's rule
that the sun is off at night. **The engine does not switch its sun off** — `calculateResult` takes
the colour straight off the ramp and its night value is a dim blue — so the harness had been lighting
its nights differently from the game it exists to predict.

**The component is `Sky` and not `Weather`, and the reason is a trap worth remembering.**
`MWWorld::Weather` is a class, so inside `MWWorld` an unqualified `Weather::` finds *it* rather than
a namespace of that name, and the error message says only that `MoonModel` does not name a type.

- *Note:* an earlier objection to this step was wrong. Putting engine arithmetic in `components/`
  does **not** violate "not one line of the ray tracer is compiled with the option off" — it is not
  ray-tracer code, and the picture the rasterizer draws is unchanged.

### Step 3 — Lights as graph nodes — **done**

`readObjects` calls `SceneUtil::addLight` on the reference's own transform, exactly as the game does,
and `LoadedCell::mLights`, `CellReport::mLights`, `StagedWorld::placeCellLights` and
`PosedActors::mLit` are all gone. The §2.3 defect cannot recur, because there is no second
bookkeeping path left to forget.

**It also honours `AttachLight`**, which the record path could not: a chandelier's light now sits at
its candles rather than at the reference's origin, and the shadows say so.

Three things this turned up, in the order they bit:

- **A light-carrying reference that is also a live prop returned before the attach**, so every
  candle, lamp and torch in the game lost its light. Attaching before the prop test fixes half of it.
- **The other half is `setEmpty`.** `SceneUtil::addLight` marks a source with no geometry beneath it
  empty so the game can skip a light on something invisible, and a prop's transform carries the light
  alone — the census office reported twenty-three light references and nought lights. A prop's model
  is not missing, it is being instanced somewhere it can run its flame, so the source says it is not
  empty.
- **`StagedWorld::mirror` never emptied the per-frame lists before walking**, which
  `RtxRenderer::renderFrame` has always done. Appending rather than replacing did not show while the
  lights were read from records and placed once; the moment they became nodes a walk meets, every
  light in the region counted twice per walk.

### Step 4 — Water the way the game has it — **done**

`RtxTool::WaterPlane` builds the sheet the game builds — `SceneUtil::createWaterGeometry` at a
hundred and fifty cells across, forty segments, `Mask_Water` — and moves it to whichever cell is
being looked at, exactly as `Water::changeCell` does. The extractor is told the same mask the game
tells it, so the walk finds the sea by itself.

**The open question answered itself.** The game's water is one sheet a hundred and fifty cells
across, so it reaches the horizon whatever is loaded and the ring never enters into it. The geometry
builder was already a component; nothing had to move.

`components/rtxbridge/waterbuilder` is deleted, and with it `LoadedCell::mWater`, the instance
bookkeeping, and `SceneExtractor::hold`/`unhold` — the escape hatch for geometry placed outside a
walk, which now has nothing to hold. Everything a cell brings is in the graph.

**It also found a test that had been passing for the wrong reason.**
`RtxUpscalerStabilityTest` never walked the graph: it called `loadRegion` and asserted the scene was
not empty, which was true only because the analytic quad went in directly. The temporal resolve was
being measured against **one flat quad and nothing else**, under a comment saying the bound had been
"calibrated against exactly this much content". It walks now, and passes against the cell.

### Step 5 — Walk every frame — **done, and not behind a switch**

The harness walks and sweeps the whole graph every frame, always. It reuses the seam that was already
there: `Motion` is what a run asks for when the scene changes between frames, and
`StagedWorld::EveryFrame` is one that walks whether or not anything moved. Where there are actors
nothing changes — their own stepping already walks the whole graph — so this is exactly the still
world, which is the case the snapshot was hiding.

**It was written as a switch and the switch was wrong.** The argument for one was cost, and there is
none: a `shot` takes **1.85 seconds with the walk and 1.84 without**, and a still frame comes out
byte-identical either way. What an option actually bought was a harness that *normally* ran at a
cadence the game never does — so the tool's own default was the divergence this file exists to close,
and the two paths would both have needed testing for ever.

**The rule it settles, which is worth stating once:** the harness is not a thing to optimise. It
exists to behave as the game behaves, and where the two differ, the harness moves. A cost that buys
divergence is not a saving.

### Step 6 — A clock

**Let the harness advance its own hour**, so a transition between two weathers can be looked at and
the moons can be watched crossing.

- *Buys:* §2.6, and the first exercise the weather blend has ever had outside the game.
- *Costs:* small once step 2 lands, since the arithmetic will be shared. Off by default, for the same
  reason as step 5.

### Later, and only when something needs them

- **Object paging and groundcover** (§2.8) — required before *Distant land* in `plan.md` §8 can be
  measured in the harness at all.
- **Actors as the game poses them** (§2.9) — the largest of these and the one that most nearly
  requires the game, since what an NPC is doing is a mechanics answer. Likely never fully closed; the
  posed row is the deliberate substitute.

## 4. What this does not change

The `verify` corpus is a comparison of the harness against **itself** at an earlier commit, and stays
that way — it is a function of the driver and the card as much as of the code (`plan.md` §7.2).
Nothing here proposes comparing `shot` against a frame captured from the game automatically; the two
being the same picture is what the steps above are for, and a person looking at both is how it gets
checked until they are.
