# Open issues: root causes and structural fixes

This plan answers every entry of `.notes/ISSUES.md` as it stood on 2026-10-03. For each issue it
gives the root cause, the evidence for it, the structural fix, the steps, and the test that holds
the fix. Where the code alone did not show the cause, an experiment did; the experiment and its
numbers are stated, and its records are in the scratch folder of the session that wrote this plan.

Three entries of `ISSUES.md` state a cause that the investigation did not confirm. This plan
corrects them (§1.1, §1.2, §4.1), and `ISSUES.md` takes the corrected cause when the fix lands.

`REDESIGN.md` stays the plan for the review's structural work. Where an issue here is an item of
`REDESIGN.md` (groundcover, BC7, Night-Eye's D8), this plan adds the root cause and removes the
block that held the item, and `REDESIGN.md`'s item points here.

## Contents

| § | Issue (`ISSUES.md`) | Root cause, one line | Size |
|---|---|---|---|
| 1.1 | GPU stall of about 1.5 ms | The card time-slices with the compositor, the browser and the editor | S |
| 1.2 | Host rows move between legs | Cache misses of the per-frame walk, set by the heap layout; and efficiency cores | M |
| 2.1 | `Ambient` vertex colour read as `Tint` | The trace keeps one reflectance; the rasterizer keeps two | M |
| 2.2 | Night-Eye metered away | A view term is added as a world light | M |
| 2.3 | `tws` keeps the hidden statics' lamps | A lamp row carries no class | S |
| 2.4 | `tws` keeps the cell borders | The debug walk does not cull by the view's mask | S |
| 2.5 | A content file's clockwise front is dropped | The reader drops the front-face state instead of composing it | S |
| 2.6 | Blended fringes and soft regions cut at 0.5 | Coverage is a fixed threshold, not a probability | M |
| 2.7 | Rendering rays miss the ring's statics | Nothing answers a CPU ray for what only the trace holds | M |
| 2.8 | Groundcover not drawn | No reader; the block is a missing test plugin, which the tree can generate | L |
| 2.9 | BC4, BC6H, BC7 draw grey | A closed format list, and no host decoder for the facts | M |
| 3.1 | `shadow.h` admits a shift by 32 | A low-bits mask built by a shift that is undefined at the word's width | XS |
| 4.1 | `report-under-hang` can hang | The test waits with no bound for a state that lasts a few ms | XS |
| 4.2 | A fault during a report leaves a mislabelled dump | Two writers of one exception record, with nothing ordering them | S |
| 4.3 | The harness starts with the catcher off | A default whose reason is gone | XS |
| 5.1 | The driver takes macOS for Linux | The system is a two-way test, not a table | S |
| 5.2 | `Tests.cmake` on Apple | A test's folder comes from a variable set in one branch only | XS |
| 5.3 | Narrow path spellings on Windows | The environment call takes `const char*`, so callers narrow their paths | S |
| 5.4 | The install ships harness and test files | The install copies working folders with a deny-list | M |
| 5.5 | The gate does not link every program | The gate builds a hand-picked target list | XS |
| 5.6 | Tests past one second | Parallel suites, and two tests that do more than their tolerance needs | S |

Sizes: XS under an hour, S a day, M a few days, L a week or more.

---

## 1. Measurement and the runtime environment

### 1.1 The GPU stall is time-slicing with other processes

**Evidence.** One bench leg at `balmora-mages-guild` under Nsight Systems with GPU context
switches traced (`nsys profile --gpuctxsw=true`):

| Process | Slices | Time on the card in 7.5 s |
|---|---|---|
| `kwin_wayland` | 919 | 1 187 ms |
| Brave's GPU process | 917 | 405 ms |
| `zed-editor` | 216 | 234 ms |

- 836 of our 5 323 GPU workloads (15.7%) have another process's slice inside them. The time
  added is 0.34 ms at the median, 1.38 ms at the 90th percentile, and 3.2 ms at most.
- **Every one of the longest 2% of workloads** holds such a slice. Without it they fall from
  5.7 ms to 4.0 ms on average.
- That explains every fact in the issue: the stall lands in whichever pass runs, it is about
  1.5 ms, and a headless run keeps most of it, because the compositor, the browser and the editor
  draw whatever the harness does.

**Root cause.** The device schedules contexts by time slice, and our queue has the same priority
as the desktop's.

**Fix.**
1. **The renderer asks for a high-priority queue** where the device offers one. This card offers
   `VK_KHR_global_priority` and `globalPriorityQuery`. `Device` queries the family's priorities
   (`VkQueueFamilyGlobalPriorityPropertiesKHR`), asks for `HIGH`, and takes `MEDIUM` where
   creation answers `VK_ERROR_NOT_PERMITTED` (a Linux driver can refuse `HIGH` to a process without
   `CAP_SYS_NICE`). It is
   an optional extension in `requirements.cpp`, and `info` prints which priority the queue got. It
   is the game's fix as well as the harness's: a windowed game composited by KWin has the same
   preemption.
2. **The bench says when the card was shared.** The card watch already samples which processes hold
   the card. The report adds one line per place: the share of measured frames with another process
   active on the card (NVML `nvmlDeviceGetProcessUtilization` on NVIDIA, `amdgpu`'s fdinfo on AMD).
   A leg with sharing is marked, as the clock is marked today.
3. **A/B decisions read zone medians and p99, not means.** A slice adds time to a few frames. The
   mean takes all of it, and the median takes almost none. `AGENTS.md`'s measuring rule names the
   statistic.

**Verification.** The same `nsys` record before and after step 1: the share of our workloads with
another process inside them, and the 98th percentile of workload time. A bench A/B on a busy desktop
(Brave and Zed open).

**Risk.** A high-priority render queue can make the desktop stutter while the game runs in a window.
The bench measures KWin's frame time beside ours with both priorities.

**Outcome (measured on this box).**
- The driver refuses `HIGH`: `vkCreateDevice` returns `VK_ERROR_NOT_PERMITTED` to a process
  without `CAP_SYS_NICE`. A player's game does not have it, and `kwin_wayland` does
  (`cap_sys_nice=ep`). So step 1 changes nothing here, and it is not built: no device where it
  is accepted (Windows, or a process with the capability) is at hand to measure the desktop's
  stutter against.
- Step 2 was there already, but its line claimed more than its source sees. NVML's process samples
  count shader-core work. In a run where `nsys` showed KWin take the card 789 times for 1.48 s, all
  28 samples were ours. The `card` line now says "ran no other process's work", and `CardShare`
  and `AGENTS.md` say that a compositor's slices are not in it.
- Step 3 is in `AGENTS.md`: an A/B reads medians and the p99, the zones' from `--json`.
- The issue is closed as the environment's: the share of our workloads with another slice inside
  was 11.9% and 11.6% in two legs with Zed and KWin alone.

### 1.2 Host rows move with the walk's cache misses and with the core it runs on

**Evidence.** `one-cell-walk`, release, legs back to back:

- **Unpinned**, four legs under `perf stat`: walk medians 1.58, 1.17, 1.67, 1.51 ms. The share of
  the process's instructions run on the efficiency cores (CPUs 16–31) moved from 32.8 to 55.3
  billion between legs.
- **Pinned to one thread per performance core** (`taskset -c 0,2,…,14`), alternated with pins to
  all of 0–15: walk medians near 1.0 ms in both. One pinned leg drifted to 1.46 ms. So hyperthread
  sharing is not the cause, but core type is a large part of the unpinned spread.
- **Six pinned legs under `perf stat`**: the clock was steady (4.86–5.15 GHz, and the slowest leg
  ran at 5.01 GHz). The cache-miss rate tracks the walk time:

| Leg | Walk median | Misses per 1 000 instructions | IPC |
|---|---|---|---|
| 1 | 1.02 ms | 3.14 | 1.65 |
| 3 | 1.03 ms | 3.36 | 1.59 |
| 6 | 1.07 ms | 3.50 | 1.63 |
| 5 | 1.16 ms | 4.11 | 1.59 |
| 4 | 1.23 ms | 3.64 | 1.61 |
| 2 | 1.53 ms | 4.75 | 1.53 |

**Root cause.** Two causes, of different weight:

1. **The OS places the game's threads on either core type.** An efficiency core runs the walk about
   half as fast. The issue's `taskset -c 0-15` left this cause out, but it also left the second.
2. **The per-frame walk chases pointers through the OSG graph**, and the graph's heap layout is set
   by the order in which the loader threads finished. That order differs in each process. A layout
   with worse locality costs up to half again, from the first frame of a leg to its last, which is
   why a leg moves "as a whole".

**Fix.**
1. **The report states each leg's machine state.** The harness's instruments already own perf's
   FIFO. A leg opens per-thread counters for the main thread with `perf_event_open`: cycles,
   instructions, cache misses and the core type of each sample. The report prints them beside the
   host rows: clock, IPC, misses per 1 000 instructions, and the share of time on efficiency cores.
   A drift is then visible and named, which closes the issue's "nothing in the report says which
   state a leg ran in".
2. **The harness keeps its frame thread on the performance cores** where the system has two core
   types: an affinity mask from `/sys/devices/cpu_core/cpus` on Linux, and the CPU set from
   `GetSystemCpuSetInformation`'s efficiency class on Windows. The game makes the same choice by a
   setting that defaults to the system's own choice.
3. **The walk stops depending on heap layout.** This is `REDESIGN.md`'s W6 and W9 (tables changed by
   the row, passes over what is new): the mirror keeps its per-frame inputs in contiguous tables
   that change only where a node changed, so a frame reads arrays and not the graph. This plan adds
   the measurement that proves it: after W6 and W9, the misses per 1 000 instructions of the walk
   must stay within 10% across six legs.

**Verification.** Six legs, pinned, before and after step 3: the walk median's spread across legs,
and its correlation with the miss rate.

**Outcome of steps 1 and 2.**
- Step 1: `ThreadCounters` counts the frame thread over each place's measured frames, one group
  on each kind of core, and the report's `host thread` line gives the clock, the instructions a
  cycle, the cache misses a thousand instructions and the share on efficiency cores. The record
  carries the counts as `thread`.
- Step 2: `Platform::Process::keepToPerformanceCores`, called by the harness before any thread of
  the run. Eight legs of `one-cell-walk` in turn: the walk's p99 read 1.75 to 1.85 ms in three of
  the four pinned legs and 2.42 to 2.96 ms in the four unpinned. The medians moved less, 1.05 to
  1.11 against 1.08 to 1.39 ms. The game keeps the system's choice: a setting for it would change
  upstream's settings for a measurement only the harness takes.

---

## 2. Light transport and parity with the rasterizer

### 2.1 Two reflectances, as the rasterizer has

**Root cause.** The rasterizer lights a fragment as `texture × (D × lit + A × ambient + E)`, with
the diffuse colour `D` and the ambient colour `A` as separate reflectances
(`files/shaders/lib/material/vertexcolors.glsl`). Under `ColorMode_Ambient` the vertex colour
replaces `A` alone. The trace keeps one reflectance, `D` (`MaterialResolver` decodes `A` only for
the ambient override). So `vertexColourOf` has no target for `Ambient`, and it falls into `Tint`,
which replaces `D`.

**Fix.** The trace keeps both reflectances, with the same split of light as the rasterizer:
- **Direct light** (the lamps, the sun, the moons) is the rasterizer's `lit` term, and takes `D`.
- **Indirect light** (the traced bounce and the path end's ambient) is the rasterizer's `ambient`
  term, and takes `A`.

The steps:
1. **`VertexColour` gains `Ambient`**, and `vertexColourOf` maps `ColorMode_Ambient` to it.
   `AmbientAndDiffuse` stays `Tint`.
2. **`GpuMaterial` carries the ambient ratio `A / D`**, per channel, where `D` is not nought, and
   one where it is or where they are equal. A material bit says that the vertex colour replaces `A`.
3. **The surface's response gets a second albedo.** `SurfaceResponse::mDiffuse` is what the
   composite multiplies the bounce by (`CHANNEL_ALBEDO`). It becomes `albedo × A / D`, so the
   bounce takes `A`. `pathEnd` at a bounce's far hit takes the same ratio. The direct terms keep
   `albedo`.
4. **A census first.** Count the materials in vanilla content and in the PBR packs where `A ≠ D`,
   and the meshes under `ColorMode_Ambient`. The census decides whether step 3 waits for its own
   `shot --against`, because every such material's picture moves.

**Tests.** A GPU test: a floor whose material has `A = 0.5 D`, under one lamp and an ambient,
read in both channels. The direct channel holds `D × lamp`, and the indirect channel's remodulation
holds `A`. A host test of `vertexColourOf` over all six modes.

**Census (vanilla Morrowind, Tribunal, Bloodmoon).**
- `ColorMode_Ambient` comes only from an OSG model's own material (`SceneManager`'s
  `fromOSGColorMode`); no NIF sets it, so vanilla content has none.
- Of 19 416 `NiMaterialProperty` records, 2 758 (14.2%) have an ambient that differs from the
  diffuse, worn by 3 295 of 34 567 shapes (9.5%) in 871 files. Most differ by a few per cent
  (A/D 0.95–0.99), but 919 materials have A/D under a half: 538 at 0.10, 136 at 0.33, 68 at 0.18,
  38 at nought. They are whole interiors — the Redoran, Telvanni, Daedric and Vivec halls — and the
  hair meshes.

**Decision.** Step 3 as written multiplies *all* indirect light by A/D, so those halls' bounce
falls to a tenth. The rasterizer's ambient is the cell's flat fill and nothing else; the trace's
indirect also carries lamp light bounced off other surfaces, which the rasterizer does not have and
which a physical reflectance returns by D. The user chose A for the fill and the path end only, and
D for bounced lamp light.

**Outcome.**
- The material carries `A` itself (`GpuMaterial::mAmbientColour`) and not a ratio, and the surface
  has `mAmbientAlbedo` beside `mAlbedo`. A path end reflects its `pathEnd` by `A`.
- The eye's bounce is split: `CHANNEL_INDIRECT` keeps the whole bounce, and `CHANNEL_FILL` holds the
  share that is fill (the sky a ray escapes to, and the far hit's `pathEnd` by its `A`).
  `CHANNEL_AMBIENT_ALBEDO` holds the eye's `A`. The accumulator and the cascade filter the fill by
  the whole bounce's weights, and the composite adds `D × S + (A − D) × F`.
- Where `A = D` the picture is the same to the bit: the trace's bounce channel matched the last
  commit on all 248 frames of `shot --views=all`, once the joined sum was written as an explicit
  `fma` (the pinning fuses a product that only one add reads, and the fill is read twice). Three
  pictures moved past the denoiser's run-to-run noise: an arm in `seyda-neen-customs` (10 of 255 on
  0.15% of the pixels), flowers at `dagon-fel` (13 on 0.03%), and one pixel of `ald-ruhn-map`.
- Cost, release, three legs each, frame medians: `seyda-neen-ship` 5.95–6.09 to 6.20–6.35 ms,
  `balmora-mages-guild` 4.55–4.59 to 4.90–4.98 ms, `vivec` 5.86–5.87 to 6.17–6.20 ms — about 0.3 ms
  a frame. The p99 moved with it at the guild (5.14–5.24 to 5.53–5.69 ms) and at `vivec` (6.67–7.31
  to 7.08–7.17 ms). It is the second filtered signal: the accumulator and the cascade read and write
  three more full-float images, the composite reads two more channels, and the payload is 23 words.
- Tests: `theFillIsReflectedByTheAmbientAlbedoAndALampsBounceByTheDiffuseOne` (a floor with
  `A = 0.5 D` under a sky and in a room: the picture is half the other floor's to the bit, unfiltered
  and filtered, and a lamp's bounce off the lid is no fill), and the vertex colour test reads both
  albedos under every mode.

### 2.2 Night-Eye is a view term, added after the meter

**Root cause.** In the game, Night-Eye adds `0.7 × magnitude` to the ambient of every lit fragment.
Nothing occludes it, and the rasterizer has no exposure to adapt it away. The trace adds the lift to
the cell's fill (`makeRoomLight` and the exterior ambient), so it becomes a world light:
- it reaches the eye only through a bounce and a path end, which geometry occludes twice;
- and the meter, which reads the upscaled frame, adapts most of it away.

**Fix.** Night-Eye leaves the world's light and becomes a term of the view, in display-referred
light after the exposure, as the rasterizer applies it:
1. **`WorldState::mNightEye` stops adding to the fill and the ambient.** `describeWorld` carries it
   to the frame's look as a colour.
2. **The tone pass adds `albedo × lift × A`** to the exposed frame before the curve. That is the
   rasterizer's `ambientColor × lift × texture`, with `A` from §2.1. The meter never sees it, so
   nothing adapts it away.
3. **The albedo at the output extent:** without an upscaler, the albedo channel itself. With one,
   the albedo at the traced extent, read bilinearly at the output pixel's place. The lift term then
   has the traced extent's texture detail, which is the cost of this shape. The step measures it:
   a `shot` at `quality` with the lift, against the same view at `native`.

D8's measurement stays the acceptance: the brightness ratio with and without the lift, at magnitudes
25 and 100, in a cave and at `seyda-neen-ship` at night, against the rasterizer's ratio.

**Tests.** A GPU test: a uniform floor and a known lift. The shown value rises by exactly
`albedo × lift` after the exposure, and the measured exposure is the same with and without the lift.

**Outcome.**
- The lift leaves the world's light, indoors and out (`makeRoomLight` takes no lift, and the sky's
  ambient is read less `WorldState::mNightEye`), and reaches the tone pass through
  `FrameOptions::mNightEye`. The rasterizer keeps its own ambient with the lift, as before.
- **Added after the curve, in display values, and not before it.** The rasterizer adds
  `texture × A × lift` to every lit fragment in the values it displays, so the display increment is
  that product whatever else lights the fragment; added before the curve, it would shrink wherever
  the picture is bright. The same place the glare fader is added.
- `CHANNEL_LIFT` (a byte a channel) holds the encoded ambient albedo of each lit thing the pixel
  shows by its share: the layers by what reaches the eye of each, the surface by the path's
  transmittance, and the water's two rays by their Fresnel shares and their legs' media. The tone
  pass reads it bilinearly at the place each shown pixel shows, the jitter taken off where the
  upscaler reconstructed the picture. Behind the puffs as the backdrop is.
- Measured, mean shown byte against magnitude 0, 25, 100 (debug, `--upscale=off`; the harness's new
  `--night-eye` puts the effect's base on the player):
  - `addamasartus` (a cave): before 60.6 → 62.1 → 65.5 (1.02×, 1.08×: the meter took it back);
    after 60.6 → 69.1 → 93.9 (1.14×, 1.55×).
  - `seyda-neen-ship` at 23:00: before 24.4 → 36.9 → 63.4 (1.51×, 2.60×: outdoors the lift was an
    ambient under the exterior's bias); after 24.4 → 30.1 → 47.3 (1.24×, 1.94×).
- **The rasterizer's own ratio was not measured**: the harness has no rasterizer, and the game window
  is not opened. The increment is the rasterizer's by construction. The ratio is lower than the
  rasterizer's wherever this renderer's unlifted picture is brighter, which the exposure makes it in
  the dark: the same increment over a brighter base.
- At `quality` against `native`, the added lift agrees to 0.1 of a byte on average at the cave (33.3
  against 33.2), and differs by 2.9 a pixel: the traced extent's texture detail, which is this shape's
  cost.
- With no Night-Eye, no picture of `shot --views=all` moved past the denoiser's noise, and every
  channel before the denoiser matched the last commit. The composed frame's hash moved on 173 of 248
  frames, and on 168 between two runs of this build: the denoiser's known run-to-run difference
  (`architecture.md`), more frequent than before the change.

### 2.3 A lamp carries its owner's class

**Root cause.** The rasterizer's light manager collects a light only from a node its cull reaches,
so `tws` darkens the lamps of the statics and objects it hides. A `GpuLight` row carries no class,
and the lamp walk (`weighLamps`, `darkeningAt`, the fog's `lampsInAir`) takes every lamp in the grid,
whatever the frame's ray mask.

**Fix.**
1. The light extractor knows the reference a light hangs under. `GpuLight` takes its class bit
   (`classBit(InstanceClass)`), packed beside `mFill` in one word, so the row stays its size.
2. The three lamp walks test `(lamp.mClass & frame.mRayMask) != 0` before they weigh a lamp. That is
   a bit test on a row already loaded, so a frame with no toggle pays one AND per lamp.
3. The light grid is unchanged: a mask change is a console command, and a test in the walk serves
   every camera, the map's and the pictures' included, with no grid per mask.

**Tests.** A GPU test: a lamp placed as a static's light and one as an actor's, under a mask with and
without the static class. The first goes dark and the second stays.

### 2.4 The debug walk culls by the view's mask

**Root cause.** Upstream hangs the cell borders under the terrain root (`Mask_Terrain`), so a cull
without the terrain class drops them. `DebugWalk` traverses with `Mask_Debug` alone, so it never
enters a node of another class. For that reason `TracedTerrain` hangs the borders under the world
root, where `tws` cannot reach them.

**Fix.** `DebugWalk` traverses with the view's own mask, `worldViewMask()`, as the rasterizer culls,
and keeps only drawables under `Mask_Debug`. `TracedTerrain` hangs the borders under a group with
`Mask_Terrain`, as upstream does. Any debug geometry under a hidden class then goes with it.

**Tests.** A host test of `DebugWalk` over a small graph: a `Mask_Debug` line under a
`Mask_Terrain` group, walked with and without the terrain bit.

### 2.5 The front face composes with the placement's mirror

**Root cause.** The rasterizer shows the face that `FrontFace` names, in window space, so a placement
with a negative determinant reverses it. `SceneUtil::attach` builds a left body part under a scale of
−1 and states `CLOCKWISE` to undo that. Traversal reads the winding in the mesh's own space, so it
ignores the placement's determinant but keeps a mirror that the skinning applied to the vertices.
The reader therefore drops `FRONTFACE` (`surface.cpp`), which is right for a rigid mirrored part and
wrong for a content file's own `NiStencilProperty` and for a skinned mirrored part.

**Fix.** One rule, `shown face flipped = (state is CLOCKWISE) XOR (placement is mirrored)`:
- a rigid left part: clockwise and mirrored, so no flip;
- a skinned left part: clockwise, and the placement is not mirrored (the mirror is in the skin), so
  a flip;
- content's own clockwise face: clockwise and not mirrored, so a flip.

The steps:
1. The reader keeps `FRONTFACE` as `SurfaceDescription::mClockwise` and stops dropping it.
2. `InstanceRecord` keeps whether the placement's transform has a negative determinant, computed
   where the row's transform is written.
3. `SceneAcceleration::placeRow` sets `VK_GEOMETRY_INSTANCE_TRIANGLE_FLIP_FACING_BIT_KHR` when the
   two disagree. The comment there that refuses the bit states the rule instead.

**Tests.** The existing `aMirroredPlacementShowsTheFaceItsMeshShows`, plus three GPU cases: a
clockwise material unmirrored, the same mirrored, and a mirrored skinned mesh. Each asserts the face
a back-face-culling eye ray meets.

**Outcome.** The rule holds, but its first home was wrong. Read into the material, the clockwise
front reached the right body parts too: a left part and a right one share the state set a material
is keyed on, and `SceneUtil::attach` hangs the front above the left one alone, so the guard's right
greave at `seyda-neen-pier` showed its inside. The front is now resolved down the walk's chain of
state sets (`Shading::mClockwise`, with the stack's `OVERRIDE` and `PROTECTED`) and kept on the
placement (`MeshInstance::mClockwise`), as the fade is. `SceneUtil::attach` mirrors no skinned part,
so the plan's skinned case does not arise in vanilla content. No view moved, and the walk's median
did not move beyond the drift between legs.

### 2.6 Coverage is a probability

**Root cause.** `candidateStops` decides a blended texel by `alphaPasses(…, painted, reference)`
against a fixed reference: `sBlendCutoff` for a mask, `sPaneCutoff` for a pane. So any coverage
between nought and one is quantised to nought or one. The rasterizer composites the fraction:
`objects.frag` blends every texel at its alpha. A soft fringe becomes a hard edge, and a soft region
in a texture that reaches solid somewhere loses everything under 0.5.

**Fix.** For a blended mask (not a pane, not a medium), the candidate stops where
`painted × opacity > u`, with `u` a draw in `[0, 1)` per ray and per candidate. The mean over draws
is the blend's own coverage, so the fringe and the soft regions converge to the rasterizer's
picture, and a ray costs the same as today.
1. **The draw.** Wyman and McGuire's hashed alpha test (2017): a hash of the candidate's position at
   the texture's scale, so the pattern stays on the surface under motion, mixed with the frame for
   the eye's rays, so the accumulator and the upscaler average it. Shadow rays and bounces take a
   per-ray draw, as their other draws do.
2. **One rule for every ray.** `candidateStops` takes the draw from its caller, so the eye, a shadow
   ray and a bounce agree on what a texel covers on average.
3. **The pane path stays** for a texture that never reaches solid (glass, lantern panes). Its light
   needs the attenuation that a stochastic hit would turn to grain.

**Measurement before the decision.** The 2026-10-02 trial turned every fringe into a pane and grained
52 of 60 pictures. This fix adds no ray, but it adds sampling noise at fringes. So it is accepted only
by measurement:
- `noise --strafe=150` at `seyda-neen-pier` and in a canopy view: the frame's noise may not rise
  past the reference's own error;
- `shot --against`: the fringes must move toward a 1 000-frame reference.

**Outcome: measured and declined.** Built as written — a material bit for a blend's mask, a draw
hashed from the placement, the diffuse's texel and the frame, held by every ray — and measured
against 1 000-frame references taken under each rule:
- `seyda-neen-pier`: the fringe's error against the converged blend fell from 8.55 to 2.37 (mean of
  a byte over the 1.0% of pixels the two rules part on), and the frame's noise did not move.
- `seyda-neen-pond`, under the canopy: the fringe's error rose from 21.57 to 23.67, the frame's
  noise mean from 1.19 to 1.90 and its p99 from 10 to 24. The leaves came out grained: a texel met
  this frame and missed the next is another surface each frame to the eye, so the accumulator's
  history is rejected there instead of averaged.

The draw itself converges (the two GPU tests, rewritten for it, held each seam column to its alpha
over 256 frames); what fails is the eye's reprojection over a stochastic primary surface. A future
attempt needs the coverage kept out of the eye's surface identity, or applied to the shadow and
bounce rays alone. The issue stays open.

### 2.7 The ring answers the game's rays for what it stands

**Root cause.** `RenderingManager::castRay` walks the OSG graph. Past the loaded cells, the
rasterizer's paged statics are in that graph with their reference numbers. Under the trace, the
ring's statics live only in the trace's tables. The ring's ground already answers through
`TracedTerrain::meet`, but nothing answers for its statics.

**Fix.** The pattern that answers for the ground answers for the statics:
1. `CellRing` keeps, per standing cell, its placed statics: the template node and the
   placement's matrix and reference number. The step first reads what the ring holds once a row is
   placed, and keeps the rest in the cell's own record, refilled in place as the ring's buffers are.
2. A node under the terrain root, as `meet`'s is, hands the intersection visitor to the ring. For
   each static whose bounding sphere the segment crosses (a per-cell list, nearest cell first), it
   carries the segment into the template's space, runs the template's `KdTree`, and inserts each
   hit with its reference number, as an object pager's hit is inserted.
3. The statics are not added to the graph: the mirror's walk would read them as loaded content.

**Tests.** A host test of the ring's ray answer against a synthetic cell: a box static four cells
out. The cast meets it at the hand-computed distance, with the placement's reference number.

### 2.8 Groundcover: the tree generates the plugin it waits for

**Root cause.** The ray tracer has no reader for groundcover. `REDESIGN.md` W14 holds the design: one
merged mesh per cell and plant model, from `GroundcoverStore`, under the game's density rule. The
work waited for a groundcover mod to check it against.

**Fix of the block.** The tree writes its own plugin: a test fixture made with
`ESM::ESMWriter`, which declares vanilla flora statics (`flora_*` models from Morrowind.esm) as
groundcover in two cells near Seyda Neen, at known positions. The harness loads it through
`groundcover=`, as a player's `openmw.cfg` does. That checks the reader against the rasterizer's own
placement of the same records, with no third-party content. Then W14's groundcover item is built as
designed, and checked twice: against the fixture, and against a real mod where one is installed.

**Tests.** The fixture's cells under both renderers: `shot` of the same view, and the count of
placed plants against `GroundcoverStore`'s count at the same density.

### 2.9 Every block format the device samples, and a decoder for the facts

**Root cause.** `readFormat` maps a closed list of GL formats. BPTC (BC6H, BC7) and RGTC1 (BC4) are
not in it, so a texture in one is `Unnamed` and draws as the grey stand-in. The upload path could take
them, because the Vulkan formats are core on every target device. The host must also read a colour
texture's facts (the mean, whether alpha reaches solid) and the contact sheet, and it has no decoder
for these formats.

**Fix.**
1. **`readFormat` and `TextureFormat` take BC7 and BC6H** (colour and data), **and BC4** where a
   slot is one channel. A colour slot refuses BC4 by name, as it refuses BC5.
2. **A BPTC decoder in `components/rtx/image`**, written from the Khronos Data Format Specification
   (BPTC section, CC BY 4.0), which publishes the partition and anchor tables. The tables carry
   their licence note in `files/licenses/`. Mesa's MIT decoder is the cross-check, not the source.
3. **The facts decode the levels they need.** The mean comes from the coarsest level that holds
   whole blocks, and the solid reach comes from the alpha of the first level (BC7 modes 4–7; BC6H
   has no alpha).

**Outcome: not built.** No BC6H or BC7 file reaches either renderer. OSG 3.6.5's DDS reader, and
OpenMW's fork of it at the pinned commit (`extern/CMakeLists.txt`), refuse every DX10 format but BC4
and BC5: "unhandled DX10 pixel format 0x62 in dds file, image not loaded". The image is null before
`readFormat` is asked, in the game as in the rasterizer. BC4 loads, through `ATI1` and the fork's
DX10 path, but no map the trace reads has one channel. What the issue still holds is narrower, and
`ISSUES.md` now says it.

**Tests.**
- The decoder against the device, the tree's rule for a second implementation: upload a BC7 texture
  with every mode and partition, sample each texel in a compute probe, and compare with the host's
  decode bit for bit.
- A `shot` of a view that wears a BC7 replacer.

---

## 3. Shader integer safety

### 3.1 A low-bits mask that is defined at the word's width

**Root cause.** `shadowtiles.comp:206` builds a mask of `n` low bits as `(1u << n) - 1u`, which is
undefined at `n = 32`. `shadow.h` asserts `SHADOW_WORKGROUP + 2 * SHADOW_REACH <= 32`, so the
assert admits the undefined case. A search of every shader finds no other shift whose amount can
reach 32 (the other two are bit indices below 32 by construction).

**Fix.** A shared function `lowBits(uint count)` in a portable header, `~0u >> (32u - count)`, which
is defined for `count` from 1 to 32. It has a debug assert for nought on the host, and the call site
documents why its count is never nought. `shadowtiles.comp` calls it, and the assert keeps `<= 32`,
which is now true.

**Tests.** A host test of `lowBits` at 1, 8, 24, 31 and 32, against hand-written masks.

---

## 4. The crash catcher

### 4.1 The `report-under-hang` mode waits with no bound

**Correction to `ISSUES.md`.** The entry points at Crashpad's POSIX client. The CI artifact of run
36963270110 shows a simpler cause:
- the report began 7 ms after the start, and its summary is stamped at once (04:30:26.088);
- nothing happened after that until CTest's kill at 04:35:25;
- the components suite ran on the same runner at the same time, for 13.7 s.

The asker thread waits in `while (!Crash::isReporting()) yield();` for a state that lasts only while
the dump is written, a few milliseconds there. On a busy runner the asker can first run after the
report ended. It then waits forever, and the main thread waits for it in `join`. That gives exactly
the record: a written dump, and no "crash-tests lived on".

A second hypothesis, a `ptrace` attach race in Crashpad's `PtraceAttach`, was tested and not found:
100 local runs, 40 of them under a stream of `SIGUSR2` to every thread every 20 µs during the dump,
and no run stopped.

**Fix.** A handshake with a bound. The asker waits until the report is in progress or the main
thread says the report ended. It records whether its request landed. The mode repeats the report
until a request lands, up to a fixed count, and fails with a message where none did. The mode always
ends, and it still asserts what it was written for.

### 4.2 One exception record, one writer at a time

**Root cause.** Crashpad's Linux client keeps one exception record. `DumpWithoutCrash`, which a
report calls, and the crash signal handler both write it. Nothing orders a report on one thread and
a fault on another, so the report's dump can carry the fault's record.

**Fix.** The report gate (`beginReport` and `endReport` in `crashnote.cpp`) orders every dump:
1. `Crash::install` registers a first-chance handler (`CrashpadClient::SetFirstChanceExceptionHandler`,
   present in the vendored client). It runs before the client writes the record.
2. On a fault, the first-chance handler waits while a report is in progress on another thread. The
   wait is async-signal-safe (an atomic load and `nanosleep`), and bounded at two seconds, so a stuck
   report cannot hold a crash. A report in progress on the faulting thread itself does not wait.
3. A fault takes the gate as a final report does (`Ending`), so a report that starts during a crash
   dump is refused.

The handler is in `crashpadclientposix.cpp`, for Linux, whose client is the one shown to share the
record. The new matrix mode runs on Windows and macOS too, and its result there decides whether they
need the same gate.

**Tests.** The crash matrix gains the mode the plan once refused: a fault on a second thread while a
report is in progress. The expectation is two dumps, each summarised as its own kind on its own
thread.

**Outcome.** On Linux the report's dump was the fault's in 3 of 3 runs before the handler, and is
its own in every run since. On macOS the mode passed with no handler. On Windows it failed another
way: the fault's dump ended the process before the report's was written, which left one summary,
the crash's. The same wait, through Crashpad's Windows first-chance filter, is in
`crashpadclientwin32.cpp`.

### 4.3 The harness's catcher is on

**Root cause.** `apps/rtxtool/main.cpp` defaults `OPENMW_DISABLE_CRASH_CATCHER` to `1` "because a
dialog waiting for a click is a run that never finishes". The next line defaults the dialog to off,
so the reason no longer holds. The hang limit is safe too: the harness sets it, and the watch starts
at the first heartbeat, after the cold compile.

**Fix.** Delete the line, and replace the comment with the policy: catcher on, no dialog. A
`omw` driver test runs `openmw-rtxtool` with a forced fault and expects a report folder.

---

## 5. Build, tools and install

### 5.1 The driver states the systems it supports

**Root cause.** `system.py` sets `SYSTEM = "windows" if WINDOWS else "linux"`, a two-way test, so a
third system silently takes Linux's paths, presets and SDK.

**Fix.**
1. `SYSTEM` comes from `sys.platform` through a table of the systems the driver supports. An
   unsupported system is refused at the driver's entry, before any verb runs, in one line that names
   `CI/before_script.macos.sh` as the macOS route.
2. `user_config_dir` and `user_data_dir` go. The harness's `info` prints the two folders through
   `Files`, which answers for every system, and `omw setup` reads them (`REVIEW.md` names this shape).

**Tests.** The driver's own tests (`tools/omw/tests`) with `sys.platform` patched to `darwin` and to
an unknown name: both are refused, and Linux and Windows are unchanged.

### 5.2 A test's folder is its binary's folder

**Root cause.** `cmake/Tests.cmake` uses `RUNTIME_OUTPUT_DIRECTORY` as every test's working folder,
and the top level sets that variable only outside the Apple branch.

**Fix.** `WORKING_DIRECTORY $<TARGET_FILE_DIR:${target}>`, which every generator and every system
answers. The crash matrix writes under `${CMAKE_BINARY_DIR}/test-output/crash-matrix` (see §5.4).

### 5.3 Paths stay paths up to the system call

**Root cause.** `Platform::Process::setEnvironment` takes `const char*`, and the Windows half calls
`_putenv_s`. So a caller narrows its path with `path.string()`, which converts through the ANSI code
page and throws for a character outside it. A search finds six more `path.string()` calls in fork
code: `apps/rtxtool/main.cpp:546`, `:1150`, `film.cpp:687`, `shaderdirectory.cpp:42`,
`pipelinecache.cpp:167` and `spirvpintool.cpp:26`, `:63`.

**Fix.**
1. `setEnvironment` and `setEnvironmentDefault` take the value as `const std::filesystem::path&`
   where the value is a path. The Windows half calls `_wputenv_s` with the path's native wide string.
2. Each listed call spells the path with `Files::pathToUnicodeString`, or keeps it a path.
3. **A check, so the class of fault does not come back:** `omw`'s listing check refuses
   `.string()` on a `std::filesystem::path` in fork code, with the reason in its message.

**Tests.** A host test on Windows CI: a cache folder under a name outside the code page (Cyrillic or
CJK) set through `setEnvironment` and read back by `_wgetenv`.

### 5.4 The install takes an allow-list, and working folders stay out of it

**Root cause.** The Windows rule installs the whole runtime folder with a deny-list, and the other
systems install `resources/` with two excludes. The tests and the harness write into those same
folders. So each new working folder leaks into the package. The CI log of run 36963270110 shows the
crash matrix's folders in the Windows install, and every install carries `views.cfg`, `benches.cfg`
and `rtx/vfs/`.

**Fix.** Each owner writes where the install does not read:
1. **The harness's data goes under one root, `${RUNTIME_OUTPUT_DIRECTORY}/rtxtool/`:** the views, the
   suites, the VFS scripts, the shader set with source, and the driver caches. The harness's
   `--resources` default resolves both roots. No install rule names `rtxtool/`, so none carries it.
2. **Test output goes under `${CMAKE_BINARY_DIR}/test-output/`**, outside the runtime folder.
3. The two excludes in the `resources` rule go, because nothing they excluded is there any more.

This changes no upstream install rule, which keeps the upstream diff where it is.

**Tests.** A CI check after `install`: the installed tree holds no `rtxtool/`, `crash-matrix/`,
`views.cfg` or `*-driver-cache`.

### 5.5 The gate links every program the flavour configures

**Root cause.** `gate.py` builds `default_targets` (`openmw-rtxtool`, `openmw`) and the test
targets, and the release leg builds the same two. CI builds `all`. So a program outside that list,
such as `openmw-rtx-spirv-digest`, is never linked by the gate.

**Fix.** The gate's release leg builds `all`: every program the release preset configures, the tools
and the SPIR-V programs among them. The Qt programs stay with the `full` flavour, as today. The cost
is one build of the tools in release, under ccache.

### 5.6 Tests past one second

**Correction to `ISSUES.md`.** Run alone, the four tests take 0.96 s (kernels), 0.70 s (the lobe
test), 0.80 s (frame cost) and 0.35–0.61 s (fog noise). They pass one second only in `omw test`,
where both GPU shards, the components suite and the crash matrix run at the same time.

**Fix.**
1. **The rule is measured where it means something:** `omw test` records each test's time from a
   serial run of the binary (`--gtest_filter`, alone), and the gate fails a test over one second
   there.
2. **`RtxFogNoiseTest.everyLevelAMarchMayReadClearsTheShareTheDensityIsDividedBy`** takes 200 000
   Halton samples per level against a 5% tolerance. The step measures the coverage of every level at
   20 000 and at 200 000 samples. Where the two agree within a tenth of the tolerance, the test takes
   20 000 and states the measured difference as its reason.
3. **`aLobeKeepsItsHistoryOverATurnOfTheViewAsWideAsTheLobe`** builds two 256-frame references,
   against which it compares a turned frame's error with a raw frame's. The step measures the
   reference's own error at 64 and at 256 frames against a 1 024-frame one. Where 64 keeps the
   reference's error under a tenth of the margin between the two compared errors, the test takes 64
   and states that.
4. The kernel test's cost is its compile of seven pipelines, which is what it tests. It stays, and
   the serial measurement holds it under one second.

---

## 6. Order of work

Each step ends on a clean `./omw gate`, with its test, and with `shot --against` where a picture can
move. The order puts the cheap fixes that unblock or protect measurements first.

1. **Phase A, safety and tooling (XS and S):** §3.1, §4.1, §4.3, §5.2, §5.5, §5.3, §5.1, §5.6.
2. **Phase B, measurement:** §1.1 (queue priority, the sharing line), then §1.2's steps 1 and 2.
   Every later A/B reads these lines.
3. **Phase C, crash catcher:** §4.2.
4. **Phase D, parity, the small ones:** §2.3, §2.4, §2.5. Each moves only the pictures of its own
   content.
5. **Phase E, install:** §5.4, which moves files the harness reads, so it lands with the harness's
   `--resources` change in one commit.
6. **Phase F, light transport:** §2.1 (census first), then §2.2, which uses §2.1's `A`.
7. **Phase G, content readers:** §2.9, §2.7, §2.8.
8. **Phase H, coverage:** §2.6, behind its measurement.
9. **§1.2 step 3** lands with `REDESIGN.md`'s W6 and W9, and its acceptance (the miss rate's spread
   across legs) is added to theirs.

When each fix lands, its entry leaves `ISSUES.md`, and `REDESIGN.md`'s matching item points to the
commit.
