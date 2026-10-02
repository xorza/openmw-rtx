# Structural redesign: proposal and plan

This proposal answers `.notes/REVIEW.md`. It does not repeat the findings. It names the few
structural causes behind most of them, gives the target shape for each cause, and orders the work.
A reference such as *(REVIEW: Frame-to-frame state)* points at a group heading in `REVIEW.md`.

`REVIEW.md` holds 432 findings: 8 high, 85 medium, 339 low. The first review (2026-10-01) wrote 302.
The second (2026-10-02) found none of them fixed, rewrote 22, and added 84. The third (2026-10-02)
compared the ray tracer with the rasterizer over 352 capabilities and added 46 (W14). The 59
findings that give a wrong or missing picture, or a wrong result, for an input the tree can produce
are also listed in `.notes/ISSUES.md`.

The second review changed this plan in four places: two new workstreams (W12, W13), new rows in W1,
W2, W3, W7, W9, W10 and W11, two new decisions (D6, D7), and a Phase 0 that tests the harness's own
verdicts before anything is judged by them. The third review added W14, its principle (P8), and
its defects to Phase 1. A re-review of the plan then took out what it planned twice or decided by a
guess: W1's losses are one table carried in the frame's record, coverage follows the content's own
flags with no threshold, the settings and modes a renderer declines are one declaration with a
parity test, a view's mask is translated in one place, Night-Eye waits on a probe (D8), and Phase 1
is one list ordered by severity.

The findings fall into three kinds:

1. **Structure.** Most findings come from fourteen causes in how the code is owned and connected.
   Sections 2 to 15 redesign those causes. A change of structure closes a whole group, and the
   group's items go with it.
2. **Decisions.** Some findings ask what the fork accepts against upstream. Section 1 records the
   owner's answers and the work each answer leaves, and the two questions a probe answers (D5, D8).
3. **Fixes in place.** The rest are local: stale comments, includes, a constant written as a rounded
   decimal, a test that asserts too little. They need no design. Section 17 lists the groups and the
   rule for each.

## Progress

Implemented on the branch `refactor`, one commit per item. This section says where the work stands.

- **Phase 0:** the verdict tests (`compareRuns`, `judgeNoise`, `MeasureWindow`, `repeat`'s status)
  are in. The baselines are in `~/.cache/omw-refactor/` (pictures from `1e4c4cd92a`, the kernel
  listing, a release build of the base in `~/.cache/omw-refactor/base`). The bench baseline is taken
  back to back with the branch at the end of Phase 1, because a bench beside a build reads wrong.
  The D5 probe is still to run.
- **Phase 1, high:** all eight are done. Step 0 keeps one cache of image facts, but not one walk
  (see below). 5 is done by another rule than the plan's (see below). A release bench against the
  base after the eight (`~/.cache/omw-refactor/bench-2/`) matches it within noise at the median,
  the p99 and the worst frame of all three places.
- **Phase 1, medium:** 9 to 15 are done. 9's thumbnail traces no fresh frame where the world is
  hidden, which stays in `REVIEW.md` as a low item. 13's hang limit came with the start function
  both binaries call (`OpenMW::startLogAndSettings`); the rest of W10's lift from `parseOptions`
  stays in Phase 4. 15 is four commits; its cut reads no dark map (see below). A release bench
  against the base after 15 (`~/.cache/omw-refactor/bench-3/`) matches it at the median, and the
  deck's trace zone is level with the base's.
- **Phase 1, medium 16:** the bolt's lamp, the flicker in the game's band and the darkening lamps
  are done. The darkening costs about 1% of the guild's trace (a probe without it was within noise
  of the base). Night-Eye waits for the D8 probe, which needs a harness hook that gives the player
  the effect; it comes after the other mediums.
- **Phase 1, medium 17:** done — the player hidden by the view mask, a script's view distance,
  and map tiles that wait for their ground.
- **Phase 1, medium 18:** done — the deck turns about the eye and the sea runs on the rasterizer's
  wind, the moon's phase runs continuously, and the sky's sheets are keyed by name.
- **Now:** Phase 1, medium item 19, W14.6's rest.

### Waiting for you

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
- **Someone ran `git pull --rebase origin` on `refactor`** while the work ran, after two early
  commits (`a297255ebe`, `3de0c852f7`) reached `origin/refactor`. I left them and worked forward:
  `da36abeaa0` moves the test the first one put in the wrong binary.

## Contents

