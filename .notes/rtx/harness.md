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

- **A sunrise hour renders differently** in `shot` and in the game, so a frame taken at 06:30 is not
  the frame the game draws at 06:30.
- **`MWWorld::MoonModel` (`apps/openmw/mwworld/weather.cpp:363`) and `RtxBridge::makeMoon` are two
  copies of one reverse-engineered arithmetic**, kept in step by nothing but the tests written
  against the port.
- **The harness never runs a transition at all**, so the blend between two weathers — the field the
  shader now carries — is exercised only in the game, which is the surface nobody iterates on.

### 2.5 The harness walks the graph only when the ring moves

The game re-walks its whole scene graph every frame and sweeps every frame, which is what makes mark
and sweep sound (`plan.md` §6, M11). The harness walks in its constructor and on a cell crossing, and
keeps a snapshot of the still world (`PosedActors`) between them.

That is a deliberate trade and it buys the determinism in §1. What it costs is that **anything the
sweep clears stays cleared** until the ring next moves — the lamps were one victim, and the sprites,
the emitters and the deforming set are cleared by the same call. The current fix walks a second time
after the sweep, which is the game's next frame brought forward.

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

### Step 2 — The weather arithmetic into a component

**Move `MWWorld::MoonModel` and the sun's own ramp into `components/weather`, and have
`apps/openmw/mwworld/weather.cpp` delegate to it.**

- *Buys:* §2.4's two copies become one, by construction rather than by test. The harness gets the
  ramp it is missing, so a sunrise renders the same in both. `makeDaylight` shrinks to a caller.
- *Costs:* it modifies game code. Not the rasterizer — the picture the GL path draws is unchanged and
  it is engine arithmetic rather than RTX code — but it is upstream code all the same, and that is a
  judgement to make deliberately.
- *Note:* this is what the earlier objection got wrong. Putting it in `components/` does **not**
  violate "not one line of the ray tracer is compiled with the option off": it is not ray-tracer code.

### Step 3 — Lights as graph nodes

**Attach a `SceneUtil::LightSource` when the harness instances a `LIGH` reference**, the way the game
does, and delete `LoadedCell::mLights` and `placeCellLights` with it.

- *Buys:* one route for lights instead of two; the §2.3 defect cannot recur; carried and moving
  lights become possible.
- *Costs:* the harness has to build the node OpenMW builds. `RtxBridge::makeLight` already holds the
  conversion, so what is needed is the attachment, not the physics.

### Step 4 — Water the way the game has it

**Give the harness a water node with `Mask_Water` rather than an analytic quad**, so the walk finds
it exactly as it finds the game's.

- *Buys:* the sea in `shot` becomes the sea in the game (§2.2), and `waterbuilder`'s hold-and-release
  bookkeeping goes away with it.
- *Costs:* the harness needs the plane's geometry from somewhere. `MWRender::Water` is game code and
  cannot be linked, so either the plane's construction moves to a component or the harness builds an
  equivalent one and the two are pinned by a test.
- *Open:* the game's plane and a per-cell quad answer "how far does the sea go" differently, and the
  harness loads a ring rather than a world. Decide what the harness should show past its ring before
  building this.

### Step 5 — Walk every frame, behind a switch

**Give the harness the option to re-walk and sweep every frame, as the game does.**

- *Buys:* §2.5 closes. The sweep's contract holds without anything being brought forward by hand, and
  the harness exercises the cadence the game actually runs at.
- *Costs:* determinism must survive it — the walk has to be a function of the frame index and not of
  the clock, which is what `getMotion` already exists for. Leave it off by default so `shot` stays
  reproducible, and turn it on for the window and the bench.

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
