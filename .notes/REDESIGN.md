# Structural redesign: proposal and plan

This proposal answers `.notes/REVIEW.md`. It does not repeat the findings. It names the few
structural causes behind most of them, gives the target shape for each cause, and orders the work.
A reference such as *(REVIEW: Frame-to-frame state)* points at a group heading in `REVIEW.md`.

`REVIEW.md` held 432 findings from three reviews (2026-10-01 and 2026-10-02): 8 high, 85 medium,
339 low. After Phase 4, 205 stay open, none of them high. The findings that give a wrong or missing
result for an input the tree can produce are also in `.notes/ISSUES.md`.

This file keeps what is still to do. A finished workstream's section is gone: the code and the
commits hold it, and "Progress" says where the work went another way than planned.

The findings fall into three kinds:

1. **Structure.** Most findings come from fourteen causes in how the code is owned and connected.
   Sections 2 to 15 redesigned those causes, and the open ones remain below. A change of structure
   closes a whole group, and the group's items go with it.
2. **Decisions.** Some findings ask what the fork accepts against upstream. Section 1 records the
   owner's answers and the work each answer leaves, and the question a probe still answers (D8).
3. **Fixes in place.** The rest are local: stale comments, includes, a constant written as a rounded
   decimal, a test that asserts too little. They need no design. Section 17 lists the groups and the
   rule for each.

## Progress

Implemented on the branch `refactor`, one commit per item, each with its test.

- **Phases 0 to 4 are done**, each ended on a clean `./omw gate`. Phase 5 is next. The work stopped
  after Phase 4 at the owner's word.
- **After Phase 4, the owner's rule for upstream code:** a change to an upstream file stays only
  where it fixes a bug or the ray tracer needs it; a change that only makes upstream code tidier is
  reverted, and the copy it removed stands again in fork code. Fourteen such changes went, W10's
  shared rules among them, and `AGENTS.md`'s Accepted diff lists each upstream change that stays.
- **Baselines** are in `~/.cache/omw-refactor/`: the pictures `after-screen` (since the prescaled
  screen basis, see Phase 4), the kernel listing `kernels-phase4.txt`, and the release benches
  `bench-2` to `bench-7`.
- **Phase 1 lows wait for something outside the tree:** BC7 and groundcover (see below), Night-Eye
  (D8), the in-memory particle image (a key for an image no file names, which the material reader
  lacks too), the distant statics' animation, and the post-processing package's nine null tests
  (the player packages are built before the world, so one decision needs the renderer handed to
  the Lua context).

Where the work went another way than the plan:

- **Phase 1.** The coverage rule and W4's one walk were measured and not taken (see "Waiting for
  you"). The harness's hold is asked in ticks of the queue's timestamp period, not a probe timed
  against the timestamps: the probe met stalls of up to 2.6 ms and was off by up to four fifths,
  and both vendors read one counter for the two (RADV's source, NVIDIA's `%globaltimer`). The rain
  shelter is `Precipitation::isOccluded`, not a `WorldState` field. The crash matrix has no mode for
  a fault inside a report (see "Waiting for you"). The ring's reader holds a `ThreadContent`, not a
  `WalkContext`, because it walks no traversal. The host rows of a measured run move between legs
  of one build by up to 40% (in `ISSUES.md`).
- **Phase 2.** `SceneHeld::mTextureCount` stays: it is the one view of the array's length, which a
  GPU test holds against the table. A composite's row has the kinds `GroundAlbedo` and
  `GroundGloss` and names its chunk's material (`TextureRow::mGroundOf`), because the key does not
  give the material back without a parse. The upscaler hands its output from `record`; `getOutput`
  is gone. `ImageFactCache::of` normalises the image's name into a scratch string, because no
  caller holds the file's `NormalizedView`. W5's price has three entries (`priceFile`,
  `priceBake`, `priceComposite`), because a bake's shape is its source's. Measured: W4 took the
  preprocess row's p99 at `island-crossing` from 1.83 to 1.74 ms.
- **Phase 3.** No `FrameStep` and no `mSimulated`: a paused frame hands the wake no impulses and a
  water clock that did not move, which the step reads already. A skin needs no still pose, because
  its pose delta is in object space. `SetPos` and `moveObjectBy` stay steps, because a script moves
  a platform with them a little each frame. Upstream code this adds to, for Phase 6's list:
  `World::moveObject`'s split and `cellForMove`, `World::setGlobal*` with `noteHourWritten`,
  `DateTimeManager::jumps`, and `RenderingManager::notifyJumped`.