0. [Principles the redesign keeps](#0-principles-the-redesign-keeps)
1. [Decisions the owner took](#1-decisions-the-owner-took)
2. [W1 — The frame says what happened](#2-w1--the-frame-says-what-happened)
3. [W2 — One walk context, one route to the ring](#3-w2--one-walk-context-one-route-to-the-ring)
4. [W3 — A value has one owner across the layers](#4-w3--a-value-has-one-owner-across-the-layers)
5. [W4 — One cache of image facts](#5-w4--one-cache-of-image-facts)
6. [W5 — One price for an arriving texture](#6-w5--one-price-for-an-arriving-texture)
7. [W6 — Scene tables change by the row](#7-w6--scene-tables-change-by-the-row)
8. [W7 — Device contracts live in one function](#8-w7--device-contracts-live-in-one-function)
9. [W8 — The denoisers share one surface test](#9-w8--the-denoisers-share-one-surface-test)
10. [W9 — Passes run only over what is new](#10-w9--passes-run-only-over-what-is-new)
11. [W10 — Rules the game owns are called, not copied](#11-w10--rules-the-game-owns-are-called-not-copied)
12. [W11 — A measurement does not measure itself, and says what it measured](#12-w11--a-measurement-does-not-measure-itself-and-says-what-it-measured)
13. [W12 — The picture and the interface are two images](#13-w12--the-picture-and-the-interface-are-two-images)
14. [W13 — One rule for every number and setting the harness reads](#14-w13--one-rule-for-every-number-and-setting-the-harness-reads)
15. [W14 — The ray tracer shows what the rasterizer shows](#15-w14--the-ray-tracer-shows-what-the-rasterizer-shows)
16. [Smaller workstreams](#16-smaller-workstreams)
17. [Fixes in place](#17-fixes-in-place)
18. [Order of work](#18-order-of-work)
19. [Verification for every phase](#19-verification-for-every-phase)

### The plan at a glance

| Workstream | Closes in `REVIEW.md` (items) | High items | Kind | Phase |
|---|---|---|---|---|
| W1 The frame says what happened | *Frame-to-frame state …* (8, less the one W12 takes), two more | the ripple burst | correction | 1, 3 |
| W2 One walk context, one route to the ring | *The game side reaches its owners …* (10), *The ring's ask …* (2) | — | refactor, correction | 1, 2 |
| W3 A value has one owner | *A value crosses a layer …* (15), *Types allow states …* (7) | — | refactor | 2 |
| W4 One cache of image facts | *Facts about one image …* (6) | — | refactor | 1 (step 0), 2 |
| W5 One price for an arriving texture | *The price of an arriving texture …* (3) | — | correction | 2 |
| W6 Scene tables change by the row | *Scene tables …* (6), *Work is batched …* (2) | — | cost | 1, 5 |
| W7 Device contracts in one function | *One fact has two names …* (12), *Shared structs …* (8), *Shared code …* (20) | — | refactor, correction | 4 |
| W8 The denoisers share one surface test | *The denoisers repeat …* (4) | — | cost | 5 |
| W9 Passes run only over what is new | *Passes run over memory …* (6) | — | cost | 5 |
| W10 Rules the game owns are called | *Rules that the game or upstream owns …* (10), *Platform facts …* (5) | — | refactor | 4 |
| W11 A measurement says what it measured | *Measurements and reports …* (15), *A run that stops early …* (3) | — | correction | 0, 1 |
| W12 The picture and the interface, two images | three items | — | correction | 1 |
| W13 One rule for the harness's numbers | *The harness reads numbers …* (2), *A harness option …* (1) | `--day` | correction | 1 |
| W14 The ray tracer shows what the rasterizer shows | eleven parity groups (44), two more | coverage, picking, the glow, the sky's ramp | correction | 1 |
| §16 Smaller workstreams | the upstream diff, memory, the crash monitor, the gates, layering, Vulkan, tooling | essential memory priority | mixed | 1, 6 |
| §17 Fixes in place | the local groups | the contact sheet | local | with their files, 7 |

A group that a workstream closes only in part is named in its section. The eight high items order
the head of Phase 1.

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
  add the window's size), `androidmain.cpp` (back to upstream's, or out of the fork), the
  `GraphicsWindowSDL2` rename (back to upstream's name), and the truncation in
  `setWindowRectangleImplementation` (call `SDLUtil::windowPoints`).

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

No work is left.

### D5. Still open, and a measurement: how this device rounds a half-float store

`specular.h` says this device rounds half-float stores toward zero. `accumulate.h` reasons as if it
rounds to nearest. One of the two is wrong, and three histories depend on the answer
*(REVIEW: One fact has two names …)*. Run a probe kernel once (the place is
`components/rtxvulkan/shaders/probes/`). §9 (W8) waits for the result.

### D6. The harness's hooks in upstream classes

`Engine::beforeFrame`, `holdsGameClock`, `WeatherManager::holdWeather` and
`WindowManager::scriptMessageBox` exist only for `openmw-rtxtool`, and Accepted diff lists none of
them *(REVIEW: The layering's documents … harness hooks)*. Decided: the engine's two frame hooks
are the host interface (`OMW::EngineHost`) and go into Accepted diff with that reason. The weather
hold and the script boxes leave upstream's classes: the host sets the weather through
`World::changeWeather` with its own transition, and declines script boxes in its own window-manager
setup.

### D7. A resize keeps the eye's adaptation

`createTargets` resets the camera's past, so every resize and upscale-mode change eases the exposure
and the glare share from nothing, which is a visible snap. The comment at `vulkanrenderer.cpp:111-113`
says a resize keeps them. Decided: it keeps them. The exposure belongs to the eye, not to the
extent. W1's `FramePast` then carries two facts: the reprojection is lost (resize, new world, cut),
and the eye's history is lost (new world, cut).

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

## 2. W1 — The frame says what happened

**Closes:** *(REVIEW: Frame-to-frame state is not told which frames really happened)* whole group;
the water-clock item in *(A value crosses a layer …)*; the ripple-reset item in the same group.

### What is wrong

Each owner of a history decides for itself what the last frame was. Each answer comes from a side
effect:

| Owner | What it infers | From what | Defect |
|---|---|---|---|
| `RipplePass` | the footfalls of this frame | the list the last placement copied | a paused frame presses the last footfalls again, up to 128 times |
| `DenoiseHistory` | whether its history is last frame's | whether it ran last frame | a frame with no denoiser leaves a history two frames old |
| `GuiDrawer` | what it draws over | `mTarget` as it stands | an untraced frame draws the interface over the last interface |
| `VulkanRenderer` | whether the past is lost | `mPreviousCamera` zeroed, or `resetHistory` | two routes reset different things |
| `RtxRenderer` | which loss drops the ripples | every cut | a door inside one worldspace drops the wake |
| `RipplePass` | the water's time | `joinSeconds` of a split float pair | the tick is read off a rounded number |
| `DisplayChain` | whether the eye's history is lost | `basisLost` and `mExposureStale`, two routes | a new world or a resize spends the reset on a held exposure, and a resize snaps the exposure (D7) |
| the seam | which jumps are cuts | `notifyCut` from a teleport or a worldspace change only | a time skip (`set gamehour`, a rest) carries the old light through every history |
| `RippleEmitters` | which strikes happened this frame | `mStrikes`, emptied only by `update` | strikes under `tws` pile up and land in one step |

### Target shape

**The frame record says what happened, once.** The game tells the renderer an event through the
seam as it does today (`notifyCut`, `notifyWorldspaceChanged`). `RtxRenderer` keeps it as a pending
loss and hands it to the next frame inside the frame's record, so the backend learns every loss by
one route: the record of the frame it is about to trace. Nothing resets a history out of band, and
the backend's `resetHistory` goes; a test states a loss in the record it traces with.

**In the core** (`components/rtx/frame/`, beside `FrameOptions`):

```cpp
/// What the host knows happened between the last traced frame and this one.
struct FrameStep
{
    /// The simulation advanced. False on a paused frame: nothing moved and nothing was pressed.
    bool mSimulated = true;

    /// The host's water clock, in seconds, as a double. The shaders keep their split pair; the
    /// backend steps the ripples on this value.
    double mWaterSeconds = 0.0;

    /// What the eye's past is worth: `None`, `Cut` (a teleport inside one worldspace, a clock jump)
    /// or `Worldspace` (another worldspace).
    HistoryLoss mLoss = HistoryLoss::None;

    /// The references the game put somewhere else in one step this frame (`World::moveObject`'s
    /// teleporting paths), each placed still: no motion, so its old place's history is refused.
    std::span<const ObjectJump> mJumped;
};
```

**One table decides what each event costs each history.** It lives beside `FramePast` in the
backend, and each owner reads its own column:

| Event | Told by | Reprojection (trace, upscaler, denoiser) | The eye (exposure, glare) | The water's ripples |
|---|---|---|---|---|
| a frame the denoiser does not run | the backend | the denoiser turns with no filter, so every filter is fresh at its next run | kept | kept |
| a resize, an upscale-mode change | the backend (`createTargets`) | lost | kept (D7) | kept |
| a teleport inside a worldspace, a clock jump | `FrameStep::mLoss = Cut` | lost | lost | kept |
| another worldspace | `FrameStep::mLoss = Worldspace` | lost | lost | lost |
| a new world handed over | the backend (`setScene` of the world) | lost | lost | lost |
| one object's jump | `FrameStep::mJumped` | that object's pixels (a still placement) | kept | kept |
| a paused frame | `FrameStep::mSimulated = false` | kept | kept | not stepped, no impulse pressed |

**In the backend** (`VulkanRenderer`), `renderFrame` folds the record and its own events into one
`FramePast { mReprojectionLost; mEyeLost; mWaterLost; mSimulated; mWaterSeconds; }` and hands it to
`TraceChain::record`, `Upscaler::record`, `DisplayChain::record` and `TraceMedia::stepRipples`. The
zeroed `mPreviousCamera`, `basisLost`, `mAirStale` and `resetHistory`'s second pair of flags go.
`DisplayChain` keeps only its deferral (`mExposureStale`: a reset spent by the next frame that
measures).

**Four game-side rules feed the record:**
- An object's jump is listed where the game makes it: `World::moveObject`'s teleporting paths (as
  opposed to physics stepping) add the reference to the `SceneFrame` the world describes, beside the
  eye, and `RtxRenderer` hands the list on as `mJumped`. A new seam member is not needed.
- A clock jump is a cut: `advanceTime(hours, incremental = false)` and a write of the `GameHour`
  global, which upstream already routes through `DateTimeManager`, call `notifyCut`.
- The ripples consume their impulses: `TraceMedia::stepRipples` moves the kept impulses into
  `RipplePass`'s pending list and clears its own copy. `RippleEmitters::update` runs on every frame
  and yields an empty list on a paused frame, and its hidden-world branch discards `mStrikes`.
- The interface never draws over itself: W12 owns this, because the save thumbnails need the
  picture without the interface too.

### Steps

1. Add `FrameStep` with `HistoryLoss`, and the table's `FramePast`. `RtxRenderer` turns
   `notifyCut` and `notifyWorldspaceChanged` into the pending loss. Delete `resetHistory`'s backend
   member, the ripple reset on the cut path, and `joinSeconds`.
2. Move each owner to its column of `FramePast`.
3. Turn the denoiser on unfiltered frames. Delete the pending-reset deferral that this replaces.
4. Make the ripple impulses a consumed list; discard the strikes of a hidden world.
5. Call `notifyCut` on a clock jump. Add `ObjectJump` and the still placement
   (`PlacementTable::place` sets `mPrevious` and both pose copies to the new pose).

### Tests

- `RipplePass`: two records at one tick with one footfall each press one footfall. A record with
  `mSimulated = false` presses nothing.
- `TraceChain`: filtered, unfiltered, filtered gives the same last frame as a fresh reset.
- `RtxRenderer` (openmw-tests): a cut keeps the ripple field and a worldspace change drops it.
- `VulkanRenderer`: a resize keeps a measured exposure; `setScene` followed by a held frame and a
  measured frame eases the measured one from nothing.
- `World` (openmw-tests): `set gamehour` and a rest each reach `notifyCut` once; `SetPos` on an
  actor in view lists one jump.
- The table itself: one test per row, which asserts the three columns.

### Risk

Low. The record replaces flags that exist now. The denoiser change can move a picture after a
toggle of the denoiser, which is the defect it fixes.

---

## 3. W2 — One walk context, one route to the ring

**Closes:** *(REVIEW: The game side reaches its owners through pass-through layers)* whole group.

### What is wrong

The frame thread's walk needs three things that `WorldMirror` owns: `Traversals`, `ThreadContent`
and `SpecularLayout`. They travel apart through three layers. `SceneExtractor` takes two of them as
nullable pointers that fall back to private copies, which is a silent wrong answer for any
production caller that forgets one. `SpecularLayout` has five holders, each defaulted to `Ignore`.
The ring's reader thread spells `ThreadContent` by hand.

The cell ring's interface is exposed again at two layers above it: eight one-line forwards on
`WorldMirror` and three more on `RtxRenderer`. `FrameContext` copies four of the mirror's values
and borrows the whole game renderer, which adds six public members for the harness only.

### Target shape

**One context per thread that walks content** (`components/rtx/mirror/walkcontext.hpp`):

```cpp
/// What every walk on one thread shares: where its traversal numbers come from, what it computes
/// from the content, and what the content's `_spec` maps mean. One per thread, owned by what owns
/// the thread's walks.
struct WalkContext
{
    Traversals mTraversals;
    ThreadContent mContent;
    const SpecularLayout mSpecular;
};
```

- `WorldMirror` owns one `WalkContext` for the frame thread. `CellRing`'s reader owns one for its
  thread (it replaces `TemplateWalk::mContent` and `mMeans`). Both are made with the same
  `SpecularLayout`, which `MirrorKnobs` carries once.
- `SceneExtractor(SceneDesc&, WalkContext&)`. No default. `mOwnTraversals`, `mOwnContent`,
  `getPreprocessor` and the resolver's and the placer's own `SpecularLayout` members go. The
  resolvers read `context.mSpecular`.
- `ViewRequest` holds `WalkContext&`. `TracedView` takes `WorldMirror&` (or the context alone) in
  place of three parameters. `createWorldView` and `createSubjectView` share one body.
- The test fixture owns one `WalkContext`, which is what production does.

**The ring's request is a function of its inputs, not of a flag.** Today `mAskStale` is set at
seven scattered sites, and two inputs set it nowhere: a reach that grows under a still eye asks for
no new cell, and indoors `getCellsToStand` keeps the last exterior shortfall because `mBandCells` is
written only in `ask` *(REVIEW: The ring's ask is refreshed by a flag …)*. Target: the ring keeps
the last request's inputs (eye cell, band, statics, held and handed counts) beside `mLastEye`, and
rebuilds the request when any differs. `walkRings` sets `mBandCells` to nought on both early returns.
Tests: grow the reach between two walks at one eye and assert the outer ring arrives; walk an
exterior band short, then an interior, and assert nought cells to stand.

**One route to the ring.** `WorldMirror::getRing()` returns `Rtx::CellRing&`. The eleven forwards
go. `TracedGround` holds `CellRing&`. `CellRing::collect(std::size_t frame)` replaces `setFrame`
plus `collect`. `extractWorld` asserts in debug that the ring adopts through this extractor.

**A narrow harness context:**

```cpp
struct FrameContext
{
    RtxRenderer& mRenderer;          // drawViews and getViews only: they need the phase machine
    const WorldMirror& mMirror;      // the scene, the reach, the eye, the grid, the ring
    Rtx::Renderer& mBackend;         // the profile, the reads
    Resource::ResourceSystem& mResources;
};
```

`getBackend`, `getContentMemory`, `isStanding`, `collectStanding`, `collectGateVerdicts` and
`getProfile` leave `RtxRenderer`. The copied `mScene`, `mReach`, `mEye` and `mGrid` go.

### Steps

1. Add `WalkContext`. Make `WorldMirror` and the reader own one each. Make the extractor take it by
   reference. Rewrite the tests to own one.
2. Delete the `SpecularLayout` copies and setters.
3. Add `getRing()`, delete the forwards, move `TracedGround` to the ring.
4. Replace `FrameContext`. Move the harness reads to the new members.

### Verification

This is a pure refactor. `./omw repeat --pairs=10` agrees, and `./omw shot --against` moves no
picture.

---

## 4. W3 — A value has one owner across the layers

**Closes:** *(REVIEW: A value crosses a layer and comes back, or has two owners)*,
*(Types allow states or spellings that the domain does not have)*.

Each item is small, but they share one rule: a value has one owner, and a type admits only the
states that exist. The target for each:

| Today | Target |
|---|---|
| `RunSetup` restates `RendererOptions`' profile, validation and memory budget | `Rtx::RunProfile { RenderProfile; ValidationOptions; optional<uint64_t> mMemoryBudget; }` held by both, assigned whole |
| `RunSetup::mStep`, read only by the harness | moves into the harness's request; the frame clock stays the one source |
| `FrameReport` copies two fields of `SceneUpload` | holds `Rtx::SceneUpload` |
| `renderFrame` returns the `Reconstruction`, and `FrameResult` carries it again | `renderFrame` returns nothing; readers take it from the result of the frame it describes |
| `RtxRenderer::mRendering` calls back up for the projection | `RenderingManager` keeps the frame size it built the projection for and rebuilds when `getPresentation().mFrame` differs |
| `"raytrace"` / `"opengl"` strings, parsed back by `createRenderer` | `enum class RendererKind` with a `NamedEnum` for the log line |
| the frames-in-flight limit: prose in the core, `sFrameSlots` in the backend, a literal `4` in the harness | `Rtx::sFramesInFlight` in `renderer.hpp`; the backend and the harness derive from it |
| `std::array<uint64_t, 2>` spelled a dozen times in the harness | `Rtx::DigestWords` |
| `TraceRecording` carries a whole `VisibilityConstants` and a whole `Reconstruction` | carries the bin's view of the camera (origin, camera, sun) and `bool mDenoised` |
| the sprite tables chosen in three places | one `SpriteTables` value built after `take`, copied into the frame block and into `TraceResult` |
| `PerSlot<SpriteBin>` gives the picture chain two bins | `TraceChain` takes its bin count: frames in flight for the world, one for pictures |
| two resize checks that disagree | `TraceChain::resize` returns early on an unchanged extent; `DenoiseHistory::resize` loses its check |
| `ExposureRule { optional<float> mFixed; bool mHeld; }` with an assert | `std::variant<Measured, Fixed, Held>` |
| `ReconstructionRequest::mNoise` optional whose empty means the default | `NoiseSource mNoise = BlueNoiseTile` |
| `SceneHeld::mBuilt` beside `mIdentity` | `mIdentity != 0` is "built" |
| `sRefusedKinds`, `sSurfaceMapCount` = last value + 1 | a `Count` enumerator, checked by `NamedEnum` |
| `MoonFaces`, `MoonSizes`: named fields picked by ternary | `std::array<MoonFace, 2>` indexed by `Moon` |
| `StopSky` holds weathers as strings | `std::optional<std::uint32_t>` and `std::vector<std::uint32_t>`, filled once by the parsers; names only where text is printed |
| `BlockFile` accepts a repeated field | refuses it, as it refuses a repeated section |
| `GuiRenderer` holds the trace half that only `OffscreenTrace` uses, and `OffscreenTrace` holds the whole `Renderer` | `traceGuiTexture`, `takeGuiCopy`, `finishGuiTraces` and `GuiTraceOptions` move to `Renderer`; `guirenderer.hpp` drops `visibility.h` |
| a chunk composite's source is asked of `CompositeQueue::find`, which forgets it a frame later, though the slot's key names it | the row says what it is (`TextureKind::Composite`, `Gloss`), and `SceneTextures` describes every bake from its row; `find`, `mFinished` and `releaseFinished` go |
| `Display::mShownFrom` is the caller's guess at where the trace chain or the upscaler left the image | `TraceResult` and `Upscaler::getOutput` hand the image with its use |
| `RenderProfile::mRadianceWidth` is fixed for the run and passed through three layers at every resize | `TraceChain` takes it at construction; `resize` and `grow` take an extent |
| `SceneHeld::mTextureCount` filled by the backend, read only by tests | goes with `mBuilt`: `SceneHeld { mIdentity, mStructureRevision }` |
| the 128-bit hash state spelled three ways in the core and again in the harness | one `Rtx::HashState`; `DigestWords`, `ContentKey`, `digestShaders` and the harness use it |
| the harness's `mLeast == 2` and "the ring is sized for two" beside `sFrameSlots` | the overlap check and the film ring derive from `Rtx::sFramesInFlight` too |
| `NightDayModes::sEvery = 0xf` and a hand list of modes | a `Count` enumerator; `sEvery = (1u << Count) - 1` |
| `Material::mAnimated`, read only by the harness's digest | deleted |
| `ui.screenSize()` reads the window manager's laid-out copy, the camera bindings read the presentation | `ui.screenSize()` reads `Renderer::getPresentation().mFrame`; the copy stays private to the layout's change test |

**Verification.** Pure refactors except the `GuiRenderer` move, which is an interface change with
the same calls. `./omw repeat` and `./omw shot --against` show nothing moved.

---

## 5. W4 — One cache of image facts

**Closes:** *(REVIEW: Facts about one image are computed and cached by several owners)*; the
format and layout items in *(Shared code is written again …)*.

### What is wrong

Four facts about one image file have four owners with four lifetimes:

| Fact | Owner today | Lifetime |
|---|---|---|
| the mean texel | `MeanTexels`, keyed by file, per thread | the thread's owner |
| whether alpha ever reaches solid | `HeldTexture::mSolid` on the frame thread; nothing on the reader | one frame (swept), or none |
| the `TextureFormat` | `readFormat` at add, at drop and at describe | none |
| the laid byte count | `laidBytes` and `describeLevels`, two derivations | none |

The sky calls `ContentPreprocessor::meanTexel` straight and skips the cache. `ContentPreprocessor::run`
also hashes its whole input to make a key for a cache that holds nothing, so an arrival frame reads
each new mesh and texture twice.

### Target shape

```cpp
/// What a file's texels say, read in one walk over its finest level the first time the file is
/// met on this thread, and kept for the thread's life. A file's texels never change.
struct ImageFacts
{
    MeanTexel mMean;
    bool mReachesSolid = false;
};

class ImageFactCache   // replaces MeanTexels
{
public:
    explicit ImageFactCache(ContentPreprocessor& content);
    const ImageFacts& of(const osg::Image& image, VFS::Path::NormalizedView file);
};
```

- One walk computes both facts. `HeldTexture::mSolid` goes, and `HeldTexture` keeps a pointer to
  the facts. The reader's `MaterialResolver::read` asks the same cache.
- `of` takes the `NormalizedView` the caller already made, and finds with the transparent hash.
  It builds the key only on insert, so a hit allocates nothing.
- The sky and the night sky ask the cache.
- `TextureRow` gains `mFormat`, read once in `TextureTable::add`. `FormatCensus` takes
  `(TextureFormat, bool mipped)` and no longer reads an `osg::Image`.
- One function lays out the kept levels (offsets and total), and `SceneTextures` reserves and
  describes by it.
- **The content key costs nothing while the cache holds nothing.** `ContentCache` states
  `static constexpr bool sHolds = false`, and `run` skips the digest under `if constexpr`. Each pass
  keeps its `digest` and its test, so a real store only flips the flag.

### Verification

`./omw repeat`, `./omw shot --against`, and a `bench` of `one-cell-walk` and `island-crossing`
for the arrival frames: the `preprocess` row and the worst frame must fall, and nothing else may move.

---

## 6. W5 — One price for an arriving texture

**Closes:** *(REVIEW: The price of an arriving texture is computed apart from what is made)*.

`chooseSide` prices each file with `costAt`, which adds a fixed 2 KB for a companion. A normal map's
companion is a spread map of half its side plus a transient `RGBA32F` means image: about 23 MB for
a 2048² map. A bake of a completed-chain source is priced at one level. `Texture::standFile` knows
the true figures, so two formulas answer one question.

**Target:** one pure function that both the choice and the made texture read.

```cpp
struct TextureCost
{
    VkDeviceSize mImage = 0;      // the chain at this side
    VkDeviceSize mCompanion = 0;  // shading map, or spread map, by getCompanion()
    VkDeviceSize mTransient = 0;  // what the batch holds until it is made: the spread means, the upload
    VkDeviceSize total() const { return mImage + mCompanion + mTransient; }
};

TextureCost priceAt(const TextureData& texture, std::uint32_t side);
```

`costAt`, `bakeOf`, `Texture::mBytes` and `standFile` read it. `sShadingBytes`, `spreadBytes` and
`chainBytes` become its parts.

**Test:** extend `anArrivalIsHeldToTheLargestSideItFitsTheRoomAt` with a `TextureEncoding::Normal`
ladder and a bake of a one-level source the device completes, with hand-computed bytes.

---

## 7. W6 — Scene tables change by the row

**Closes:** *(REVIEW: Scene tables redo work for rows that did not change)*,
*(Work is batched behind a threshold …)*, the composite-queue item in
*(A queue or cache loses what it must keep …)*.

### What is wrong

| Where | Defect |
|---|---|
| `updateInstanceRecords` | a slot that moves on two frames in a row is in `getSettled` and `getMoved`; its record and both device rows are written twice, every frame, for every walking actor |
| `SlotSet` | `add` after `remove` and before `compact` lists a slot twice, so every caller compacts after each remove: one pass over the list per removed slot |
| `GrowableBuffer::outgrow`, `SlotTable::sync` | the frame that crosses a power of two remakes the table and writes every row |
| `SceneAcceleration::prepareRefit` | a posed body is rebuilt only every 64 placements (`sRebuildEvery`), so frames alternate between a rebuild and none |
| `readPlacedStats` | the texture and structure figures are loops, so they are left out of the per-placement stats and go stale; `extendScene` pays a 4096-slot walk on the frame path |
| `SceneBuffers::place` | every medium and additive box is transformed on every placement, standing rows included |
| `CompositeQueue::take` | one frame with a full texture table pops and drops every waiting chunk, and none asks again |
| `CompositeQueue::gather` | a linear scan of the ring for each written chunk |

### Target shape

- **`SlotSet` with three states per slot**: absent, listed, removed-but-listed. `add` on a removed
  slot restores it without a push. Callers stop compacting after each remove and compact once per
  sweep, or before the read.
- **`PlacementTable::isMoved(slot)`**, from `SlotSet::has`. The settled loop in
  `updateInstanceRecords` skips a moved slot. `changed` names each slot once. The test expects no
  repeat.
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
- **`CompositeQueue`** follows `RefusedTakes`: on a refusal it puts the ask back at the front,
  records `getFreedCount()`, and ends the frame's take. It does not take again until the freed
  count moves. A per-material ring position (`sNoIndex` where none waits) makes `gather` O(1).

### Verification

`./omw repeat --pairs=10`. `./omw release bench` on a hot card, back to back, with a warm-up leg:
`one-cell-walk` and a crowd view for the movers, `island-crossing` for growth. Report the median,
the p99 and the worst frame before and after. The worst frame must not rise.

---

## 8. W7 — Device contracts live in one function

**Closes:** *(REVIEW: One fact has two names, and nothing holds them equal)*,
*(Shared structs restate each other …)*, most of *(Shared code is written again …)*.

### The rule

- **A computation that C++ and GLSL both perform is one `RTX_SHADER` function** in
  `components/rtx/shaders/`. A test may hold a host oracle against the device, but the oracle is
  not a second copy of the formula.
- **A format that the host creates and a shader declares is one macro.** Where two names must stay
  equal, one is defined as the other.
- **An edge has one number.** Where two constants describe one edge, one derives from the other.
- **A struct that repeats another struct's fields embeds it.**

### The concrete changes

| Today | Target |
|---|---|
| `EXPOSURE_BLACK` (2^-13.3) and `MIN_LOG_LUMINANCE` (2^-10) both claim the meter's dark edge; the band between them meters ten times too bright | one derives from the other; the test asserts that just under the scale is black |
| `ATROUS_CHANNEL` declares the image that is created as `ACCUMULATE_COLOUR` | one macro for the image the cascade writes into |
| the specular-albedo bilinear blend in `specularalbedo.cpp` and in `gloss.glsl` | one `RTX_SHADER` function in `brdf.h` over the four node values |
| the light grid's flat index in `lightgrid.cpp` and in `lights.glsl` | `lightGridCell(uvec3, uvec3)` in `shaders/scene.h` |
| a lamp's source floor written in `falloff` and in `falloffAlong` | one constant |
| the eye pair `Camera mCamera; Camera mArms;` in five structs | `struct Eyes { Camera mWorld; Camera mArms; }`; `eyeOfPixel(packed, eyes)` |
| `SpecularConstants` restates `HistoryConstants` | embeds it |
| `Camera` restates `Basis` | `Basis mBasis` at its head; `basisOf` goes |
| two octahedral pack and unpack pairs | one `octahedralCode` and `octahedralFromCode` in `octahedral.h` |
| the wave tile's texel layout in two compose kernels | one `waveTileTexels` in `wave.h` |
| the ground stack's sum in `groundcomposite.comp` and `traversal.glsl` | one `GroundSum` in `ground.glsl` |
| `spriterects.comp` maps to the screen by its own formula | `screenOf`, with the per-frame values computed once on the host |
| padding by three conventions; three structs with no size check | one rule (assert the end offset); every shared struct has its `static_assert` |
| `HIT_RECORD_LAYERS` in `scene.h` with the old offset rule | in `visibility.h` beside `HIT_RECORD_EYES` |
| probe kernels number bindings and result slots by literal | named in `probe.h` and `pinning.h` |
| constants that are rounded decimals of closed forms | written as their derivation (`vec2(5, 12) / 13`, `2.0f * PI / 180.0f`); the water's colour is not a constant at all but the content's (W14.4) |
| three constants that fold a transcendental on both compilers | one rule stated in `portable.h`: literal plus a test, or allowed because no host value must match |
| `screenOf` divides by the bases' squared lengths on every call, and five callers finish `rayAt`'s inverse their own way | the host hands each basis prescaled; one `pixelOfScreen(Screen, extent, jitter)` beside `rayAt` |
| `starField` asks `textureSize` at every starry pixel | `StarField` carries the sheet's extent |
| the eye pair now has a third spelling: two identical `eyeAt` wrappers | goes with `Eyes` above |
| four padding conventions for push blocks | one rule in `hosttypes.h`: assert `offsetof(last) + sizeof(last)`, no padding members; `NormalSpreadConstants::mPadding` goes |
| the light animation's golden-ratio constants as decimals | derived from `std::numbers::phi` |

**Half-float rounding** waits for D5. The result decides between two moves: put the bounce history
and the shadow moments in full floats, or correct `specular.h`.

### Verification

`./omw kernels > before.txt` first, then `--against=before.txt` after each step. A refactor names no
moved kernel tuple, or names one whose pictures `./omw shot --against` shows did not move. The
exposure edge and the format macro are corrections: they move pictures, and the commit says which.

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

## 11. W10 — Rules the game owns are called, not copied

**Closes:** *(REVIEW: Rules that the game or upstream owns are restated by the fork …)*, the platform
items in *(Platform facts live in the crash catcher …)*.

The fork already has the pattern: `components/sky/*` and `ripplerules.hpp` hold rules that both
renderers call. Extend it to every remaining copy.

| Rule | New home | Callers |
|---|---|---|
| the engine's installation reading (data dirs, archives, content and groundcover files, fallback) | one function lifted from `apps/openmw/main.cpp`'s `parseOptions` | `main.cpp` and `apps/rtxtool/hosted.cpp` |
| the ripple simulation's grid, texel, rate and two dampings | `RipplesSurface` exposes them; `ripple.h` restates them with a test that holds them equal, because the GLSL header cannot include the game | the trace and the rasterizer |
| gravity (`WATER_GRAVITY`) | deleted from `scene.h`; the environment spells `GravityConst * UnitsPerMeter` once | the wave spectrum |
| a light's minimum radius and its animation flags | `lightutil.hpp` | `createLightSource` and `lightbuilder` |
| which child a day-night mode shows | one function beside `Constants::NightDayLabel` | `DayNightCallback` and `TemplateWalk` |
| a reference's rotation | `makeOsgQuat(const osg::Vec3f&)` in `misc/convert.hpp` | `cellreader` and `objectpaging` |
| the sea's centre and geometry | one small header | `Water` and `WorldMirror::standSea` |
| the sun-glare fader's colour and strength, the disc's sunrise and sunset alpha included | `Sky::SunGlareFader` in `components/sky` | `SunGlareCallback` and `SkyReader` |
| `sWeatherCount` | `= ESM::Weather::Length` | — |
| the game's starting `timescale` | read from the world's globals at session start | the harness and its Lua |
| the log stamp format | one `Debug` function | the game's log and the crash monitor |
| the executable path, local time, the UTF-8 command line | `Platform::Process` (posix and win32 pair) | the crash catcher and `components/files` |
| pixel-to-point conversion | `SDLUtil::windowPoints` | `sdlgraphicswindow.cpp` too |
| the field of view while it is overridden (upstream's getter returns the flag) | `RenderingManager::getFieldOfView`, corrected, and recorded beside the other upstream fixes | `describeEye`, `updateProjectionMatrix`, the Lua camera |
| which display the game is on | the window's display, one `SDLUtil` answer | the sensor manager and the input filter |
| the user data folder | the configured one, handed to the crash catcher by `setupLogging` | the saves, the log and the crash reports |
| the engine's crash-catcher setup (`setHangLimit`, the version) | inside the function lifted from `parseOptions` above | the game and the harness, whose hangs are then reported |

A rule that the GLSL headers must restate (they cannot include game code) gets a test in
`openmw-tests` that holds the two equal.

---

## 12. W11 — A measurement does not measure itself, and says what it measured

**Closes:** *(REVIEW: Measurements and reports state something other than what was measured)*;
*(A run that stops early, or compares less than it was asked to, still reads as a pass)*; the file
and writer items in *(Files and folders hold what another owner owns)*; the harness items in
*(Behaviour with no test)*.

### Target shape

- **The card watch opens its window on its own thread.** The main thread publishes "a window
  begins now" as an atomic generation. The worker resets its clock and tally on its next turn and
  stamps the window. `nameProcess` runs only for a pid that is new and not this process. Perf's fifo
  opens at `Measurer` construction, outside the measured frames.
- **The record carries its premise.** `asJson(GpuClock)` writes `meanMhz` and `readings`.
  `GpuClock` holds the throttle mask and the temperature as `std::optional`, and AMD's reader
  answers "unknown", which the report prints and the JSON writes as `null`.
- **The record states its premises.** The session builds one header at construction from the
  request's setup and flags and a build constant (`!NDEBUG`): the flavour, the layers, the hold,
  hashing and pictures, the reconstruction knobs, the memory budget, the reach, the gamma, the step.
  The printed report opens with it, and the JSON writes it whole. A verb that does not measure prints
  its frame-time table under a "not measured" label, or not at all.
- **The card watch watches the renderer's card.** The renderer hands its device's PCI address
  (`VK_EXT_pci_bus_info`) or UUID on the first frame, and the watch opens that device in NVML or in
  sysfs. Where none matches, the line says "not watched". The memory clock is kept like the core
  clock: a sum, a lowest and a highest.
- **Window bookkeeping runs only in a played run.** `StandingNote::take` and `HomeKey::answer` run
  where `mRequest.mPlayed`. A measured run takes the note once at each stop's end.
- **A comparison states its coverage.** `--against` reports every reference view this run did not
  draw. `abandon` closes the record for the stops it reached, and `compareRuns` judges only those
  stops' pictures. The harness gives "differed" its own exit status, which `omw repeat` reads in place
  of the report's sentences, and `repeat` refuses a pair count under one.
- **The measuring window is a tested type.** `MeasureWindow`, in the library and free of the world,
  takes `isWhole`, `paused`, `cellsToStand`, the frame's spans and the cell, and answers when a stop
  measures, when its wait fails and which span a frame time closes. `Measurer` keeps the instruments
  and calls it.
- **Output files are written after `Engine::go` returns**, from `SessionResult`. A failure is noted
  in the report and sets the exit status, as every other writer does.
- **One asynchronous picture writer**, owned by the session: the film, `--pictures` and Home use
  it. `PerfControl` and `GpuBreakdown` get their own files.
- `PerfControl::enable` retries `ENXIO` until a stated deadline, so `profile --offcpu` needs no sleep.
- The memory report labels a heap by both flags (`device+host`, `device-only`, `system`).

### Verification

A bench of one view twice, before and after, back to back, started in the background on a quiet
desktop. The first measured frame of each stop must lose its spike. The JSON diff shows the header
and the two clock fields. `MeasureWindow`, `compareRuns` and `judgeNoise` have table tests (Phase 0).

---

## 13. W12 — The picture and the interface are two images

**Closes:** the untraced-frame item in *(Frame-to-frame state …)*; *(The seam promises what only one
renderer does)*: the thumbnail and the debug-line gamma items.

### What is wrong

The tone pass writes the picture into the present target, and `GuiDrawer` blends the interface into
the same image (`LOAD_OP_LOAD`). So the picture without the interface exists nowhere after the GUI
draws:

- an untraced frame, and every nested GUI frame (a blocking message box), blends the interface over
  the last interface, and a translucent widget compounds its alpha every frame;
- `capture` reads the interface into every save thumbnail, against the seam's "the frame without
  the GUI", and maps the whole frame onto 518×266 by nearest sample, at the wrong aspect;
- `freezeFrame` freezes the last interface with the picture.

The debug lines are drawn after the tone pass's gamma, and the rasterizer raises them with the world.

### Target shape

- `DisplayChain` writes the picture into `mPicture`, an RGBA8 image at the output extent, which only
  a new trace or picture rewrites.
- The GUI pass starts its render pass with `LOAD_OP_DONT_CARE` and a full-screen draw that samples
  `mPicture`, then blends the interface. The draw replaces the `LOAD` read of the target, so a frame
  pays one image read either way. The memory is one more output-sized image: 66 MB at 7680×2160.
- `readPixels`, `capture` and `freezeFrame` read `mPicture`. The target is the presenter's alone.
- `capture` crops to the asked aspect and filters by area, through one function that
  `ScreenshotManager`'s rule and the ray tracer's path both call. Where the last picture is older
  than the request (the world was hidden), it traces one frame first, as the seam promises.
- The line pass raises its colour to the frame's `mInverseGamma`, so both renderers draw the debug
  overlay at the world's gamma.

### Tests

- Two untraced frames with a translucent GUI batch give the same pixels as one.
- `capture` of a frame with a GUI batch over it equals the frame without one, at the asked aspect.
- A debug line at gamma 2 is the gamma-1 line raised by one half, within the store's rounding.

### Verification

`./omw shot --against` moves no picture (the shot reads `mPicture`, which holds what the target held
before the GUI). `./omw release bench`: the GUI pass's zone must not rise.

---

## 14. W13 — One rule for every number and setting the harness reads

**Closes:** *(REVIEW: The harness reads numbers and settings by rules the game states elsewhere)*;
*(A harness option writes the world through a variable that means something else)*; the
player-settings item in *(Measurements and reports …)*; the `toNumeric` item in *(One fact has two
names …)*.

### What is wrong

The harness reads its line, its files and its defaults by three rules: Boost's `lexical_cast`
(accepts `nan` and `inf`, stops at the first character it cannot read), `parseNumber` (whole text,
finite) and `toNumeric` (finite, prefix). A line value goes into the run without the setting's own
sanitizer, so `--distant-cells=40` measures a reach the game never builds. A measured run still
reads the player's `[Shaders]` auto-map switches, `object paging min size`, `viewing distance` and
`specular map layout`. And `toNumeric` itself reads two ways, by compiler: `"+1.5"` is a number on
macOS and not on Linux. `--day` writes the wrong global.

### Target shape

- **`toNumeric` has one behaviour.** The stream branch refuses what `from_chars` refuses (a leading
  `+` or whitespace), and a table test of spellings (`"+1.5"`, `" 1.5"`, `"1.5x"`, `"0x10"`, `"-0"`)
  runs the same claim on every toolchain.
- **One value semantic** for every numeric option: built on `toNumeric`, whole text, a stated range,
  declared beside the help that states it. `parseNumber` becomes the same function, and the five
  scattered hand checks go.
- **A line value goes through the setting's sanitizer**, or is refused naming its range. A film is
  paced against the field of view the camera got.
- **A measured run reads `shippedDefault` for every knob the line does not name**, the `[Shaders]`
  switches and the three named settings included. They reach the engine the way the window does
  (`applyHostedSettings`). `view` keeps the player's settings and frame-rate limit.
- **`--day` states days passed.** The stager sets `mDaysPassed` (through `Globals::sDaysPassed`, or by
  advancing whole days from the new game's start) and leaves the calendar alone where a stop names
  no day. The note writes back the same quantity.

### Tests

The value semantic's table (finite, range, whole text). Days 0 and 3 give two different moon phases.
A measured run's `RtxSettingValues` and `[Shaders]` switches equal the shipped defaults whatever the
registry holds.

---

## 15. W14 — The ray tracer shows what the rasterizer shows

**Closes:** the eleven groups the parity review added to `REVIEW.md`, from *(A blended surface's
coverage is decided by the material's alpha alone)* to *(A picture is traced the first frame it can
be, whatever ground has stood)*, and its items in *(Frame-to-frame state …)* and *(The trace drops
part of what the game asks of the picture)*.

### What is wrong

The parity review checked 352 capabilities: 172 the same, 107 approximated for a reason the tree
states, 36 missing, 34 wrong, 3 stale. Nothing arrives a frame late: the walk reads every change on
the frame the rasterizer draws it. The defects are in **what** the ray tracer reads, and they have six
causes:

| Cause | What it does | Worst case |
|---|---|---|
| The material reader keeps a subset of the loader's facts, by its own rules | coverage from the material's alpha alone, the material's alpha under a vertex tint, a grey slot for "no map", one UV set for every map, a medium walk that reads the diffuse map alone, unread facts dropped with no refusal | the Imperial lantern's glass vanishes; lava, waterfalls and webs lose their coverage (781 vanilla shapes) |
| Content the rasterizer draws has no reader | groundcover, six texture formats, untextured particles, a ring model with one bad drawable, distant animated statics, distortion nodes | every groundcover mod draws bare ground; a BC7 or 24-bit mod texture draws grey |
| The trace's light rules are its own | negative lamps refused, flicker centred on 1, Night-Eye metered away, a bolt lit by its area | Telvanni towers come out lit; torches burn 1.6 times too bright |
| The sky reads the weather by its own rules | a linear sky ramp, decks and sea turned about the origin, a stepped moon phase, decks fixed at attach, glare without the disc's alpha, sprites dropped, typed-in water colour, `tsky` as "indoors" | every exterior frame's sky is too near the fog colour |
| The seam does not answer what the game asks | bind-pose picking, a static camera that drops the player, a script's view distance, `tws`, `tww`, `ToggleBorders`, shader reload, texture filters, rain under roofs, stereo, the reach's bound | a corpse cannot be focused or looted |
| The walk resolves state its own way | one fade number for two uniforms, the innermost class mark wins, `PolygonMode` dropped, a 0.2 s emitter cap, one cut for every object, map tiles before their ground, a glow updater's reset never heard | a spell-cast glow on a door never ends |

### Target shape

**W14.0 — One declaration says what each renderer honours.** The seam gains
`Renderer::support() const`, a table made once per renderer: for each setting the game's renderers
read, each render mode, and each script request (`reloadShaders`, `setViewDistance`, stereo), the
answer `Honoured` or `Declined` with its reason. Its readers:
- the settings window greys out a declined control and shows the reason as its tooltip;
- the console and Lua answer "not available under this renderer" for a declined mode, where today
  `tww` and `ToggleBorders` report "Off";
- `processChangedSettings` hands a renderer only the keys it honours;
- `rtx.rst` lists the declined set, and a test holds the page to the table.

**A parity test guards it:** every key `GlRenderer` and the shared `mwrender` files read through
`Settings::` appears in the ray tracer's table, honoured or declined with a reason, so a setting
upstream adds fails the test until someone decides it. This replaces §16.8's `readsSetting`, the
separate "or logs once" answers below, and the groundcover log line.

**W14.1 — The material reader carries the loader's facts by the rasterizer's rules.**
- **Coverage follows the content's own flags, as the rasterizer's does.** A blend with no alpha
  test is translucent; a test cuts at its reference. The one shortcut is exact: where every texel of
  the finest level is 0 or 255, a cut at any reference covers what the blend covers, so such a blend
  may be cut. "Binary alpha" is one more fact in W4's `ImageFacts`, computed in the same walk as the
  mean. No share of texels and no threshold decides it. The cost of the soft blends this makes
  translucent (781 vanilla shapes) is measured before it lands.
- Under `ColorMaterial::Tint` the opacity is the vertex alpha, as on the sprite path.
- Two neutral slots: a white texel for a surface with no map, the grey stand-in for a refused map.
- Each map carries its UV unit (`mEmissiveUnit` beside `mDarkUnit`). The mesh reader tells the sets
  apart by the index the loader knows (a user value on the geometry), not by the array's address.
- One per-crossing material read (`texel × dark`, and the sheet where `mEnvironment` is set), shared
  by `crossingOf`, `candidateStops` and `resolveFor`.
- **`describeStateSet` is exhaustive.** It switches over `osg::StateAttribute::Type` with no
  default, so a type upstream adds stops the build (`-Wswitch` under `-Werror`) until someone decides
  it; each case carries the fact or files a refusal once per state set. The GL modes, which are not
  an enum, are checked against the list of modes `NifOsg` sets, and a mode off the list is refused.
  The alpha-test function is carried as a comparison selected without a branch.
- A `classic` specular layout: `F0 = rgb`, `α = sqrt(2 / (n + 2))` with `n = a × 255`, stated as the
  approximation it is. `apply lighting to environment maps` moves the sheet into the lit term.

**W14.2 — Content the rasterizer draws reaches the trace.**
- **Groundcover is one merged mesh per cell**, as the rasterizer's chunks merge it: the ring's
  reader builds it from `GroundcoverStore` with `Groundcover`'s own `DensityCalculator` (moved to be
  shared), under `rendering distance` and `density`. One instance per cell keeps the top-level
  structure's row count what it is today, where an instance per plant would multiply it. Stomp is a
  later deformer.
- **One rule for image formats:** a format the device samples natively is uploaded as it is (every
  BC format is core on the target cards); every other layout `OSG` decodes is widened to RGBA8 on the
  way in, as the sixteen-bit formats already are. A refusal is left only for what cannot be decoded.
- An emitter with no image draws with the white texel, coloured by its vertices; a generated image
  enters the table under a key made from its address, as the composites do.
- The ring's template walk refuses a drawable, not the whole model, as the frame's walk does.
- A prepared part whose chain carries an AutoPlay state-set updater gets one animated material per
  model, applied once a frame through `MaterialResolver::animate`.
- A `distortion` node is left out of the trace and filed as a refusal; a refracting pane of its
  strength is a later step.

**W14.3 — The trace's light rules are the game's.**
- `Light` carries its sign. Subtracting lamps are a run of their own in `LightGrid`; `gather` takes
  their unshadowed term off the direct lamp term, floored at nought, as the rasterizer clamps. They
  cast no shadow and no bounce, and that is stated.
- `lightcontroller.hpp` names the band (0.25, 1) and the speeds. `lightBrightness` keeps its
  deterministic ladder and spans the band around 0.625; the periods derive from the speeds.
- Night-Eye follows D8.
- `getMagicBoltLightReach` and the `lightReach` parameter go: the bolt's lamp is the game's 66 units,
  and one upstream hunk leaves the diff.

**W14.4 — The sky reads the content's sky.**
- The atmosphere mesh is read in `addSkyContent`, its `i % 2` alpha rule moves to
  `components/sky/vertexrules.hpp`, and the two ring sines reach the frame. `skyGradient` ramps between
  them, and `skyBudget` integrates that ramp in closed form. A sky mod's mesh is then read too.
- The deck turns about the eye; the sea keeps a fixed heading.
- `MoonModel` reports a phase angle continuous in game time, which `placeMoon` takes; `GetMasserPhase`
  keeps the engine's discrete answer.
- `SkyContent` is keyed by texture path; `SkyReader` takes the two names off `WeatherResult` each
  frame and opens a new path once. Both crossing directions of a deck fade as the rasterizer's.
- The glare's strength takes the disc's alpha, through W10's shared fader.
- The sun's flash is an additive sprite in the display chain, scaled by the seen share
  `SunGlarePass` eases; or `look.h` states that bloom stands in for both sprites.
- The water's scattering colour and the weather fog's share come from the fallbacks through
  `SkyReader`, and `WATER_EXTINCTION` gets a test of its stated pairing.
- `WorldReading` carries "open to the sky" and "the sky is drawn" apart, so `tsky` blanks only the dome.

**W14.5 — The seam answers what the game asks.**
- **Picking poses on demand.** `Renderer::poseForIntersection(osg::Drawable&)`: nothing for the
  rasterizer, whose cull posed it; for the ray tracer, `OffscreenTrace`'s `PoseCull`, moved to
  `components/sceneutil`, run once per frame number on each skinned drawable a segment enters.
  `IntersectionVisitorWithIgnoreList` calls it. That visitor is upstream code, so the hook joins
  Accepted diff with the reason. A pose per ray that reaches an actor costs less than the rasterizer's
  pose per actor per frame.
- `TracedTerrain` answers a terrain hit past the active grid from the storage's heights.
- **One translation of what a view shows.** `rayMaskOf` becomes `describeView(spec)`, which answers
  every question a view's mask asks: the classes the eye casts against, whether the medium and
  additive walks run (W9's class-mask row), whether lamps light it (`Mask_Lighting`), and whether its
  sun casts. A map tile then shows what the rasterizer's map shows, and stops paying three traversals
  a pixel.
- The harness hides the player through the seam's view mask for the stops it stages there.
  `EyeState::mPlayersEye` and `WorldMirror::setShowsPlayer` go.
- `EyeState` carries a script's view distance as an override; the trace closes the air at
  `min(reach, d)`, and the ring keeps the setting's reach, so a script never rebuilds the world.
- Under `tws` the ray tracer keeps tracing with the world's masks left out. `TracedTerrain` makes a
  `CellBorder` with `Mask_Debug`, which `DebugWalk` draws. `tww`, the shader reload and stereo are
  answered by W14.0's table until each is built.
- `RtxSettings` carries the texture filter and mipmap keys, and the content sampler is made from them.
- `WorldState` carries `weather particle occlusion && isOccluded()`, which both renderers read.
- `reachOf` clamps both of its sources to one bound under `sFarPlane`.
- `ViewQueue::draw` defers a world view while a cell under its box is in the ring's disc and not yet
  placed (`CellRing::isStood`).
- `setProjectionOffset` is a principal-point shift in `Camera`, applied to both eyes, as the
  rasterizer shifts the arms' projection too.

**W14.6 — The walk resolves state as the rasterizer's stack does.**
- `SceneUtil::StateSetUpdater` keeps a generation that `reset()` bumps, and `MaterialResolver::animate`
  sets the mirror's state set up again when it moves. A spell-cast glow then ends, and a recolour holds.
- `Shading` carries `mAlpha` and `mActorFade` apart, each replaced by the nearest state set that
  states it. The opacity is their product at the drawable, and sprites take it too.
- A `FirstPerson` ancestor wins over an inner `Effect` mark.
- `DebugWalk` keeps the `PolygonMode` along the path and emits edges where it is `LINE`.
- The emitter clock advances by the world's step on every walked frame; the 0.2 s constant goes.
- A single object's jump is W1's `FrameStep::mJumped`.

### Tests

Each fix carries a test with hand-computed values, on the GPU where the fact is a picture: a soft
pane keeps its coverage and a foliage mask keeps its cut; a tinted candle's opacity is its vertex
alpha; an untextured part draws at its material colour; a glow on UV set 1 lands on its set; a
subtracting lamp darkens a lit wall by its unshadowed term; the flicker's mean is 0.625 and its
extremes 0.25 and 1; the sky's share is 0 at 3.6° and 1 at 28.6°; a moon's phase is continuous across
`moonPhaseHour`; a ray through a posed bone hits it; a `GlowUpdater` run through its end leaves no
sheet; a sprite under a faded actor is faded; W14.0's parity test; a binary-alpha blend cut at half
covers the texels the blend covers, and a soft blend stays translucent. The vanilla scans the parity
review ran (blend pairs,
texturing slots, UV sets, alpha histograms, negative and animated light records) become a
`scene --census` count, so a later change to a material rule says how many shapes it moves.

### Verification

`./omw shot --views=all --map --upscale=off --against=<baseline>` after each step: a correction moves
the pictures of its own content and nothing else, and the commit names them. `./omw repeat --pairs=10`
for the walk's changes. `./omw release bench` for groundcover and the new formats: arrival frames'
worst case and the p99 must not rise beyond what the new content costs, and the report says what that
is.

---

## 16. Smaller workstreams

### 16.1 The upstream diff, as decided

§1 holds the work each decision leaves:

1. **Debug-only checks on hot paths.** Each `Crash::notNull` on a per-node or per-frame path
   becomes the debug-only form.
2. **The port's defects:** the settings migration, the launcher's custom size, `androidmain.cpp`,
   the `GraphicsWindowSDL2` rename, and the pixel-to-point truncation.
3. **The comments and docs that the decisions make false:** the `GlRenderer` class comment, the
   upscale docs, `rtx.rst`'s "untouched" and its `-DOPENMW_RTX`.
4. **D1 and D6, as decided:** `AGENTS.md`'s SDL3 entry names the removal of contrast; the engine's
   two frame hooks go into Accepted diff, and the weather hold and the script boxes move into the host.
5. **The post-processing package tests the renderer once.** `initPostprocessingPackage` registers
   upstream's usertype where there is a chain and an inert one where there is none, so upstream's
   bindings come back unchanged and the nine null tests go.

### 16.2 Memory and device requirements

*(REVIEW: Memory requests leave the design's promise …; The device check and the enabled
extensions differ …)*

- `sEssentialPriority` beside `sContentPriority`, set in both `take` overloads. One
  `askingFor(properties, use)` states every allocation's priority.
- `memoryTypeBits` per `BufferKind`, computed once in the allocator's constructor by a pure
  function over `VkPhysicalDeviceMemoryProperties`. Tested on the Turing, Ada and RDNA 2 type lists
  (a new `describeRdna2` fixture from a Vulkan Hardware Database report).
- A subgroup obstacle in `profileOf`: quad operations in the compute stage.
- `Device` enables an option's extensions only after its feature holds, in table order.
- `rayTracingMaintenance1` leaves the requirements, or names its reader.
- **An arrival's block covers only what is still to place.** `BottomLevelStore::build` subtracts
  each placed structure's aligned size from `wanted` before the next `take`, and the two comments
  that say an arrival "asks for nothing" are corrected. A test takes part of a run into a hole and
  asserts the new block's size.
- **The push-constant limit is a requirement.** One `sPushConstantBytes = 256` beside
  `sApiVersion`; `profileOf` refuses a device below it by name, and `pushRangeOf` asserts every
  constants block against it (the tone pass pushes 160 bytes, the sprite bin 144).
- **The stress hold asks the device's clock rate.** A start-up probe reads `clockRealtimeEXT` across
  a pair of timestamp queries, and the hold is handed in ticks.

### 16.3 The crash monitor

*(REVIEW: The crash monitor takes a report's kind from the wrong owner)*

The exception decides the kind. A real exception is a crash, and the note table's kind and reason
are read only for a simulated dump. `terminateReason` writes into a fixed buffer with
`std::format_to_n`. `kindOf` and `markOf` end in `Crash::fatal`. `end()` returns which of its two
outcomes happened. The monitor reads `noteTable().size()` bytes and the command line carries only
the address. A crash-matrix mode faults a second thread inside a `Crash::report`.

**One thread shows the monitor's dialogs.** `HandlerMain` runs on a worker. The main thread waits on
one queue of requests ("ask to end" from the watch, "tell the player" once the handler returns),
answers them in order, and stops the watch through it. This ends the deadlock on macOS, where the
watch thread's message box waits for a main queue that Crashpad's Mach loop never drains, and a crash
is never held behind an unanswered hang question.

### 16.4 The visibility gates

*(REVIEW: The visibility gates model one frame of a script)*

Run each way frame after frame, carrying the locals and the answer forward, until a state repeats.
Bound the count and answer `Undecided` at the bound. Catch `std::runtime_error` and `Undecided` in
one handler and let other exceptions end the process. Keep the run's context, locals and written
globals as cleared members, and key the inputs by a global's index. Move `handsOver` to
`Terrain::RefKinds`, so `mwscript` stops depending on `mwrender`.

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
| Shading math departs from the exact form it claims | each is a correction with a hand-computed test; each moves pictures, and the commit names them |
| The trace drops part of what the game asks of the picture | the projection offset and the view mask are W14.5's; `Precipitation::isFrozen()` as the one underwater answer for the rain; the cloud deck named as both sheets when only the next weather has one |
| A reader decodes data that it never checks it can decode | assert the precondition in `texelAt` and `readTexelBand`; the contact sheet draws a bake, a composite and BC5 as a labelled blank |
| A queue or cache loses what it must keep … | `chainSignature` compares exactly; pipeline-cache partials get a name the sweep ages out; Qt installs beside its final name and renames |
| Test-only code ships in the game | `MipChain`'s builder and `OwnedTexture` move to test support beside `SpriteLightBake`; `SpecularAlbedo::at` and the `describeImage` overload go |
| Docs and comments state what the code does not do | correct or delete each one |
| Frame constants are derived on every lane … | the host writes each into `VisibilityConstants` once |
| Kernels step around rules … | `RTX_STORE_COUNTED` for `puffs` and `backdrop`; one path or a named measurement for each branch |
| Parameters and fields carry nothing … | delete the dead fields and parameters; `PathSeeds` for the three seeds |
| Arrival frames allocate or churn … | one refusal per model per cell; report a refusal when the count moves; one owner per path string |
| Canaries that no report reads | the stop report prints the `NodeKinds` overflow and `mWornBeyondKept` |
| Includes that name nothing they use | delete them |
| Smaller defects | each as written; `shadow.h`'s bound becomes `< 32`, or the mask is built as `~0u >> (32u - width)` |
| Shading math departs … (second review) | the normal map's spread is read at the level its anisotropic read resolves; the water's rays are biased once; each moves pictures, and the commit names them |
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

Each phase ends green on `./omw gate`. Two steps wait on a measurement: W8 step 3 on D5, and
Night-Eye on D8.

```
Phase 0  verdicts, baselines ─► Phase 1  defects, by severity ─► Phase 2  ownership
    ─► Phase 3  frame record ─► Phase 4  contracts ─► Phase 5  frame cost
    ─► Phase 6  upstream diff ─► Phase 7  tests and docs
```

### Phase 0 — The verdicts, then the baselines

- **The harness's verdicts get their tests first (P7).** `compareRuns` and `judgeNoise` get table
  tests over a directory of PNGs. `MeasureWindow` is lifted out of `Measurer` and gets a table test.
  `omw repeat` reads a verdict status in place of the report's sentences. Every later phase is judged
  by these three, so a defect in them would pass every later phase.
- `./omw shot --views=all --map --upscale=off --out=<dir>`, with the directory outside `/tmp`.
- `./omw kernels > <dir>/kernels-before.txt`.
- `./omw release bench`, with a warm-up leg first, in the background on a quiet desktop. Keep
  `--frame-times=<dir>`.
- `./omw repeat --pairs=10` to confirm the tree is deterministic before any change.
- D5: the half-float store probe.

### Phase 1 — Defects, by severity

Every defect a local change fixes, in one list, highest first; within a severity, the ones that
share a workstream sit together. One commit each, each with its test. A correction that moves
pictures names them.

**Step 0.** W4's `ImageFacts` record lands first, with `binaryAlpha` beside the mean, because the
coverage rule (5) reads it. The rest of W4 stays in Phase 2.

**High:**

1. `--day` states days passed (W13).
2. The ripple burst after a pause (W1's consumed list).
3. Essential memory priority (§16.2).
4. The contact sheet's precondition (§17).
5. Blended coverage from the content's flags (W14.1).
6. Picking on the posed body (W14.5).
7. The glow updater's reset heard by the mirror (W14.6).
8. The sky's ramp from the atmosphere mesh (W14.4).

**Medium:**

9. The picture and the interface as two images (W12).
10. The ring's request from its inputs (W2).
11. The structure block sized from what is left to place (§16.2).
12. `describeView` (W14.5): the class mask, the lamps and the sun of a view.
13. The crash monitor's dialog thread (§16.3), and the harness's hang limit (W10).
14. The denoiser turn on unfiltered frames (W1).
15. W14.1's rest: vertex alpha under a tint, the white neutral slot, the glow's UV unit, the medium
    walks' dark map and sheet.
16. W14.3: negative lamps, the calibrated flicker, the bolt's lamp. Night-Eye after D8.
17. W14.5: the static camera, the script's view distance, map tiles that wait for their ground.
18. W14.4: the storm heading, the continuous moon phase, decks keyed by path.
19. W14.6: the two fade uniforms, the first-person class.
20. W14.2: the image-format rule, then groundcover.
21. W13 and W11: the shipped defaults, one rule for a number, the card watch.
22. W6: the composite queue, the instance record written twice, `SlotSet`'s third state.
23. The crash kind from the exception (§16.3).
24. The shading corrections of §17.

**Low:** W14.0's declaration and its parity test first, because it answers most of the low parity
items at once; then the low items of each workstream, by section.

### Phase 2 — Ownership

W2, W3, W4, W5. These are refactors. Each step must show no moved picture and an agreeing
`repeat`. W4 and W5 also show a lower `preprocess` row and arrival-frame worst case.

### Phase 3 — The frame record

The rest of W1: `FrameStep` with its loss and its jumps, the table's `FramePast`, the cut on a clock
jump. After Phase 2, because `FrameContext` and `RunSetup` change shape there.

### Phase 4 — Device contracts

W7 and W10. `./omw kernels --against` after each step. The exposure edge and the format macro are
corrections that move pictures.

### Phase 5 — Frame cost

W6 (block-growth tables, running totals, the refit), W8 steps 1 and 2, W9, the barrier rows of
§16.6. Each with a
`./omw release bench` before and after. A change that does not improve the worst frame or the p99
does not go in. W8 step 3 follows D5 and its own measurement.

### Phase 6 — The upstream diff

§16.1, then §16.5, §16.7 and §16.8.

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
