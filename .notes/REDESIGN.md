# Structural redesign: the open work

This plan answers `.notes/REVIEW.md`. It does not repeat the findings. It names the structural
causes behind them, gives the target shape for each, and orders the work. A reference such as
*(REVIEW: Frame-to-frame state)* points at a group heading in `REVIEW.md`. The findings that give a
wrong or missing result for an input the tree can produce are in `.notes/ISSUES.md`.

This file holds only what is still to do. Done work is in the code and the commits, and leaves
this file when it lands. The section numbers are the plan's first ones, which the commits and the
notes cite.

| Workstream | What is left | Phase |
|---|---|---|
| W6 Scene tables change by the row | block growth, running totals, the refit, the presence rows | 5 |
| W8 The denoisers share one surface test | steps 1 and 2; step 3 after its probe | 5 |
| W9 Passes run only over what is new | every row, each with its bench | 5 |
| W14 The ray tracer shows what the rasterizer shows | the items that wait for an input outside the tree | after its inputs |
| W15 Groundcover stands in the ring | the whole of it, test plugin first | 8 |
| W16 A mask's soft texels are layers to the eye | the whole of it, behind its measurement | 8 |
| W17 One row per texture format | the table, then A8, the float formats and BC4 | 8 |
| W18 The harness's folder is the build tree's | the whole of it | 6 |
| W19 The walk visits what can change | the probe, then the frozen subtrees | 5 |
| §16 Smaller workstreams | the upstream diff, the device, layering, Vulkan, tooling | 5 (§16.6), 6 |
| §17 Fixes in place | the local groups; the frame constants | 5, 7 |

**Waiting for you:** the keys' clock reads the session's own `timescale` (`sky.lua` takes it at
`onInit` and `onLoad`), and no window ran it. A run under stub modules gave the expected steps.
Press the clock keys once in a played `view` to confirm.

---

## 0. Principles the redesign keeps

These come from `AGENTS.md` and the owner's posture. Each workstream below applies one or more.

- **P1. One source for each truth, inside the fork.** A value that two owners keep is a value that
  two owners can disagree on. The redesign deletes the second copy. It does not add a check that
  holds two copies equal, unless the two copies are in two languages and a shared function is not
  possible. Across the line to upstream code, P9 decides.
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
- **P9. Upstream code changes only where it must.** A change to an upstream file is a bug fix, or a
  hook the ray tracer cannot work without, and `AGENTS.md`'s Accepted diff names it. A rule the ray
  tracer shares with the game is a copy in fork code that names the upstream rule it follows, held
  to it by a test where one can reach both; upstream code is not moved, renamed or tidied to share
  it.

---

## 7. W6 — Scene tables change by the row