- **Phase 4.** D5 answered (§1). The prescaled screen basis rounds where the division did, to the
  same error (2e-7 of the plane in a float32 simulation of both forms), and the denoiser's history
  carries that rounding into 17 of 60 pictures, at most 17 of 255 on 0.6% of Mournhold's pixels;
  with the division put back they matched. The baseline moved to `after-screen` with it. The star
  field's sheet extent is measured in the backend, because only the array knows the side it stands
  at. The sea's gravity and the wave height's ratio left the device header, which no shader read.
  The film's clock is planned in seconds of the game's own clock and turned into hours by the
  session's starting `timescale`, because the plan is made before any world exists. The installation
  options moved to `Files::addInstallationOptions`, because the harness's option library is
  engine-free; with them the harness reads an `openmw.cfg`'s groundcover lines as the game does.
  The test of the keys' hour spelling found the C++ one rounding a float product first, 732 of the
  day's half minutes a minute late; `minuteOfDay` rounds the exact product. The frame-constant
  items of §17 move to Phase 5 with the frame cost.

### Waiting for you

- **W1 step 5: a write of `GameHour` as a cut, refined.** The plan called `notifyCut` on every
  write of the `GameHour` global. A script that holds the hour writes it every frame, and every
  frame would then drop every history. The branch cuts where the write moves the clock by more than
  the frame's own step, either way round the day (`DateTimeManager::jumps`): a held hour moves it
  back one step and does not cut, and `set gamehour to 21` does. A rest, a wait, travel and jail
  were cuts already, through upstream's `notifyWorldSpaceChanged` in a non-incremental
  `advanceTime`. Say if you want every write to cut instead.
- **W7: the exposure meter's dark edge moved interiors brighter.** `EXPOSURE_BLACK` (10^-4) sat
  3.3 stops under the scale's bottom (2^-10), so that band was metered at the first bin's middle,
  up to ten times too bright. One edge now: the scale reaches down to the black edge, at 2^-13
  (within a third of a stop of 10^-4, and exact), so the band is metered as what it is. Nine
  pictures moved, all interiors and night exteriors (`seyda-neen-customs`, `balmora-fog-night`,
  `balmora-storm-night`, `balmora-mages-guild`, `wolverine-hall`, `addamasartus`, `arkngthand`,
  `andrano-tomb`, `mournhold-arrival`), brighter: the guild's mean byte 47 → 65, the tomb's 55 →
  65. The other way to one edge, black raised to 2^-10, ignores the band and darkens the same
  places (the guild 47 → 31.5), which lost the room's detail. Say if you want that one.
- **D5 answered: a half store rounds toward nought on this card** (`RtxHalfStoreTest`). The bounce's
  running mean and the shadow moments are full floats now. The cascade writes every level through
  one declaration, so every level went to full floats: release bench on a quiet card, the filter
  zone about +0.05 ms and the accumulate zone +0.03 to +0.08 ms at the two decks, nothing at the
  guild. Bias against the converged reference fell at all three `noise` places (1.93 → 1.89,
  1.62 → 1.60, 2.17 → 2.13). A split that keeps the later levels in halves (one body, two declared
  targets) took half of that bias back (1.91, 1.61, 2.15), because the later levels' stores round
  toward nought too; its saving could not be measured — another program held the card — so the
  branch keeps every level in full floats. Say if the 0.1 ms matters more than the bias.
- **Coverage (high 5, W14.1): the plan's rule was measured and not taken.** The plan said a blend
  is a pane wherever one texel of its finest level is soft. Every DXT3 leaf, banner, rope and sail
  the game ships is soft at its anti-aliased edge (4-bit alpha, steps of 17), so the rule made them
  all panes: at `seyda-neen-pier` the panes went from 3 to 91, and the sun and sky rays through them
  turned every surface in the picture to grain (52 of 60 pictures moved). The branch takes a rule
  with no threshold that is exact where it acts: a blend with no test whose texture never reaches
  solid is a pane, because such a texture is no mask and a cut drops all of it or keeps all of it.
  That fixes the lantern glass, the glass pots, interior lava and the waterfalls; a mask's soft
  fringe stays a cut. Webs and crystals are panes only if their texture never reaches 255. To make a
  mask's fringe soft as well, the pane path needs to be cheap and clean for foliage first. Your call
  whether that is wanted.
- **W4 (Step 0): one cache, two walks, measured.** The plan said one walk reads the mean and the
  solid reach together. Every blended material asks the reach, and the mean decodes every texel's
  colour, so the one walk put a full decode of each blended texture the frame met on the frame: the
  preprocess row's worst frame went from 0 to 11.3 ms at `seyda-neen-ship-dawn`. The branch keeps
  one per-file cache per thread (`ImageFactCache`), whose entry reads each fact at its first ask:
  the reach by the walk that stops at the first solid texel, the mean only for an additive sheet.
  The worst frame is back at 0.07 ms.
- **The harness's hang report needs the catcher on.** `openmw-rtxtool` defaults
  `OPENMW_DISABLE_CRASH_CATCHER` to `1`, so item 13 reports a hang only where a shell sets it to
  `0` (checked: a run stopped for 30 s wrote a dump and a summary with the version and the
  renderer). Whether the harness keeps the catcher off is in `ISSUES.md`, not decided here.
