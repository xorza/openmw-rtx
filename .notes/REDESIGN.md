# Structural redesign: proposal and plan

This proposal answers `.notes/REVIEW.md`. It does not repeat the findings. It names the few
structural causes behind most of them, gives the target shape for each cause, and orders the work.
A reference such as *(REVIEW: Frame-to-frame state)* points at a group heading in `REVIEW.md`.

The 309 findings fall into three kinds:

1. **Structure.** About 150 findings come from eleven causes in how the code is owned and connected.
   Sections 2 to 12 redesign those causes. A change of structure closes a whole group, and the
   group's items go with it.
2. **Decisions.** Twenty findings ask a question that only the owner can answer: what the fork
   accepts against upstream. Section 1 lists them. Work on the upstream diff waits for the answers.
3. **Fixes in place.** About 140 findings are local: stale comments, includes, a constant written as
   a rounded decimal, a test that asserts too little. They need no design. Section 14 lists the
   groups and the rule for each.

## Contents

0. [Principles the redesign keeps](#0-principles-the-redesign-keeps)
1. [Decisions for the owner](#1-decisions-for-the-owner)
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
12. [W11 — Instruments stay out of the measured frame](#12-w11--instruments-stay-out-of-the-measured-frame)
13. [Smaller workstreams](#13-smaller-workstreams)
14. [Fixes in place](#14-fixes-in-place)
15. [Order of work](#15-order-of-work)
16. [Verification for every phase](#16-verification-for-every-phase)

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

---

## 1. Decisions for the owner

The redesign cannot answer these. Each changes how much of the upstream diff stays. My
recommendation is first in each list.

### D1. The SDL3 port *(REVIEW: The SDL3 port and the scaled presentation …)*

The port touches about 144 upstream files. The ray tracer calls no function that SDL 2.26 does not
have. The port also removes `[Video] gamma` and `contrast`, because SDL3 has no gamma ramps.

- **(a) Recommended: keep SDL3 and record it.** Add the port to the accepted diffs in `AGENTS.md`,
  with its reason. Restore gamma and contrast in some other way, or name their removal as an
  accepted change to the rasterizer's picture. Then fix the three defects the port brought in:
  the settings migration, the launcher controls and `androidmain.cpp`.
- **(b) Revert to SDL2 and keep only the seam.** This is the smallest diff. It is also the largest
  piece of work, and it gives up SDL3's display scale on Wayland.

Choose (a) only if SDL3 serves a reason that the fork has, for example pixel density on Wayland. If
no such reason exists, (b) is the answer the rules give.

### D2. The scaled presentation *(REVIEW: same group, and `glrenderer.hpp:83-86`)*

The GL path now draws into a frame texture and blits it into the window. This is a fifth change to
the rasterizer's picture. `[Video] resolution x/y` now means the frame, not the window.

- **(a) Recommended: the GL path presents at native size always** (`presentAtNative()`, which it
  calls under stereo already). The pingpong, postprocessor and intersector hunks go back to
  upstream. `resolution x/y` then has a meaning for the ray tracer only, and the settings
  migration problem goes away for the rasterizer.
- **(b) Accept it.** Add it to `AGENTS.md` and `architecture.md` §1 as a fifth correction, with its
  reason, and correct the `GlRenderer` class comment. Add the settings migration (an old file with
  `resolution x/y` and no `window width` copies the values once).

### D3. The scope of the extra warnings *(REVIEW: The fork's extra warnings apply to the whole tree …)*

Commit `41554ed6a9` turned five extra warnings on for the whole tree. About 80 upstream files carry
hunks only to quiet them, with a patched sol3, `extern template`s of Boost, and release-mode
`Crash::notNull` checks on per-node hot paths.

- **(a) Recommended: scope the flags to the fork's own sources.** See §13.1 for the mechanism. Then
  revert every hunk in an upstream `.cpp` file. Header hunks that a fork translation unit reaches
  stay, because the warning fires in the fork's compile of that header.
- **(b) Keep the flags tree-wide, and name it in `AGENTS.md`.** Then at least change the hot-path
  `Crash::notNull` checks to the debug-only form, which the rules require in any case.

### D4. Three upstream bug fixes *(REVIEW: same group)*

`ContentModel::dropMimeData`, `Store<ESM4::Cell>::insert` and a moved reference with a missing
source cell are real upstream bugs. The ray tracer needs none of the fixes.

- **(a) Recommended: send them upstream, and drop them here once upstream has them.**
- **(b) Keep them, and list them in `AGENTS.md` as accepted fixes.**

### D5. A measurement, not a choice: how this device rounds a half-float store

`specular.h` says this device rounds half-float stores toward zero. `accumulate.h` reasons as if it
rounds to nearest. One of the two is wrong, and three histories depend on the answer
*(REVIEW: One fact has two names …)*. Run a probe kernel once (the place is
`components/rtxvulkan/shaders/probes/`). §9 (W8) waits for the result.

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

### Target shape

The host says two facts about each frame. The backend turns them into one record and hands that
record to every owner of a history.

**In the core** (`components/rtx/frame/`, beside `FrameOptions`):

```cpp
/// What happened between the last traced frame and this one, as the host knows it.
struct FrameStep
{
    /// The simulation advanced. False on a paused frame: nothing moved and nothing was pressed.
    bool mSimulated = true;

    /// The host's water clock, in seconds, as a double. The constants keep their split pair for
    /// the shaders; the backend steps the ripples on this value.
    double mWaterSeconds = 0.0;
};
```

`FrameOptions` gains `FrameStep mStep`. `Renderer::resetHistory()` takes the kind of loss:

```cpp
enum class HistoryLoss { Cut, Worldspace };
virtual void resetHistory(HistoryLoss loss) = 0;
```

A cut drops every history but the water. A worldspace change also drops the water. `setScene` of
the world is a worldspace change.

**In the backend** (`VulkanRenderer`):

- One member `bool mPastLost` replaces "zero `mPreviousCamera`". `resetHistory`, `setScene` of the
  world and `createTargets` set it. `renderFrame` reads it once, clears it, and hands it on.
- One record goes to every owner of a history, in `renderFrame`:

  ```cpp
  struct FramePast
  {
      bool mLost = false;      // no reprojection is valid
      bool mSimulated = true;  // FrameStep::mSimulated
      double mWaterSeconds = 0.0;
  };
  ```

  `TraceChain::record`, `Upscaler::record`, `DisplayChain::record` and `TraceMedia::stepRipples`
  take it. `DisplayChain::mExposureStale` and `mGlareStale`, `TraceChain::mAirStale` and the
  `basisLost` local go. Each owner keeps
  only its own rule for what a lost past means to it, written beside the owner.

- **The denoiser turns on every frame.** On a frame with no denoiser, `TraceChain::record` calls
  `mDenoise.turn(TemporalFlags{})`. `TemporalTurns::next` with no filter that runs makes every
  filter fresh at its next run, which is the rule its own comment states. This also replaces the
  "a reset waits for the frame that filters" deferral: a filter that did not run reads no history.

- **The ripples consume their impulses.** `TraceMedia::stepRipples` moves the kept impulses into
  `RipplePass`'s pending list and clears its own copy. `RipplePass::record` appends nothing when
  `mSimulated` is false. On the game side, `RippleEmitters::update` runs on every frame and yields
  an empty list on a paused frame, so the scene's list is true for every frame and the digest
  agrees with it.

- **The interface draws over the picture, never over itself.** Two options:
  - *(recommended)* On a frame that presents without a trace, `DisplayChain` records its last
    curve pass again from the inputs it kept, into the target. A traced frame pays nothing. Before
    you choose this, confirm that the curve's inputs (the upscaler output or the trace colour, the
    bloom chain, the exposure) are not written between two traces.
  - Keep the picture in its own image, and start each GUI draw with a copy of it. This costs one
    copy of the output extent on every frame: 66 MB at 7680×2160 RGBA8.

### Steps

1. Add `FrameStep` and `HistoryLoss`. Change the seam's `notifyCut` and `notifyWorldspaceChanged`
   in `RtxRenderer` to call `resetHistory` with the kind. Delete the ripple reset from the
   cut path.
2. Add `mPastLost` and `FramePast`. Move each owner to read it. Delete `joinSeconds`.
3. Turn the denoiser on unfiltered frames. Delete the pending-reset deferral that this replaces.
4. Make the ripple impulses a consumed list. Make `RippleEmitters` clear on a paused frame.
5. Make the untraced frame redraw the curve (or copy the picture).

### Tests

- `RipplePass`: two records at one tick with one footfall each press one footfall. A record with
  `mSimulated = false` presses nothing.
- `TraceChain`: filtered, unfiltered, filtered gives the same last frame as a fresh reset.
- `VulkanRenderer`: two untraced frames with a translucent GUI batch give the same pixels as one.
- `RtxRenderer` (openmw-tests): a cut keeps the ripple field and a worldspace change drops it.

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
| constants that are rounded decimals of closed forms | written as their derivation (`vec3(12, 30, 37) / 255.0f * 0.85f`, `vec2(5, 12) / 13`, `2.0f * PI / 180.0f`) |
| three constants that fold a transcendental on both compilers | one rule stated in `portable.h`: literal plus a test, or allowed because no host value must match |

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
| the sun-glare fader colour | `Sky::SunGlareFader` in `components/sky` | `SunGlareCallback` and `SkyReader` |
| `sWeatherCount` | `= ESM::Weather::Length` | — |
| the game's starting `timescale` | read from the world's globals at session start | the harness and its Lua |
| the log stamp format | one `Debug` function | the game's log and the crash monitor |
| the executable path, local time, the UTF-8 command line | `Platform::Process` (posix and win32 pair) | the crash catcher and `components/files` |
| pixel-to-point conversion | `SDLUtil::windowPoints` | `sdlgraphicswindow.cpp` too |

A rule that the GLSL headers must restate (they cannot include game code) gets a test in
`openmw-tests` that holds the two equal.

---

## 12. W11 — Instruments stay out of the measured frame

**Closes:** *(REVIEW: Measurements and reports state something other than what was measured)*;
the file and writer items in *(Files and folders hold what another owner owns)*.

### Target shape

- **The card watch opens its window on its own thread.** The main thread publishes "a window
  begins now" as an atomic generation. The worker resets its clock and tally on its next turn and
  stamps the window. `nameProcess` runs only for a pid that is new and not this process. Perf's fifo
  opens at `Measurer` construction, outside the measured frames.
- **The record carries its premise.** `asJson(GpuClock)` writes `meanMhz` and `readings`.
  `GpuClock` holds the throttle mask and the temperature as `std::optional`, and AMD's reader
  answers "unknown", which the report prints and the JSON writes as `null`.
- **One rule for a knob with no option.** A measured run reads `shippedDefault` for every knob the
  command line does not name (`mObjectPagingMinSize` and the view-distance fallback among them).
  `view` reads the player's settings. A `view` keeps the player's frame-rate limit.
- **Output files are written after `Engine::go` returns**, from `SessionResult`. A failure is noted
  in the report and sets the exit status, as every other writer does.
- **One asynchronous picture writer**, owned by the session: the film, `--pictures` and Home use
  it. `PerfControl` and `GpuBreakdown` get their own files.
- `PerfControl::enable` retries `ENXIO` until a stated deadline, so `profile --offcpu` needs no sleep.
- The memory report labels a heap by both flags (`device+host`, `device-only`, `system`).

### Verification

A bench of one view twice, before and after, back to back, started in the background on a quiet
desktop. The first measured frame of each stop must lose its spike. The JSON diff shows the two
new fields.

---

## 13. Smaller workstreams

### 13.1 Upstream diff hygiene (after D1–D4)

The mechanism for D3 (a): one CMake function adds the five options to a list of sources.

```cmake
function(openmw_fork_warnings)
    foreach (target IN LISTS ARGN)
        target_compile_options(${target} PRIVATE -Wnull-dereference -Wcast-qual
            $<$<COMPILE_LANGUAGE:CXX>:-Wsuggest-override -Wzero-as-null-pointer-constant>
            $<$<CXX_COMPILER_ID:GNU>:-Wdouble-promotion>)
    endforeach ()
endfunction ()
```

The core is part of `components`, so it needs `set_source_files_properties` on the list that
`add_component_dir` builds for `rtx`, `myguirtx` and `crashcatcher`. The backend, `apps/rtxtool`
and `apps/openmw/mwrender/rtx` are targets or source lists of their own. Then:

1. Revert each hunk in an upstream `.cpp` file whose only fork commit is `41554ed6a9`.
2. Build. A warning that now fires in a fork compile of an upstream header keeps that header's hunk.
3. Revert the sol3 patch and the Boost `extern template`s, which exist only for GCC 15's
   `-Wnull-dereference`.
4. Any `Crash::notNull` that stays on a per-node path becomes the debug-only form.

### 13.2 Memory and device requirements

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

### 13.3 The crash monitor

*(REVIEW: The crash monitor takes a report's kind from the wrong owner)*

The exception decides the kind. A real exception is a crash, and the note table's kind and reason
are read only for a simulated dump. `terminateReason` writes into a fixed buffer with
`std::format_to_n`. `kindOf` and `markOf` end in `Crash::fatal`. `end()` returns which of its two
outcomes happened. The monitor reads `noteTable().size()` bytes and the command line carries only
the address. A crash-matrix mode faults a second thread inside a `Crash::report`.

### 13.4 The visibility gates

*(REVIEW: The visibility gates model one frame of a script)*

Run each way frame after frame, carrying the locals and the answer forward, until a state repeats.
Bound the count and answer `Undecided` at the bound. Catch `std::runtime_error` and `Undecided` in
one handler and let other exceptions end the process. Keep the run's context, locals and written
globals as cleared members, and key the inputs by a global's index. Move `handsOver` to
`Terrain::RefKinds`, so `mwscript` stops depending on `mwrender`.

### 13.5 Layering guards

*(REVIEW: The layering's documents and guards lag the code)*

`RtxSourceTreeTest` reads the folder order from `architecture.md`'s table, so the order has one
statement. Move `shaders/` to the top of that table. The test refuses a quoted include with a `..`
component, and refuses `<apps/...>` under `components/rtx` and `components/rtxvulkan`. Then fix the
49 quoted `"../"` includes in the fork's `mwrender` files, the two in test support, and the
`#ifdef _WIN32` in `glrenderer.cpp`.

### 13.6 Vulkan lifetimes

*(REVIEW: Vulkan objects are waited on or rebuilt by a side effect …)*

A slot waits on the timeline value its own submit returned (`Submission`), never on a stamp of an
object its record may not name. Staging blocks retire through `Retiring<std::size_t>` like every
other retired object. The presenter compares against the extent it was last asked for. The two
GUI `finish` calls before a present-mode change go, or state the reason that remains. A trace
pipeline reads each named module once per build and chains the words (`maintenance5`).

### 13.7 Tooling single sources

*(REVIEW: The driver, CI and CMake restate facts that each other hold)*

The presets are the one source for Qt flavours and dependency roots (`$env{OMW_VCPKG_ROOT}`,
`$env{OMW_QT_ROOT}`). CMake writes the shader and digest tool locations into the cache, and
`kernels` reads them. The digest tool lists `SpecId`s, and the Python SPIR-V reader goes. One
`bench_line` helper serves `repeat` and `profile`. `gate` calls the build verb's body. In CI: one
ccache input on `openmw-deps`, one reusable `package.yml`, one format check (`omw format --check`
in `checks`), `GTEST_FAIL_IF_NO_TEST_SELECTED` in the test preset, `.python-version`.

---

## 14. Fixes in place

These groups need no design. Fix each item where it stands, in the commit that touches the file, or
in one sweep per group.

| REVIEW group | Rule |
|---|---|
| Shading math departs from the exact form it claims | each is a correction with a hand-computed test; each moves pictures, and the commit names them |
| The trace drops part of what the game asks of the picture | projection offset as a principal-point shift in `Camera`; `Precipitation::isFrozen()` as the one underwater answer for the rain; the cloud deck named as both sheets when only the next weather has one |
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
| Smaller defects | each as written |

**Tests** follow each workstream. The groups *(Tests that cannot fail …)*, *(Behaviour with no
test)* and *(Test fixtures are duplicated …)* close as each owner is rewritten, with three that
stand alone and go first because they cost every run of the suite:

- the blue-noise spectrum test: a separable transform with a twiddle table, about a thousandth of
  the work;
- the unsettled ring test: a `ContentSource` that blocks until released, and exact counts per walk;
- the renderer and `FrameClock` wall-clock bounds: an injected clock.

---

## 15. Order of work

Each phase ends green on `./omw gate`. Phases 1 and 2 do not depend on the decisions in §1.

```
Phase 0  baselines ──┐
Phase 1  correctness ├─► Phase 2  ownership ─► Phase 3  frame record ─► Phase 4  contracts ─► Phase 5  frame cost
                     │                                                                         │
D1–D4 decisions ─────┴──────────────────────────────────────────────────────────────────────► Phase 6  upstream diff
                                                                                                Phase 7  tests and docs
```

### Phase 0 — Baselines

- `./omw shot --views=all --map --upscale=off --out=<dir>`, with the directory outside `/tmp`.
- `./omw kernels > <dir>/kernels-before.txt`.
- `./omw release bench`, with a warm-up leg first, in the background on a quiet desktop. Keep
  `--frame-times=<dir>`.
- `./omw repeat --pairs=10` to confirm the tree is deterministic before any change.
- D5: the half-float store probe.

### Phase 1 — Correctness with no change of structure

The high and medium defects that a local change fixes. One commit each, each with its test.

1. The ripple burst after a pause (the consumed-list part of W1).
2. The denoiser turn on unfiltered frames (W1).
3. The interface drawn over itself (W1).
4. Essential memory priority (§13.2).
5. The contact sheet's precondition (§14).
6. The composite queue's refusal rule (W6).
7. The instance record written twice (W6).
8. `SlotSet`'s third state (W6).
9. The crash kind from the exception (§13.3).
10. The card watch off the main thread (W11).
11. The shading corrections: refraction cone, glow sums, puff merge weight, add-whole sprites,
    camera in double precision (§14). These move pictures, and each commit names them.

### Phase 2 — Ownership

W2, W3, W4, W5. These are refactors. Each step must show no moved picture and an agreeing
`repeat`. W4 and W5 also show a lower `preprocess` row and arrival-frame worst case.

### Phase 3 — The frame record

The rest of W1: `FrameStep`, `HistoryLoss`, `FramePast`, `mPastLost`. After Phase 2, because
`FrameContext` and `RunSetup` change shape there.

### Phase 4 — Device contracts

W7 and W10. `./omw kernels --against` after each step. The exposure edge and the format macro are
corrections that move pictures.

### Phase 5 — Frame cost

W6 (block-growth tables, running totals, the refit), W8 steps 1 and 2, W9. Each with a
`./omw release bench` before and after. A change that does not improve the worst frame or the p99
does not go in. W8 step 3 follows D5 and its own measurement.

### Phase 6 — The upstream diff

§13.1 by the answers to D1–D4. Then §13.5 and §13.7.

### Phase 7 — Tests and docs

The test groups in §14, and every doc item, `architecture.md` §1 and §13 included.

---

## 16. Verification for every phase

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