**Closes:** *(REVIEW: Scene tables redo work for rows that did not change)* and *(Work is batched
behind a threshold …)*.

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
the p99 and the worst frame before and after. The worst frame must not rise. The frame thread's
cache misses a thousand instructions (the report's `host thread` line) must stay within a tenth of
each other across six legs: the walk's spread between legs follows them, and a table read as
arrays is what stops it depending on the heap's layout.

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
3. **Decide the surface history by a measurement.** The proposal in REVIEW keeps two of each
   surface channel in the G-buffer (this frame's and last frame's) and binds last frame's as the
   history. That deletes two full-frame writes, two image pairs and `mDistanceScale`.
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
| the medium and additive walks | run only where the view casts against their classes (`describeView`); additive placements keep their class bit | a map tile stops paying three traversals a pixel (and stops drawing absent actors' spell sheets) |
| the upscaler's clears | one `Barriers` into `TRANSFER_DST` for all of them, one back merged into `between()` | two barrier commands a frame instead of up to eleven |
| `barrierBeforeBuild` | deleted; its comment moves to `recordRefit`'s `barrierAfterBuild` | one drain fewer per moving frame |

Each row has its own commit and its own `bench` figure. A change that shows no gain on a hot card
does not go in.

---

## 15. W14 — The ray tracer shows what the rasterizer shows

**Closes:** what is left of *(Content the rasterizer draws reaches the ray tracer with no reader)*.

- **Groundcover** is W15, which has a design of its own.
- **A generated particle image** enters the table under a key made from its address, as the
  composites do; the material reader needs the same key.
- **A distant static's texture animation**: a prepared part whose chain carries an AutoPlay
  state-set updater gets one animated material per model, applied once a frame through
  `MaterialResolver::animate`.
- **The cut reads the diffuse alpha alone**, where `objects.frag` tests `alpha × dark.a`. Reading
  the dark map in `candidateStops` cost the dawn deck's trace 3% (2.59 against 2.50 ms,
  clock-normalised, four legs each), behind a material bit or not, because the code sits in every
  shadow ray's candidate loop. Count the content the difference moves before deciding.

Each carries a test with hand-computed values, on the GPU where the fact is a picture, and a
`./omw shot --against` that moves the pictures of its own content and nothing else.

---

## 15a. W15 — Groundcover stands in the ring

**Closes:** *Groundcover is never drawn under the ray tracer* (`ISSUES.md`), and the groundcover
item of *(Content the rasterizer draws reaches the ray tracer with no reader)*.

### What the rasterizer does

- `MWWorld::GroundcoverStore` keeps, per exterior cell, the ESM contexts of the groundcover files
  (`groundcover=` in `openmw.cfg`), and the model of every static whose model is under `grass/`.
  It holds no reference: they are read off the files when a chunk is made.
- `MWRender::Groundcover`, a `QuadTreeWorld` chunk manager, reads a chunk's references cell by
  cell (`collectInstances`): a later file's reference replaces an earlier one's by `RefNum`, and
  `DensityCalculator` keeps one reference in `1 / density`, in content order, from a count that
  starts again in every cell. Each model is one clone, instanced over its references.
- What it draws: alpha tested at `128 / 255` with blending off, both `OVERRIDE` (MGE's content
  states no alpha it can be trusted with); swayed by the wind and pressed by the player
  (`groundcover.vert`); lit by the lamps only where `[Groundcover] point lighting` is on (it is by
  default); shown per chunk where the chunk's box is within `rendering distance` of the eye (6144
  units by default); in the default worldspace alone; class `Mask_Groundcover`, which
  `classmasks.hpp` already counts among the statics.
- The game's loaded cells hold no groundcover reference, so groundcover stands in the active grid
  too, where the ring stands no static.

### Target shape: an instance per plant, through the ring's static path

**Measured first:** `seyda-neen-ship` stands 64 179 instances today, 57 054 of them the ring's
distant statics, over 3 074 553 triangles in 97.4 MiB of structures (`./omw scene`, 2026-10-03).
6144 units are three quarters of a cell (8192), so the plants of 4 to 9 cells stand at once. **Not
measured, because no grass mod is installed here:** at an estimated 1 000 to 4 000 plants a cell,
an instance per plant adds 4 000 to 36 000 rows, under the ring's statics today; one mesh merged
per cell and model, the plan's first shape, adds their triangles instead, at an estimated 30 to 100
a plant 0.1 to 3.6 million, up to today's whole geometry again. So a plant is an instance of its
model's mesh, as a distant static is, and the merge is the fallback the measurement below decides.
The fixture and a real mod replace both estimates with counts.

1. **A source of groundcover references, in the core** (`components/rtx/mirror/cells/`):
   ```cpp
   /// The groundcover references of one exterior cell, as the game's groundcover reads them.
   class GroundcoverSource
   {
   public:
       virtual ~GroundcoverSource() = default;
       /// Appends the cell's references the density keeps, in content order. Called on the
       /// ring's reader thread alone.
       virtual void collect(const osg::Vec2i& cell, std::vector<Terrain::PagedCellRef>& into) = 0;
       /// The model a groundcover record names, empty where it names none.
       virtual VFS::Path::NormalizedView modelOf(ESM::RefId record) const = 0;
   };
   ```
   `CellWorld` gains `GroundcoverSource* mGroundcover` (null where groundcover is off, or the
   worldspace is not the default one), and `WorldAround` gains `float mGroundcoverReach`
   (`[Groundcover] rendering distance`, nought where off).