- **Item 15's vertex alpha darkens the tables under Dunmer candles.** The candles' wax and iron
  cups carry a material alpha of 0.4 under a vertex tint, which the rasterizer does not read: it
  draws them opaque, and so does the trace now. Before, the trace drew them as 40% panes, and the
  candle's lamp shone through its own cup. In `Light_De_Candle_14` the lamp stands 4.3 units over
  the wax, and the cup rises to 4.5 units under the flame with a half-width of 6, so the cup's
  shadow covers the table for about 26 units round the candle's foot. `balmora-mages-guild` moved
  on 2.2% of its pixels, all in that light. The shadow follows from the geometry and the lamp's
  anchor. If the warmer table is wanted back, the lamp's anchor or its clearance is the place to
  change, not the material.
- **Item 15: the cut does not read the dark map's alpha.** `objects.frag` tests `alpha × dark.a`,
  and the plan said one read serves the cut too. Measured: the dark read in `candidateStops` cost
  the dawn deck's trace 3% (2.59 against 2.50 ms, clock-normalised, four legs each), behind a
  material bit or not, because the code sits in every shadow ray's candidate loop. A probe without
  it was level with the base. The hit and a medium's crossing read the dark map and the sheet; the
  cut reads the diffuse alpha alone. Which content the difference moves is not counted yet.
- **Item 20: BC7 waits for a decoder.** The byte formats are widened now. A BC7 colour map uploads
  as it is on every target card, but the host reads a colour texture's texels for its facts (the
  mean, the solid reach) and the contact sheet, and BC7 has no host decoder here. Its partition
  tables are spec data I did not want to write from memory; Mesa's
  `src/mesa/main/texcompress_bptc_tmp.h` (MIT) holds a decoder. Your call whether to take its
  tables (a licence note in `files/licenses/`) or have the facts of a BC7 texture be "unknown".