2. **The game's source** (`apps/openmw/mwrender/rtx/tracedgroundcover.{hpp,cpp}`), over
   `GroundcoverStore`: `initCell`, a `ESM::ReadersCache` of its own (the reader thread is its one
   caller), and the refs read as `collectInstances` reads them. **The density rule is a copy**,
   `Rtx::GroundcoverDensity`, which names `MWRender::Groundcover`'s `DensityCalculator` and stays
   apart from it (P9); a host test holds it to hand-computed keeps: at 0.25, the 4th, 8th and
   12th of twelve references; at 1, every one; the count restarts each cell.
3. **The reader reads a cell's groundcover beside its statics.** `CellReader::read` fills
   `PreparedCell::mGroundcover`, a `std::vector<PreparedRef>` beside `mRefs`, through the same
   `readModel` (a grass model is read once and lent to every cell). Its material is overridden as
   upstream's state set does: cut at a half, no blend (`MaterialReading` gains the override, which
   the extractor's `describeStateSet` would otherwise read off the NIF). No size rule: a plant is
   small everywhere.
4. **The placer stands it by its own rule.** `CellPlacer` places `mGroundcover` where the cell's
   box is within `mGroundcoverReach` of the eye, **in the active grid as well**, and takes it down
   past it; class `Static`. The cell is prepared where either reach holds it, so a grass cell near
   the eye is read even with distant statics off.
5. **Point lighting.** Where `[Groundcover] point lighting` is off, the material carries
   `MATERIAL_NO_LAMPS`, and the lamp walk's weight is multiplied by a factor of nought for it — one
   path, no branch, as `lightShown` does for a hidden class.
6. **Not in this workstream:** the wind's sway and the player's stomp. Both move vertices every
   frame, which is a deformer over a shared mesh; the trace stands the plants still, and the
   difference is named in `rtxsupport` as a declined part of the setting.
7. **The seam:** `rtxSupport().declinedSetting("Groundcover", "enabled")` goes; the log line in
   `RtxRenderer` keeps naming stereo and post processing.

### The test content, generated

A fixture the tree writes: `Rtx::Testing::writeGroundcoverPlugin` with `ESM::ESMWriter`, two
exterior cells near Seyda Neen (−2,−9 and −3,−9), each with groundcover references to vanilla
`flora_*` statics whose models are copied under `grass/` in a test VFS, at known positions, scales
and rotations. Loaded through `groundcover=`, as a player's configuration loads it.

### Tests

- Host: the density copy's keeps; `TracedGroundcover::collect` over the fixture: the count, the
  replacement by `RefNum` across two files, and the positions read back.
- Host: the ring over the fixture with a test `ContentSource`: plants stand in the active grid and
  out of it, stand within the reach and go past it, and none stands with groundcover off.
- GPU: one plant, a quad with a soft alpha, met by the eye where its alpha passes 128 and not
  where it does not.
- The harness: `shot` of a fixture view under both renderers, and the placed count against
  `GroundcoverStore`'s at density 1 and 0.5.

### Acceptance

`./omw release bench` with a groundcover mod at its default density: the trace zone, the top-level
build, the frame's p99 and its worst against the same run with groundcover off. **If the trace
zone grows more than a fifth** from the instances' overlap (many small boxes along every ray near
the ground), the merge per cell and model goes in instead, built on the reader thread from the
same prepared references, and its memory is the figure that decides between the two.

---

## 15b. W16 — A mask's soft texels are layers to the eye

**Closes:** *An alpha-blended surface … is cut at alpha 0.5* (`ISSUES.md`).

### What two trials showed

- **Every soft texel a pane** (2026-10-02): at `seyda-neen-pier` the panes went from 3 to 91, and
  52 of 60 pictures turned to grain. The grain was the light: every sun and sky ray through a pane
  is attenuated by it, so every surface under foliage had a noisy shadow.
- **A hashed alpha test for every ray** (the second trial): the fringe's error against the converged
  blend fell at the pier (8.55 → 2.37) and rose under the canopy at `seyda-neen-pond` (21.57 →
  23.67, noise mean 1.19 → 1.90, p99 10 → 24). A texel met in one frame and missed in the next is
  a different surface to the eye each frame, so the accumulator dropped its history there.

Both failed by what the coverage did to rays other than the one the rasterizer blends for, and by
making the eye's surface change between frames. The rasterizer blends a soft texel into the
picture and nothing else: its shadow casters are alpha tested at a half (`shadowcasting.frag`,
"this replaces alpha blending").

### Target shape

**A mask's soft texel is a layer to the eye ray, and a cut to every other ray.** Deterministic, so
the surface identity is the same every frame, and limited to the one ray that draws the picture:

1. **Which texels.** A blended mask (`isBlended() && !isTranslucent() && !isAdditive()`) carries
   `MATERIAL_SOFT_MASK`. At the eye's candidate, the cone-filtered alpha `a` decides: `a ≥
   SOFT_SOLID` (254.5 / 255) is the surface, `a < SOFT_EMPTY` (0.5 / 255) is passed, and between
   them it is a layer of opacity `a × material opacity`. Every other ray keeps the cut at a half.
2. **The eye's traversal** stops on the layer as it stops on a pane today, and the existing peel
   (`PEEL_LAYERS`, four) composites it over what is behind, in the pane channels, with the pane
   filter. `answerPane` takes the soft texel through the same `shadePane`, which is the leaf's own
   light: its sun ray and its lamps are the leaf's, and the light through it is a cut's.
3. **The budget.** A ray that peeled four layers takes the next soft texel as a cut, as a pane
   past the budget is taken today; foliage in front of foliage keeps its nearest four.
4. **The surface behind** is the G-buffer surface, unchanged from frame to frame, so the
   accumulator's history holds through a canopy.

### Implementation

`scene.h` (`MATERIAL_SOFT_MASK`, `SOFT_SOLID`, `SOFT_EMPTY`), `scenebuffers.cpp` (the bit),
`traversal.glsl` (`candidateStops` takes the eye's literal `detailed`, which already says the ray
draws the picture, and answers *layer* for a soft texel), `visibilityhit.rchit` (`answerPane` for a
soft texel), `visibility.rgen` (no change: the peel loop takes any pane answer).

### Measurement before it goes in

Release, hot card, back to back: the trace zone at `seyda-neen-pond`, `seyda-neen-pier` and a
Bitter Coast canopy, before and after; `noise --strafe=150` at the same three; the fringe's error
against 1 000-frame references taken under the rasterizer's rule. **Accepted** where the fringe's
error falls at all three, the frame's noise does not rise, and the trace zone grows by less than a
tenth. Where the cost is over, the layer's light is taken from the leaf's own surface behind the
fringe and not shaded again — a design of its own, measured the same way.

---

## 15c. W17 — One row per texture format

**Closes:** *A texture the engine loads in a format the reader does not name …* (`ISSUES.md`).

### What is wrong

A format is spread over six switches: `readFormat` and `nameOf` (`texels.cpp`), the layout
(`texturedata.hpp`), the widening on the way in (`imagedescription.cpp`), the alpha and colour
decoders (`alphaimage.cpp`), and the device's format (`rtxvulkan/device/memory/formats.cpp`). A
format the content files use and no switch names is `Unnamed`: the grey stand-in, and a sky deck
left out, where the rasterizer samples it. Alpha-only `A8`, the half and full float formats and
BC4 are those today. BC6H and BC7 never load at all (OSG's DDS reader refuses them), so they are
no reader's question.

### Target shape

**One table, one row a format**, in the core (`components/rtx/image/formattable.hpp`):

```cpp
struct FormatRow
{
    GLenum mPixelFormat;          // the image's pixel format
    GLenum mDataType;             // GL_NONE for a block
    TextureFormat mFormat;        // the trace's name for it
    std::string_view mName;       // for a log and a refusal
    TexelLayout mLayout;          // block side and bytes
    Widen mWiden;                 // None, or the RGBA8 / RGBA16F it is laid as on the way in
    Decode mDecode;               // how the host reads a texel: Unorm8 channels, Half, Float, Bc1..Bc5
    bool mColour;                 // whether a colour slot takes it
};
inline constexpr std::array sFormats{ ... };
```

`readFormat` finds the row; `nameOf`, the layout, the widening and the decoders read it. The
backend keeps its own map from `TextureFormat` to `VkFormat`, because Vulkan is the backend's, and
a `static_assert` over the table holds that every row whose `mWiden` is `None` has a device
format.

### The rows the issue adds

| File | Pixel format, type | Row | On the device | Host decode |
|---|---|---|---|---|
| alpha-only | `GL_ALPHA`, `GL_UNSIGNED_BYTE` | `Alpha8` | widened to RGBA8 `(0, 0, 0, a)`, as GL samples it | `a` |
| half float | `GL_RGBA`/`GL_RGB`/`GL_LUMINANCE`, `GL_HALF_FLOAT` | `Rgba16f` | `R16G16B16A16_SFLOAT`, a three- or one-channel file widened | half |
| full float | the same, `GL_FLOAT` | `Rgba32f` | `R32G32B32A32_SFLOAT`, widened the same way | float |
| BC4 | `GL_COMPRESSED_RED_RGTC1_EXT` | `Bc4Unorm` | `BC4_UNORM_BLOCK` | the red channel's block |

**A decision for you: BC4 in a colour slot.** GL samples it as `(r, 0, 0, 1)`, red. A colour slot
refuses BC5 by name today rather than drawing it yellow. Recommended: the same rule for BC4, so a
data slot takes it and a colour slot names it.

### Tests

The table against itself (every row has a layout, a name and a decode; no pixel format and type
twice), each new row's host decode against hand-written texels, and a GPU probe that samples each
new format's upload and reads back what GL would show, to the byte.

---

## 15d. W18 — The harness's folder is the build tree's

**Closes:** *On macOS the harness's folder, `rtxtool/`, stands inside the app bundle* (`ISSUES.md`).

### What is wrong

`Rtx::harnessDirectory(resources)` is `resources/../rtxtool`. On Linux and Windows the resources
are in the build tree, so the harness's folder is too. On macOS the resources are the bundle's
(`RTX_RESOURCES_ROOT` is `Contents/Resources`), `openmw-rtxtool` itself is built into
`Contents/MacOS` (`CMAKE_RUNTIME_OUTPUT_DIRECTORY`), and the bundle is installed whole, so neither
the resources nor the executable is a place to derive it from.

### Target shape

**One CMake variable names it, outside any bundle**: `RTX_HARNESS_DIR` is
`${OpenMW_BINARY_DIR}/rtxtool` on every system (on Linux and Windows the folder it is today). The
views, the suites, the VFS scripts and the shaders with their source are copied there, and the
harness and the tests are compiled with `OPENMW_RTX_HARNESS_DIR`. The core knows no harness folder:

1. `Rtx::harnessDirectory` goes. `RtxTool::harnessDirectory()` answers the compiled path, for
   `main.cpp`, `hosted.cpp` and the driver cache.
2. `Rtx::shaderDirectory(resources, withSource)` becomes `shaderDirectory(resources)`, the modules
   without source. The harness hands the renderer the folder with source as a path
   (`Setup::mShaderSourceDirectory`, an optional path that replaces `mShaderSource`), so the game
   side asks no harness question.
3. The tests read `OPENMW_RTX_HARNESS_DIR` in place of their walk up from `OPENMW_RTX_SHADER_DIR`.

The harness only ever runs from its build tree — no install carries it, and `omw archive` refuses
a package that holds any of it — so a compiled build path is correct by construction.

### Tests

The shader directory test loses its harness half; `run.cpp`'s suite test reads the compiled
folder; a configure on macOS (CI's `macos` job) lists the bundle and finds no `rtxtool/` in it —
a CI step after the build, as `omw archive` checks the other two systems' installs.

---

## 15e. W19 — The walk visits what can change

**Closes:** *A measured run's host rows move as a whole between runs of one build* (`ISSUES.md`),
with W6.

### Evidence

Six legs of one build at `one-cell-walk`, held to the performance cores at a steady clock: walk
medians 1.02 to 1.53 ms, and the frame thread's cache misses a thousand instructions 3.14 to 4.75,
moving together. A leg is slow from its first frame to its last. The walk reaches every node of
every loaded cell on every frame (`SceneExtractor`'s traversal skips nothing), and an identity met
again is resolved through maps keyed on the node: a frame is a walk of pointers through OSG's heap,
whose layout is set by the order the loader threads finished in, which differs in every process.

### Target shape

**A subtree that cannot change between frames stands, and the walk goes past it.**

1. **The probe first.** Count, per frame at `one-cell-walk` and `balmora-mages-guild`, the nodes the
   walk visits and the share of them under a subtree with no update callback, no controller, no
   `Switch`, `LOD` or `Sequence`, no skin, no particle system and no light: the frozen share. The
   design goes on only where that share is most of the walk.
2. **Frozen at arrival.** Where the walk first meets a subtree, it records whether the subtree is
   frozen (the test above, over the subtree once). A frozen subtree's rows — its placements, their
   materials and their transforms — go into a flat run on the extractor
   (`FrozenRun { std::uint32_t mFirst, mCount; }` over one `std::vector<Index>`).
3. **Passed after.** On every later walk the traversal meets the subtree's root, stamps its run's
   rows as reached in one pass over contiguous indices — the sweep's contract, kept without the
   graph — and does not descend.
4. **Thawed by what moves it.** A frozen subtree changes in three ways the game says: its object
   is moved (`RenderingManager::moveObject`, `notifyJumped`), a child is added or removed (the
   walk sees the root's child count change), or a state set's updater runs on it (`StateSetUpdater`'s
   generation, which the fork already reads). Each drops the run, and the next walk descends
   again.
5. **The rest is W6's**: the rows themselves change by the row, so a frozen subtree costs one
   stamp pass and nothing else.

### Measurement

The six-leg drift at `one-cell-walk`: the walk's median and the cache-miss rate must stay within a
tenth across legs (the acceptance W6 states), and the walk's median must fall. `./omw repeat
--pairs=10`, because a stamp that misses a row is a row swept from a frame that still shows it.

---

## 16. Smaller workstreams

### 16.1 The upstream diff

1. **Debug-only checks on hot paths.** Each `Crash::notNull` on a per-node or per-frame path becomes
   the debug-only form: `NodeCallback::run`, the light manager, the terrain drawable, the MyGUI
   batch, the physics check that replaced upstream's `assert`, and the terrain's water culling
   view (`quadtreeworld.cpp:447`, once per chunk per cull). The five extra warnings stay on for the
   whole tree, and the rules require this whatever their scope.
2. **The SDL3 port's defects:** `androidmain.cpp` back to upstream's, and upstream's
   `GraphicsWindowSDL2` name back. Both take fork lines out of upstream files.
3. **The docs that say false:** `rtx.rst` and `settings-default.cfg` name a `-DOPENMW_RTX` the
   build does not have.
4. **The harness's hooks in upstream classes.** The engine's two frame hooks are the host interface
   (`OMW::EngineHost`) and go into Accepted diff with that reason. The weather hold
   (`World::holdWeather`) and the script boxes (`WindowManager::scriptMessageBox`) leave upstream's
   classes: the host sets the weather through `World::changeWeather` with its own transition, and
   declines script boxes in its own window-manager setup.
5. **The post-processing package tests the renderer once.** `initPostprocessingPackage` registers
   upstream's usertype where there is a chain and an inert one where there is none, so upstream's
   bindings come back unchanged and the null tests go.

### 16.2 Device requirements

*(REVIEW: The device check and the enabled extensions differ …)*

- A subgroup obstacle in `profileOf`: quad operations in the compute stage.
- `Device` enables an option's extensions only after its feature holds, in table order.
- `rayTracingMaintenance1` leaves the requirements, or names its reader.
- **The push-constant limit is a requirement.** One `sPushConstantBytes = 256` beside
  `sApiVersion`; `profileOf` refuses a device below it by name, and `pushRangeOf` asserts every
  constants block against it (the tone pass pushes 200 bytes, the sprite bin 184).

### 16.5 Layering guards

*(REVIEW: The layering's documents and guards lag the code)*

`RtxSourceTreeTest` reads the folder order from `architecture.md`'s table, so the order has one
statement. Move `shaders/` to the top of that table. The test refuses a quoted include with a `..`
component, and refuses `<apps/...>` under `components/rtx` and `components/rtxvulkan`. Then fix the
quoted `"../"` includes in the fork's own `mwrender` files (never upstream's), the two in test
support, and the `#ifdef _WIN32` in `glrenderer.cpp`. The core's prose names no Vulkan object either
(swapchains, descriptor sets, command buffers, `VkInstance`): it states each cost in its own terms,
and the backend's comment carries the Vulkan reason.