- **Item 20: groundcover waits for content to check it against.** This install has no groundcover
  plugin (`openmw.cfg` names none), and the design is a new reference kind from
  `GroundcoverStore`, the game's density rule shared, and one merged mesh per cell and plant model
  held by the ring as its ground is — an instance per plant would add about a hundred thousand rows
  to the top level. Built against synthetic tests alone it could be quietly wrong where it matters.
  If you put a groundcover mod (Aesthesia, Remiros') in `~/.config/openmw/openmw.cfg`, I build it
  next and measure it.
- **Item 23: no crash-matrix mode for a fault inside a report.** The plan asked for one. Built, it
  left two dumps in 8 of 8 runs on Linux, and the report's dump was summarised as the fault on the
  reporting thread: Crashpad's Linux client keeps one exception record, which `DumpWithoutCrash` and
  the crash handler both write. A matrix check that passes there would make that defect an
  expectation, and Windows and macOS answer differently, so the rule is held by `readNotes`'s unit
  test instead, and the record in `ISSUES.md`. Serialising a fault behind a report in progress on
  another thread (a first-chance handler that waits on the gate) would give one clean dump each,
  per system; that is a design of its own, not built.
- **W14.4: the water's scattering colour leaves the air's share out.** The plan said the colour and
  the weather fog's share both come from the fallbacks. Built with the share, the colour the
  rasterizer's underwater fog settles at, a clear noon's sea turned a muddy brown-grey over most of
  every coastal picture (`seyda-neen-pond` moved on all its pixels, red up by a fifth): the share is
  the air's, a tint on the rasterizer's picture under water, and read as the water's own albedo it
  quadrupled the red and undid the blue-peaked pairing `look.h` states with `WATER_EXTINCTION`. The
  branch reads `Water_UnderwaterColor` at its weight, so a water mod's colour reaches the trace, and
  nothing moved. If a storm should grey the sea, that is a separate term on the water, not its albedo.
- **The keys' clock reads the session's own `timescale`, untested in a window.** `sky.lua` takes it
  at `onInit` and `onLoad`, where the harness takes it before its first stop. A run of the script
  under stub modules gave the expected steps (a clock at 40 over a base of 10 reads ×4, a step up
  ×8), but no window ran it, because nothing here opens one. Press the clock keys once in a played
  `view` to confirm.

## Contents

0. [Principles the redesign keeps](#0-principles-the-redesign-keeps)
1. [Decisions the owner took](#1-decisions-the-owner-took)
7. [W6 — Scene tables change by the row](#7-w6--scene-tables-change-by-the-row)
9. [W8 — The denoisers share one surface test](#9-w8--the-denoisers-share-one-surface-test)
10. [W9 — Passes run only over what is new](#10-w9--passes-run-only-over-what-is-new)
15. [W14 — The ray tracer shows what the rasterizer shows](#15-w14--the-ray-tracer-shows-what-the-rasterizer-shows)
16. [Smaller workstreams](#16-smaller-workstreams)
17. [Fixes in place](#17-fixes-in-place)
18. [Order of work](#18-order-of-work)
19. [Verification for every phase](#19-verification-for-every-phase)

The section numbers are the plan's first ones, which the commits and the notes cite. W1 to W5, W7
and W10 to W13 are done, and §16.3 and §16.4 with them.

### The open work at a glance

| Workstream | What is left | Phase |
|---|---|---|
| W6 Scene tables change by the row | block growth, running totals, the refit, the presence rows | 5 |
| W8 The denoisers share one surface test | steps 1 and 2; step 3 after its probe | 5 |
| W9 Passes run only over what is new | every row, each with its bench | 5 |
| W14 The ray tracer shows what the rasterizer shows | the lows that wait for something outside the tree | after its inputs |
| §16 Smaller workstreams | the upstream diff, memory and device, layering, Vulkan, tooling, the seam | 5 (§16.6), 6 |
| §17 Fixes in place | the local groups; the frame constants | 5, 7 |

---

## 0. Principles the redesign keeps

These come from `AGENTS.md` and the owner's posture. Each workstream below applies one or more.

- **P1. One source for each truth.** A value that two owners keep is a value that two owners can
  disagree on. The redesign deletes the second copy. It does not add a check that holds two copies
  equal, unless the two copies are in two languages and a shared function is not possible.
- **P2. A record says what happened.** An owner never infers a fact about the frame from a side
  effect (a zeroed camera, a list that a placement did not clear, a stamp on another object). The
  frame tells each owner what happened, once.
- **P3. Ownership follows use.** An owner hands out the object a caller uses. It does not hand out
  one forward per method. A struct that crosses a layer carries what the reader reads and nothing
  more.
- **P4. Work is incremental.** A table changes by the row and grows by the block. No frame pays for
  a whole table because one row crossed a threshold.
- **P5. A measurement does not measure itself.** The harness does its own work outside the frames
  that it measures.
- **P6. Measure before a shader decision.** Each kernel change has a figure before and after it
  (`./omw release bench`, `./omw kernels`), and each picture change names the pictures it moves
  (`./omw shot --against`).
- **P7. A verdict is tested before it judges.** The harness's comparisons and its measuring window
  decide whether every later change passes. They have their own tests first.
- **P8. The rasterizer is the reference for what is drawn and when.** The ray tracer may light a
  thing differently, which is its purpose. It does not drop, freeze, misplace or misread what the game
  asks for. Every fact the loader states is carried or refused by name. Every setting, script call and
  console command the rasterizer honours is honoured, or the seam says it is not.

---

## 1. Decisions the owner took

The owner decided D1 to D4 on 2026-10-01. Each decision keeps a part of the upstream diff, and
`AGENTS.md`, `architecture.md` §1 and `README.md` now record all four. On 2026-10-02 the owner took
the recommended answer to D1's contrast, D6 and D7. What is left is to fix the
defects inside the kept parts, not to revert them. A daily
agent merges upstream into the fork (`.github/workflows/upstream.yml`), so each kept hunk can
conflict on a merge. That cost is accepted.

### D1. The SDL3 port stays. Gamma is back; contrast stays out.

The port touches about 144 upstream files. Its reason is the presentation: `SDL_GetWindowPixelDensity`
and `SDL_GetWindowDisplayScale` give the frame-to-window mapping and the interface scale on a
fractionally scaled Wayland desktop, and SDL2 has no per-window display scale. Commit `37778677fe`
does not state this reason.

The work in this plan:

- Fix the defects the port brought in: the settings migration (an old file with `resolution x/y`
  and no `window width` copies the values once), the launcher's custom size (say what it sets, or
  add the window's size), `androidmain.cpp` (back to upstream's, or out of the fork), and the
  `GraphicsWindowSDL2` rename (back to upstream's name).

**Gamma** came back in `c80cd6eded`: both renderers raise the world's picture to one over
`[Video] gamma` in their last pass over it (the tone pass, `PingPongCanvas`), and `AGENTS.md` records
it. **Contrast** is still removed, and nothing says so *(REVIEW: The SDL3 port … contrast)*.
Decided: `AGENTS.md`'s SDL3 entry names its removal. It had no menu control, upstream applied it only
on Windows, and the tone pass has a contrast grade of its own (`TONE_CONTRAST`).

### D2. The scaled presentation is the fifth accepted change to the rasterizer.

The GL path keeps its frame texture and its blit. `[Video] resolution x/y` is the frame for both
renderers.

The work in this plan:

- Correct the `GlRenderer` class comment (`glrenderer.hpp:83-86`), which says the rasterizer is not
  modified.
- The pingpong, postprocessor and intersector hunks stay.
- Correct the upscale docs (`rtx.rst`, `settings-default.cfg`, the launcher tooltip): `off` traces
  at the frame's size, not the window's.

### D3. The five extra warnings stay on for the whole tree.

The work in this plan:

- Change every `Crash::notNull` on a per-node or per-frame path to the debug-only form. The rules
  require this whatever the scope of the flags: `NodeCallback::run`, the light manager, the terrain
  drawable, the MyGUI batch, the physics check that replaced upstream's `assert`, and the terrain's
  water culling view (`quadtreeworld.cpp:447`, once per chunk per cull).
- The cast hunks, the sol3 patch and the Boost `extern template`s stay.

### D4. The three upstream bug fixes stay in the fork.

No work is left. A fourth joins them (W7): `RenderingManager::getFieldOfView` returned the override
flag, 1°, wherever a field of view was overridden — in werewolf form, to Lua's camera — and the
projection and `describeEye` each spelled the right rule beside it; both call the getter now.

### D5. Answered: a half-float store rounds toward nought on this card

`RtxHalfStoreTest` holds it. `specular.h` was right, and `accumulate.h` reasoned as if the store
rounded to nearest. The bounce's running mean, the shadow moments and every level of the wavelet
cascade are full floats now (see "Waiting for you" for the cost). W8's step 3 can be decided.

### D6. The harness's hooks in upstream classes

`Engine::beforeFrame`, `holdsGameClock`, `WeatherManager::holdWeather` and
`WindowManager::scriptMessageBox` exist only for `openmw-rtxtool`, and Accepted diff lists none of
them *(REVIEW: The layering's documents … harness hooks)*. Decided: the engine's two frame hooks
are the host interface (`OMW::EngineHost`) and go into Accepted diff with that reason. The weather
hold and the script boxes leave upstream's classes: the host sets the weather through
`World::changeWeather` with its own transition, and declines script boxes in its own window-manager
setup.

### D7. A resize keeps the eye's adaptation

Decided and done (Phase 3): the exposure belongs to the eye, not to the extent, so a resize loses
the reprojection and keeps the eye (`FramePast`).

### D8. Open, and a measurement: how Night-Eye reaches the trace

The rasterizer adds Night-Eye's lift to the ambient of every lit fragment: nothing occludes it and
nothing adapts to it. The trace adds it to the fill, which geometry occludes and the exposure meter
mostly cancels *(REVIEW: The trace's light rules …)*. Two shapes keep the effect:
- **A.** Exempt the lift from adaptation, as `DAYLIGHT_GAIN` is: `mExposureBias` takes the ratio of
  the fill with and without the lift, to the power `EXPOSURE_ADAPTATION`. Cheap, exact where the fill
  is the frame's light (a cave), and over-strong where the sun dominates.
- **B.** Add the lift after the meter, in the tone pass, as `nightEye × albedo`, unoccluded and
  unadapted, which is the rasterizer's rule. Exact, and it needs the albedo at the output extent.

Recommended: B where an albedo at the output extent exists, else A. A probe decides: hold the
picture of `seyda-neen-ship` at night and of a cave, at magnitudes 0, 25 and 100, against the
rasterizer's ratio of lifted to unlifted brightness.

---

## 7. W6 — Scene tables change by the row

**Closes:** *(REVIEW: Scene tables redo work for rows that did not change)* and *(Work is batched
behind a threshold …)*. Phase 1 did `SlotSet`'s third state, the instance record written once, and
the composite queue; this is the rest.

### What is wrong

| Where | Defect |
|---|---|
| `GrowableBuffer::outgrow`, `SlotTable::sync` | the frame that crosses a power of two remakes the table and writes every row |
| `SceneAcceleration::prepareRefit` | a posed body is rebuilt only every 64 placements (`sRebuildEvery`), so frames alternate between a rebuild and none |
| `readPlacedStats` | the texture and structure figures are loops, so they are left out of the per-placement stats and go stale; `extendScene` pays a 4096-slot walk on the frame path |
| `SceneBuffers::place` | every medium and additive box is transformed on every placement, standing rows included |

### Target shape

- **Per-frame device tables grow by the block.** `SlotTable`, the skin tables and the sprite bin
  move onto `BlockedBuffer`'s shape: fixed blocks behind one address table. Growth costs one block
  and owes only its new rows. `SlotTable::mRows` reserves by the block too. `GrowableBuffer` stays
  only for start-up and resize paths, and `outgrow` goes.
- **The refit rebuilds the oldest posed mesh on every placement that poses anything.** No threshold.
  The worst frame stays one rebuild, and every frame costs the same.
- **Running totals.** `TextureArray` keeps count, bytes and reduced count, updated in `stand` and
  `drop`. `StructureStorage` keeps its two totals as rooms are taken and given back. One
  `readStats` runs at every placement, and `readPlacedStats` goes.
- **Presence rows in a `SlotTable`**, driven by the same `changed` list as the instance table.

### Verification

`./omw repeat --pairs=10`. `./omw release bench` on a hot card, back to back, with a warm-up leg:
`one-cell-walk` and a crowd view for the movers, `island-crossing` for growth. Report the median,
the p99 and the worst frame before and after. The worst frame must not rise.

---

## 9. W8 — The denoisers share one surface test

**Closes:** *(REVIEW: The denoisers repeat each other's per-pixel work)*, the history-loop item in
*(Shared code is written again …)*, the branch items in *(Kernels step around rules …)*.

### What is wrong

The accumulator, the glossy filter and the shadow denoiser's temporal pass each run
`heldSurfaceMatches` on the same four taps with the same inputs. Each pixel pays the surface and
motion loads and the four tests three times. The bilinear history loop is written four times. The
two surface histories re-encode a G-buffer channel into `RGBA16F` at every pixel, and that
re-encoding is the only reason `mDistanceScale` exists.

### Target shape, in three steps

1. **One function** in `lib/surfacematch.glsl` returns the four corner weights (`vec4`: zero
   outside, unmatched or at zero share) from the four loaded surface texels. Each pass dots them
   with its own history fetches. The function takes texels, not an image, because a
   format-qualified image is a type of its own. *Refactor: no picture moves.*
2. **The accumulator writes the match** into an `R8UI` image (four bits). The shadow denoiser's
   temporal pass reads it in place of `heldSurface`. The glossy filter reads it too. The nearest-tap
   test reads its bit from the same mask. *Refactor: no picture moves. Saves two of three tests per
   pixel.*
3. **Decide the surface history after D5 and a measurement.** The proposal in REVIEW keeps two of
   each surface channel in the G-buffer (this frame's and last frame's) and binds last frame's as
   the history. That deletes two full-frame writes, two image pairs and `mDistanceScale`.
   `accumulate.h` argues for a format of the pass's own. Both arguments are about precision. Write
   a probe that compares the match rate of both encodings over `one-cell-walk`, and choose by it.

The merge of the glossy filter into the accumulator (one dispatch) is not in this plan:
`DenoisePasses::record` records the shadow denoiser between them. Step 2 takes most of the gain
without changing the order.

The five passes that branch per pixel on "has a surface" either select (as `specular.comp` and
`pane.comp` do) or name the measurement where the branch lands.

### Verification

`./omw noise` (the frame's noise and its bias against a converged reference), `./omw noise
--strafe=150` and `--walk=150`, `./omw shot --against`, `./omw release bench`. Steps 1 and 2 must
not move a denoised picture beyond `sDenoiserNoiseLevels`.

---

## 10. W9 — Passes run only over what is new

**Closes:** *(REVIEW: Passes run over memory or tiles that hold nothing new)*.

| Pass | Change | Expected gain |
|---|---|---|
| ocean transform (`waveform`, `waveline` ×6, `wavecompose`) | two dispatches and one barrier: the row pass computes the spectrum of its row for all three pairs in shared memory and transforms; the column pass transforms and stores into the tiles | about 36 MiB less traffic a frame, four queue drains fewer; the 128 cascade stops running 256 threads with 64 working |
| `spriteruns.comp` | count non-empty tiles of the workgroup into shared memory before the first barrier; return uniformly at zero | scales with occupied tiles, not all tiles |
| `shadowfilter.comp` | `APRON = 1 << SHADOW_LEVEL` | 61% and 44% fewer loads at levels 0 and 1 |
| `shadowtiles.comp` | the mask pass writes a receiver word per tile; the classification reads words | two full-frame reads off the cleared tiles |
| skinning and morphing | measure first: one dispatch over a table of rows in place of one per mesh | hundreds of tiny dispatches become one, if the command processor shows the cost |
| histogram on the bloom's first halving; last wavelet level into the composite | measure first | up to two frame-sized round trips |
| `spriterects.comp` | a sprite the shelter zeroed gets `noTiles()` | sheltered rain costs nothing after the shelter launch |
| the medium and additive walks | built by W14.5's `describeView`: they run only where the view casts against their classes; additive placements keep their class bit | a map tile stops paying three traversals a pixel (and stops drawing absent actors' spell sheets) |
| the upscaler's clears | one `Barriers` into `TRANSFER_DST` for all of them, one back merged into `between()` | two barrier commands a frame instead of up to eleven |
| `barrierBeforeBuild` | deleted; its comment moves to `recordRefit`'s `barrierAfterBuild` | one drain fewer per moving frame |

Each row has its own commit and its own `bench` figure. A change that shows no gain on a hot card
does not go in.

---

## 15. W14 — The ray tracer shows what the rasterizer shows

**Closes:** what is left of the eleven parity groups: *(Content the rasterizer draws reaches the ray
tracer with no reader)* and Night-Eye in *(The trace's light rules are its own …)*. The rest was
done in Phase 1. Each item here waits for an input outside the tree ("Waiting for you").

- **Groundcover is one merged mesh per cell**, as the rasterizer's chunks merge it: the ring's
  reader builds it from `GroundcoverStore` with `Groundcover`'s own `DensityCalculator` (moved to be
  shared), under `rendering distance` and `density`. One instance per cell keeps the top-level
  structure's row count what it is today, where an instance per plant would multiply it. Stomp is a
  later deformer. Waits for a groundcover plugin to check it against.
- **BC7 and the other formats a host must read.** Every BC format uploads as it is; the host reads a
  colour texture's texels for its facts and the contact sheet, and has no BC7 decoder. Waits for the
  owner's word on Mesa's tables.
- **Night-Eye** follows D8's probe.
- **A generated particle image** enters the table under a key made from its address, as the
  composites do; the material reader needs the same key.
- **A distant static's texture animation**: a prepared part whose chain carries an AutoPlay
  state-set updater gets one animated material per model, applied once a frame through
  `MaterialResolver::animate`.

Each carries a test with hand-computed values, on the GPU where the fact is a picture, and a
`./omw shot --against` that moves the pictures of its own content and nothing else.

---

## 16. Smaller workstreams

### 16.1 The upstream diff, as decided

§1 holds the work each decision leaves:

1. **Debug-only checks on hot paths.** Each `Crash::notNull` on a per-node or per-frame path
   becomes the debug-only form.
2. **The port's defects:** the settings migration, the launcher's custom size, `androidmain.cpp`,
   and the `GraphicsWindowSDL2` rename.
3. **The comments and docs that the decisions make false:** the `GlRenderer` class comment, the
   upscale docs, `rtx.rst`'s "untouched" and its `-DOPENMW_RTX`.
4. **D1 and D6, as decided:** `AGENTS.md`'s SDL3 entry names the removal of contrast; the engine's
   two frame hooks go into Accepted diff, and the weather hold and the script boxes move into the host.
5. **The post-processing package tests the renderer once.** `initPostprocessingPackage` registers
   upstream's usertype where there is a chain and an inert one where there is none, so upstream's
   bindings come back unchanged and the nine null tests go.

### 16.2 Device requirements

*(REVIEW: The device check and the enabled extensions differ …)*. The memory half was done in
Phase 1.

- A subgroup obstacle in `profileOf`: quad operations in the compute stage.
- `Device` enables an option's extensions only after its feature holds, in table order.
- `rayTracingMaintenance1` leaves the requirements, or names its reader.
- **The push-constant limit is a requirement.** One `sPushConstantBytes = 256` beside
  `sApiVersion`; `profileOf` refuses a device below it by name, and `pushRangeOf` asserts every
  constants block against it (the tone pass pushes 176 bytes, the sprite bin 184).

### 16.5 Layering guards

*(REVIEW: The layering's documents and guards lag the code)*

`RtxSourceTreeTest` reads the folder order from `architecture.md`'s table, so the order has one
statement. Move `shaders/` to the top of that table. The test refuses a quoted include with a `..`
component, and refuses `<apps/...>` under `components/rtx` and `components/rtxvulkan`. Then fix the
51 quoted `"../"` includes in the fork's `mwrender` files, the two in test support, and the
`#ifdef _WIN32` in `glrenderer.cpp`. The core's prose names no Vulkan object either (swapchains,
descriptor sets, command buffers, `VkInstance`): it states each cost in its own terms, and the
backend's comment carries the Vulkan reason.

### 16.6 Vulkan lifetimes and barriers

*(REVIEW: Vulkan objects are waited on or rebuilt by a side effect …)*

A slot waits on the timeline value its own submit returned (`Submission`), never on a stamp of an
object its record may not name. Staging blocks retire through `Retiring<std::size_t>` like every
other retired object. The presenter compares against the extent it was last asked for. The two
GUI `finish` calls before a present-mode change go, or state the reason that remains. A trace
pipeline reads each named module once per build and chains the words (`maintenance5`), and the GUI
pass's four pipelines load their two modules once.

The present target states its resting use once (`PresentTarget::sResting`), and every user
transitions from it and back; the five literals go. `Barriers` asserts in debug that nothing is
pending when it goes out of scope.

### 16.7 Tooling single sources

*(REVIEW: The driver, CI and CMake restate facts that each other hold)*

The presets are the one source for Qt flavours and dependency roots (`$env{OMW_VCPKG_ROOT}`,
`$env{OMW_QT_ROOT}`). CMake writes the shader and digest tool locations into the cache, and
`kernels` reads them. The digest tool lists `SpecId`s, and the Python SPIR-V reader goes. One
`bench_line` helper serves `repeat` and `profile`. `gate` calls the build verb's body. In CI: one
ccache input on `openmw-deps`, one reusable `package.yml`, one format check (`omw format --check`
in `checks`), `GTEST_FAIL_IF_NO_TEST_SELECTED` in the test preset, `.python-version`. The driver's usage formats
the harness's verbs from `HARNESS_VERBS`. The benchmarks carry a `benchmark` label that both CI jobs
run. One Vulkan headers pin serves all three systems. The release checks for a successful CI run on
its commit. The harness's resources live under one directory that the install leaves out.

**Systems other than Linux.** `SYSTEM` comes from `sys.platform`, and `bootstrap` and `build` refuse
macOS in one line that names the macOS route (`CI/before_script.macos.sh`); the user folders come
from the harness's `info`, through `Files`. `cmake/Tests.cmake` runs each test in
`$<TARGET_FILE_DIR>`, and the crash matrix writes under the build tree. `Platform::Process::setEnvironment`
takes a path and widens it on Windows.

### 16.8 The seam answers each question once

*(REVIEW: The seam promises what only one renderer does)*: W12 takes the thumbnail and the debug
lines, W14.0's declaration takes the settings window and the post-processing package's question,
and W1 takes the time-skip cut and the strikes.

---

## 17. Fixes in place

These groups need no design. Fix each item where it stands, in the commit that touches the file, or
in one sweep per group.

| REVIEW group | Rule |
|---|---|
| Test-only code ships in the game | `MipChain`'s builder and `OwnedTexture` move to test support beside `SpriteLightBake`; `SpecularAlbedo::at` and the `describeImage` overload go |
| Docs and comments state what the code does not do | correct or delete each one |
| Frame constants are derived on every lane … | the host writes each into `VisibilityConstants` once |
| Kernels step around rules … | `RTX_STORE_COUNTED` for `puffs` and `backdrop`; one path or a named measurement for each branch |
| Parameters and fields carry nothing … | delete the dead fields and parameters; `PathSeeds` for the three seeds |
| Arrival frames allocate or churn … | one refusal per model per cell; report a refusal when the count moves; one owner per path string |
| Canaries that no report reads | the stop report prints the `NodeKinds` overflow and `mWornBeyondKept` |
| Includes that name nothing they use | delete them |
| Smaller defects | each as written; `shadow.h`'s bound becomes `< 32`, or the mask is built as `~0u >> (32u - width)` |
| Where an image was left … ; Barriers that order nothing … | §16.6 |
| Options a verb takes and does nothing with | an `sPictures` verb set owns the picture-only knobs; every contradictory pair is refused at parse time, and `--accumulate` with an upscaler is refused |

**Tests** follow each workstream. The groups *(Tests that cannot fail …)*, *(Behaviour with no
test)* and *(Test fixtures are duplicated …)* close as each owner is rewritten, with three that
stand alone and go first because they cost every run of the suite:

- the blue-noise spectrum test: a separable transform with a twiddle table, about a thousandth of
  the work;
- the unsettled ring test: a `ContentSource` that blocks until released, and exact counts per walk;
- the renderer and `FrameClock` wall-clock bounds: an injected clock (`FrameRateLimiter::limit`
  already takes `now`);
- `RtxMonitorTest`'s quarter-second sleep: count the predicate's calls and release the turn after
  the first;
- `RtxInstanceTest`, 15.9 s under ThreadSanitizer: it moves to `rtx-gpu-tests`, or runs on the
  harness's unvalidated instance;
- the card watch's test: assert on the closed window's readings, not on the time between two calls.

The second review adds tests where behaviour has none: `RenderManager`'s batching over a
`CountingRenderer`; allocation legs for `drawGui`, `traceGuiTexture`, `collectDrawCalls`, the
present, the game side's frame and a second `CellReader::read`; `HeldSubmit`'s opener thread adopted
so its validation errors are reported. `compareRuns`, `judgeNoise` and `MeasureWindow` are Phase 0.

---

## 18. Order of work

Each phase ends green on `./omw gate`. Night-Eye waits on a measurement (D8).

```
Phase 0  verdicts, baselines ─► Phase 1  defects, by severity ─► Phase 2  ownership
    ─► Phase 3  frame record ─► Phase 4  contracts ─► Phase 5  frame cost
    ─► Phase 6  upstream diff ─► Phase 7  tests and docs
```

### Phases 0 to 4

Done: the verdicts and the baselines, the defects by severity, ownership, the frame record and the
device contracts. "Progress" says where each went another way than planned.

### Phase 5 — Frame cost

W6 (block-growth tables, running totals, the refit), W8 steps 1 and 2, W9, the barrier rows of
§16.6, and §17's frame constants. Each with a `./omw release bench` before and after. A change that
does not improve the worst frame or the p99 does not go in. W8 step 3 follows its own
measurement.

### Phase 6 — The upstream diff

§16.1, then §16.2's rest, §16.5, §16.7 and §16.8.

### Phase 7 — Tests and docs

The test groups in §17, and every doc item, `architecture.md` §1 and §13 included.

---

## 19. Verification for every phase

- Build the touched targets and run the covering test binary with a filter
  (`./omw test <binary> --gtest_filter=...`).
- `./omw test` once before a phase is called done. `rtx-gpu-tests` fails without a device, so a
  green run means a device ran it.
- A change to anything a frame reads: `./omw repeat --pairs=10`.
- A shader change: `./omw kernels --against=<before>`, then `./omw shot --against=<baseline>`. Time a
  suite on its second run, because the first includes the driver's compile.
- A frame-cost change: `./omw release bench`, hot card, back to back, warm-up leg first, started in
  the background. Quote the median, the p99 and the worst frame.
- A denoiser change: `./omw noise`, with `--strafe=150` and `--walk=150`.
- The end of each phase: `./omw gate`, alone, with no build beside it.
- `REVIEW.md`: delete each item as it closes. A workstream is done when its groups are empty.