### 16.6 Vulkan lifetimes and barriers

*(REVIEW: Vulkan objects are waited on or rebuilt by a side effect …)*

Staging blocks retire through `Retiring<std::size_t>` like every other retired object. The
presenter compares against the extent it was last asked for. The two GUI `finish` calls before a
present-mode change go, or state the reason that remains. A trace pipeline reads each named module
once per build and chains the words (`maintenance5`), and the GUI pass's four pipelines load their
two modules once.

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
in `checks`), `GTEST_FAIL_IF_NO_TEST_SELECTED` in the test preset, `.python-version`. The driver's
usage formats the harness's verbs from `HARNESS_VERBS`. The benchmarks carry a `benchmark` label
that both CI jobs run. One Vulkan headers pin serves all three systems. The release checks for a
successful CI run on its commit.

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
| Smaller defects | each as written |
| Where an image was left … ; Barriers that order nothing … | §16.6 |
| Options a verb takes and does nothing with | an `sPictures` verb set owns the picture-only knobs; every contradictory pair is refused at parse time, and `--accumulate` with an upscaler is refused |

**Tests** follow each workstream. The groups *(Tests that cannot fail …)*, *(Behaviour with no
test)* and *(Test fixtures are duplicated …)* close as each owner is rewritten, with these that
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
so its validation errors are reported.

---

## 18. Order of work

Each phase ends green on `./omw gate`.

1. **Phase 5, frame cost:** W6, W8 steps 1 and 2, W9, the barrier rows of §16.6, and §17's frame
   constants. Each with a `./omw release bench` before and after. A change that does not improve
   the worst frame or the p99 does not go in. W8 step 3 follows its own measurement.
2. **Phase 6, the upstream diff:** §16.1, then §16.2, §16.5 and §16.7.
3. **Phase 7, tests and docs:** the test groups in §17, and every doc item, `architecture.md` §1 and
   §13 included.
4. **Phase 8, the open issues:** W15 (the test plugin first), W16 behind its measurement, W17.
   W18 goes with Phase 6, because it moves what the harness reads; W19's probe goes with Phase 5,
   and its frozen subtrees with W6.

W14 goes as each input arrives.

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
