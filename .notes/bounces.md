# Proposal: more than one bounce

Status: proposal, 2026-11-01. When it lands, each lasting decision moves into
`docs/rtx/architecture.md`, and this file is deleted.

## The request

A slider in the `[RTX]` page sets the path's depth from 1 to 8 bounces, for fast GPUs.

## What the code does now

- The eye's solid traces one bounce (`bounce.comp`). `landing.comp` lights the bounce's hit with
  the lamps, the sun or a moon, and the glow, and with `pathEnd` (the cell's ambient, dimmed by
  one occlusion ray). `pathEnd` stands for every bounce that is not traced
  (`shading.glsl`, `BounceRun`: "a walk with no recursion cannot bounce a bounce, and it should
  not").
- `bounce` lists the pixels whose ray hit a surface (`appendLandings`), and `landing` runs over
  that list by an indirect dispatch (`STAGE_DISPATCH_LANDING`).
- `storeBounced` writes `CHANNEL_INDIRECT`, `CHANNEL_FILL` and the lobe's share of
  `CHANNEL_SPECULAR` once. The composite multiplies the indirect light by the diffuse albedo and
  the fill by the ambient albedo, as the rasterizer has `D × lit + A × ambient`.
- Cost on the high-end NVIDIA laptop GPU at a 1280×720 trace, the mean of 17 places: `bounce` + `landing`
  1.03 ms. `landing` 0.36 ms at `seyda-neen-shore`, 1.15 ms at `balmora-mages-guild`
  (`docs/rtx/research/2026-10-21-nvidia-stage-costs.md`). On the AMD handheld: 9–15 ms + 19 ms
  (`docs/rtx/research/2026-10-30-amd-handheld-stage-times.md`).

## What the field does

- **A traced bounce is a stage that runs again.** Quake II RTX runs `indirect_lighting.rgen` once
  for each bounce. Each run replaces the G-buffer's surface with the bounce's hit, and the second
  run lights that hit. It stops at 2, and its second bounce takes no local lights: "very
  expensive, yet their contribution from the second bounce is barely noticeable"
  ([`path_tracer.h`](https://github.com/NVIDIA/Q2RTX/blob/master/src/refresh/vkpt/shader/path_tracer.h)).
- **A wavefront, not a megakernel.** Laine, Karras and Aila keep the path's state in memory and
  run one small kernel for each step, over a queue of the paths that are still alive
  ([HPG 2013](https://research.nvidia.com/publication/2013-07_megakernels-considered-harmful-wavefront-path-tracing-gpus)).
  The `bounce` → list → `landing` pair is already this pattern at a depth of 1.
- **A real-time path ends by a throughput threshold, not by Russian roulette.** NVIDIA's NRD
  sample marks roulette "bad practice for real time denoising": it "increases entropy of the
  signal", "transforms radiance into non-radiance", and "breaks denoising". Its good practice is
  "terminate path if pathThroughput is smaller than some threshold", "approximate ambient at the
  end of the path", and "re-use data from the previous frame" (`PT_THROUGHPUT_THRESHOLD` 0.001,
  `USE_RUSSIAN_ROULETTE` 0;
  [`TraceOpaque.cs.hlsl`](https://github.com/NVIDIA-RTX/NRD-Sample/blob/main/Shaders/TraceOpaque.cs.hlsl),
  [`Shared.hlsli`](https://github.com/NVIDIA-RTX/NRD-Sample/blob/main/Shaders/Shared.hlsli)).
  Offline renderers (pbrt) and RTX Remix use roulette. Remix has `rtx.pathMaxBounces` 4 (0–15),
  `rtx.pathMinBounces` 1, and `rtx.enableRussianRoulette` on, with the continue chance held
  between 0.6 and 1 on the first bounce and under 0.9 after it. Its own note says that roulette
  "may leave threads in GPU warps without work" unless the paths are reordered
  ([`RtxOptions.md`](https://github.com/NVIDIAGameWorks/dxvk-remix/blob/main/RtxOptions.md)).
- **A path's whole light goes to the channel of its first bounce.** The NRD sample sets
  `isDiffusePath` at the first bounce, and every later bounce adds to that channel. The fork's
  `bounceOf` already sends a lobe's whole arrival to `CHANNEL_SPECULAR`.
- **Fast multi-bounce light comes from caches, not from depth.** The NRD sample traces 1 bounce
  and gets the rest from two caches. L1 is the previous frame's composed diffuse and specular
  light, read where the hit projects into the previous frame, with tests on view depth, the
  screen edge and the side of the surface (`ReprojectIrradiance`,
  [`RaytracingShared.hlsli`](https://github.com/NVIDIA-RTX/NRD-Sample/blob/main/Shaders/RaytracingShared.hlsli)).
  L2 is SHaRC, a world-space hash grid of radiance. Its update pass traces full paths from about
  4 % of the pixels each frame, and the render pass stops the path at the first cached hit after
  the primary ([SHaRC integration](https://github.com/NVIDIA-RTX/SHARC/blob/main/docs/Integration.md)).
  Each frame then carries the light of all the frames before it, which is an unlimited number of
  bounces for the cost of one.

## Decisions

Each decision gives the choice taken and the choices refused. The user took the six that
change the picture or the cost on 2026-11-01: `pathEnd` at the last landing, the threshold, the
diffuse lobe past the first bounce, all lights at every landing with a bench to check it, the
range 1–8 with a default of 1, and the depth before an L1 cache. The rest follow from the code.

- **Depth is a loop of one kernel, `landing`, over two lists in turn.** At depth `k`, `landing`
  lights hit `k` and, in the same turn loop as its light rays, traces the extension ray to hit
  `k + 1`. It stores that hit where it read hit `k` (`storeStageSolid`) and lists the pixel in the
  other list. The last depth traces no extension ray. Against: a `bounce` stage again for each
  depth, which resolves the stored surface again and adds a barrier for each depth (resolving the
  surface again cost the integrated GPU 28.7 ms of a 251 ms frame,
  `docs/rtx/research/2026-10-21-amd-integrated-stage-times.md`); one kernel with a loop over depths,
  the megakernel that `docs/rtx/architecture.md` already refuses.
- **`pathEnd` goes to the last landing only.** `pathEnd` stands for the bounces that are not
  traced, so it moves down as the depth goes up, which is NRD's "approximate ambient at the end
  of the path". The throughput carries it, and at a depth of 1 the picture is today's, to the bit.
  **The sky's escape is not `pathEnd`**: out of doors a ray that reaches nothing brings back the
  sky's glow (`bounceEscape`), which is real light. It counts at every depth where a ray escapes,
  and it goes to `CHANNEL_FILL` as it does now (`escapedArriving`). Against: `pathEnd` at each
  landing, which counts the same light again at each depth; no `pathEnd` above a depth of 1, which
  takes the content's `AMBI` out of every room and makes dark corners black.
- **A path's light stays in the channel its first bounce chose.** Where the eye's solid drew its
  lobe, every deeper landing adds to `CHANNEL_SPECULAR`; where it drew the diffuse half, to
  `CHANNEL_INDIRECT` and `CHANNEL_FILL`. That is `bounceOf` at each depth, keyed by the record's
  `mBounceLobe`, as the NRD sample keys by `isDiffusePath`.
- **A path ends at a throughput threshold, as NRD's sample does.** A path stops where the
  luminance of its throughput falls under a threshold, or at the depth limit. Against: Russian
  roulette, which Remix uses. In this wavefront the lists compact the surviving paths, so
  roulette leaves no idle lanes, and Remix's argument for SER does not apply. What remains
  against it is NRD's: each survivor is divided by its chance, which is a firefly the denoiser
  must take, and the depth limit makes the estimate biased in any case. In a room every bounce
  lands, and an albedo of 0.5 puts the throughput at 0.004 after 8 bounces, so the threshold
  saves little, and the slider is the real limit on cost.
- **A bounce after the first is diffuse only**, as in Quake II RTX. The lobe at the eye's solid
  stays as it is. Against: the full BRDF at every depth, which adds a reflection of a reflection
  that the specular channel does not hold apart.
- **The lamps light every landing.** Morrowind's rooms are lit by lamps alone, so Quake II RTX's
  saving (no local lights on the second bounce) removes most of what a second bounce brings into
  a room. Against: the sky and the sun alone after the first landing. A `bench` A/B against that
  choice is part of batch 5, and its numbers come back to the user.
- **Seeds by depth.** Depth 1 keeps its blue-noise streams (`STREAM_BOUNCE`). A deeper draw takes
  a hash of the pixel, the frame and the depth, as the NRD sample takes blue noise for the first
  bounce only. Each landing seed (`SEED_LAMPS_BOUNCE`, `SEED_AMBIENT_REACHING`,
  `SEED_SHEET_SIDE`) is salted by the depth.
- **The depth limit is a frame constant, and the depth a push constant.**
  `VisibilityConstants::mBounceDepth` holds the limit, as `mBounceRate` holds the rate: the world's
  frame takes the setting (`describeWorld`), and a frame built by hand (`beforeWorld`, the doll,
  the map tile) takes 1. `landing` takes its depth in a push constant
  (`ComputePipeline<LandingConstants>`), since each dispatch has its own. The branch on either is
  uniform across the dispatch, so it adds no divergence and no tuple (`VisibilityVariant` stays at
  16 tuples).
- **A head for each depth, and two arrays of pixels in turn.** `STAGE_TILES` gets a landing head
  (the indirect dispatch, its count, and the apart run's pair) for each depth up to 8. The begin
  of the frame clears them with the heads it clears now (`GBuffer::begin`, `updateInline`), so no
  depth clears anything. Depth `k` reads head `k` and array `k mod 2`, and appends to head `k + 1`
  and array `(k + 1) mod 2`, which only depth `k − 1` read. Against: two heads reused in turn,
  whose clear between dispatches needs a transfer and a second barrier at each depth, or a third
  array for a clear on the device (14.7 MB more at a native 1440p trace).
- **Pictures stay at a depth of 1.** A doll and a map tile are drawn by `PictureTracer` with a
  frame built by hand. Their look is fixed, and a deeper path costs them time at a load.
- **No rate on the eye's bounce.** `BOUNCE_RATE` goes (batch 2). At 0.5 it saved 4–6 % of the
  frame and raised the noise at every place measured (`look.h`), and at 1.0 it does nothing. The
  depth's own early stop is the threshold. Against: keeping it for a low-end preset, which can
  bring it back with the measurement if a preset is ever wanted.
- **The setting is `[RTX] bounces`, an integer from 1 to 8, default 1.** Default 1 keeps every
  player's picture and cost; a fast GPU opts in. Against: a default of 2, which costs an AMD
  handheld far more than this card; a top of 4 (Remix's default), since past 4 the light adds under
  7 % at ρ = 0.5 but pale rooms gain more. It applies live, through
  `RtxRenderer::applyChangedSettings`, because it changes no image and no pipeline. The menu
  shows a slider on the RTX page, under the upscaler. The harness takes `--bounces=N`, so
  `noise --ab=bounces=1,4` and `bench` can compare depths.
- **A radiance cache is a second proposal, after the depth ships: L1 first, L2 only if L1's
  misses show.** L1 (the previous frame) and
  L2 (a SHaRC hash grid) give the light of many bounces for about the cost of one, and that is the
  best performance the field knows. But a cache is a reuse with bias, and
  `docs/rtx/architecture.md` ("No reuse of the bounce") removed ReSTIR GI for its bias. The traced
  depth gives the reference that a cache is measured against. Section "Phase B" holds the plan.

## What a deeper path changes in the picture

- **A room gets brighter, by the albedos.** A surface lit by its walls takes the walls' light times
  1 + ρ + … + ρᴺ⁻¹ of the bounces, and `pathEnd` shrinks by ρᴺ⁻¹. At ρ = 0.5 the bounced light
  goes from 1 to 1.99 of the first bounce's at a depth of 8; at ρ = 0.7, from 1 to 3.1. Pale
  plaster rooms change most.
- **The exposure takes some of it back, but not inside.** The meter holds interiors at
  `EXPOSURE_MAX` (40), so a brighter room shows brighter. Out of doors the meter follows.
- **Noise.** Each depth adds variance to `CHANNEL_INDIRECT`, mostly in rooms lit by lamps, where
  each landing draws one lamp of the candidates. `noise` measures it.

## Implementation

### Shaders

- `lib/stage.glsl`
  - `STAGE_DISPATCH_LANDING` becomes `STAGE_DISPATCH_LANDING + depth − 1` for depths 1 to
    `MAX_BOUNCES` (8), and `mLandedApart` an array of the same size. `STAGE_TILES_HEAD_WORDS` and
    the `sEmpty` table in `GBuffer::begin` grow with them.
  - Two arrays of pixels after the tile lists, each `width × height` words with its two runs
    (gathering from the start, apart from the end), as the one array has now.
  - `appendLandings(count, gathers, depth)`, `listLanding(slot, gathers, pixel, depth)` and
    `landingPixelOf(pixel, depth)` take the depth, which picks the head and the array.
- `lib/shading.glsl`
  - `PathEndRays` gains `mExtend`, the extension ray (a `traceAsk` with a committed hit). It runs
    as the fifth turn of the loop in `pathEndSeenOf`. A new `PathEndMet` returns its `SceneAnswer`
    beside the `PathEndSeen`.
  - `landingLightOf` takes the depth and whether it is the last. At an inner landing, the fill and
    the ambient ray are `noRay()`, and the glow, the sky and the lamps stay. At the last landing,
    the fill is today's.
  - `nextBounceOf(Surface landing, vec3 weight, uint depth, uvec2 pixel)` draws a cosine direction
    off the landing (`cosineDirection`, the side by `sampledFace` for a sheet). It multiplies the
    weight by the landing's diffuse albedo times `1 − F` for a glossy landing, the weight
    `bounceDraw` gives a diffuse draw (the cosine-weighted pdf cancels the cosine and π). It
    applies the threshold, takes the far ground's escape past `BOUNCE_REACH` as `startBounce`
    does, and builds the `traceAsk`. The cone widens by `BOUNCE_SPREAD` from the landing's
    footprint. A lamp body's landing continues as any other surface; only its glow is held at
    nought (`landingLightOf`).
- `trace/landing.comp`
  - Reads the push constant `{depth, last}`.
  - Adds what arrived, by `bounceOf` with the record's `mBounceLobe`, into the channels that
    depth 1 wrote: `CHANNEL_INDIRECT` and `CHANNEL_FILL`, or `CHANNEL_SPECULAR` by the stored
    modulation. `storeBounced` gains an add form. Each pixel has one lane in a dispatch, and a
    barrier orders the dispatches, so a read-modify-write of the pixel is safe.
  - Resolves the landing at the detail its draw needs: the lobe's at depth 1 as now
    (`storedLandingOf`), and the coarse detail at a deeper diffuse landing.
  - Where the extension ray hits: `storeStageSolid`, `storeBounce(new weight)`, and the pixel is
    listed in the other list. Where it misses: the escape (`bounceEscape`) times the weight is
    added to the indirect light.
- `trace/bounce.comp`: no change. It stays depth 1's origin.
- The gather rate (`indirectGatherDrawn`) keys on the depth too, so the runs stay apart at every
  depth.

### Host

- `VisibilityPass::record`: after `bounce`, record `landing` `bounces` times, each with its push
  constant and its list, and a `GBuffer::handToStage` between them. Each dispatch is its own
  `GpuZone` (`FrameZone::Landing` with the depth), so `bench` reports the cost of each depth.
- `Shaders::VisibilityConstants` or the stage's push constants hold the depth limit and the
  throughput threshold (`look.h`: `BOUNCE_THROUGHPUT_FLOOR = 0.001`, NRD's value, as a first
  setting, then measured).
- `GBuffer`: the second list doubles the landing list's words (4 B a pixel: 3.7 MB at a 1280×720
  trace, 8.3 MB at a native 1440p trace) and adds one dispatch head. `bytesAt` counts it.
- `Rtx::FrameOptions` (or the renderer's live state, as `setGamma` holds the gamma) carries the
  depth. `VulkanRenderer::setBounces(std::uint32_t)` follows `setGamma`'s path.

### Setting, menu and harness

- `Settings::RTXCategory::mBounces`: `SettingValue<int>`, `makeClampSanitizerInt(1, 8)`, and
  `bounces = 1` in `files/settings-default.cfg` under `[RTX]`, with its comment.
- `RtxSettingValues` and `RtxSettings::derive` carry it. `RtxRenderer::applyChangedSettings`
  reacts to `{ "RTX", "bounces" }`.
- `rtxsupport.cpp` lists `{ "RTX", "bounces", {} }`, and `RtxSupportTest` covers it.
- `openmw_settings_window.layout`: a `ScrollBar` slider (`SettingType` Slider, `SettingValueType`
  Integer, range 1–8) under `RayTracingUpscaleList`, and the page's later widgets move down.
- `files/data/l10n/OMWEngine/{en,de,fr,pl,ru,sv}.yaml`: `RayTracingBounces` and a hint that says
  what each step costs.
- `apps/rtxtool`: `--bounces` beside `--upscale`, recorded in the bench record
  (`benchrecord.cpp`), and accepted by `noise --ab`.

### Tests

- **A furnace with an exact answer.** The eye looks into a closed cube whose walls have albedo
  ρ and glow E, with no lamps and no sky. The pixel's exact radiance is E (1 + ρ + … + ρᴺ) at a
  depth of N. For ρ = 0.5 and E = 1: 1.5, 1.75, 1.875 … 1.99609 at N = 1, 2, 3 … 8. The test
  averages enough frames for the stated tolerance, and states the variance bound that sets it.
  This proves that each depth adds its term. A second run with a cell ambient and no glow proves
  that the fill goes to the last landing: a cosine draw carries a weight of exactly ρ for each
  landing, so the fill channel at a depth of N is ρᴺ⁻¹ times the fill at a depth of 1.
- **A depth of 1 is today's frame.** `./omw kernels` and `shot --views=all --map --upscale=off
  --against` before and after the refactor must show no picture moved.
- **The threshold.** A pixel whose throughput falls under the floor lists no extension ray: a
  wall of albedo 0.01 at depth 2 lists no pixel in the second list.
- **The ceilings.** `RtxVisibilityKernelsTest.noKernelSpillsPastItsCeiling` and the ray-query
  site count (`noModuleStartsARayQueryAtMorePlacesThanItsCeiling`) hold `landing` with its fifth
  ray. A ceiling that has to rise is a decision for the user, with the measurement.
- **The setting.** `RtxSupportTest` and a settings test for the sanitizer's bounds.
- **`./omw repeat --pairs=10` at a depth of 4**, since the lists' order depends on atomics, and
  the frame must not.

### Measurements (to `docs/rtx/research/<date>-bounce-depth.md`)

- `./omw release bench`, depths 1, 2, 4 and 8, the default and interiors suites, A B B A: the
  frame median and p99, and each depth's zone.
- `./omw release noise --ab=bounces=1,2` and `=1,4`: the change in frame noise and bias for each
  depth.
- `shot` at `balmora-mages-guild`, `arrille`, `andrano-tomb` and `seyda-neen-ship` at depths 1, 2,
  4 and 8, to show what each depth adds.
- The same suite on the AMD handheld (`omw release remote`), for the cost at the low end.

## Plan

Each batch ends green: build, the covering tests, `./omw test`, and `./omw gate` at the end.

1. **Measure today** (no code change): the bench and the noise of the default and interiors
   suites at a depth of 1. They are the baseline for each later batch.
2. **Remove `BOUNCE_RATE`.** At 1.0 it skips nothing; it is a dial kept for an experiment. Delete
   `BOUNCE_RATE` (`look.h`), `VisibilityConstants::mBounceRate`, the draw and the divide in
   `startBounce`, `SEED_BOUNCE_TRACED` (`hash.glsl`), the assert in `Rtx::describeWorld`, the line
   in `beforeWorld` (`camera.cpp`), and the check in `frameworld.cpp`'s test. Fix the comment that
   counts it: `BOUNCE_REACH`'s "unlike the two rates beside it". Move its measurement into
   `docs/rtx/architecture.md` as a refused choice: "a rate on the eye's bounce, 0.5, saved 4–6 %
   of the frame and raised the noise at every place measured", with the table. Two steps, so each
   is checked on its own:
   - Remove the rate and keep every seed's number (`SEED_BOUNCE_LOBE` as
     `SEED_AMBIENT_PANE + PEEL_LAYERS + 1u` for this step): `kernels` and `shot --views=all --map
     --upscale=off --against` must show no picture moved.
   - Close the gap in the seeds. This moves the draws of the lobe, the water's shafts, the soft
     edges and the see-through meets, so pictures move by their noise alone: `noise` at the default
     suite must hold its figures, and the exact-value tests must pass.
3. **Move the bounce's tail into `landing`, at a depth of 1.** Add the extension ray to the turn
   loop. At the last depth, no ray runs, so the picture must not move (`kernels`, `shot`). Check
   the register and spill ceilings.
4. **Two lists and the depth loop.** The push constant, the list heads, the add form of
   `storeBounced`, the seeds by depth, the fill at the last landing, and the threshold. Run the
   furnace test, `repeat`, and the depth-1 `shot` check.
5. **The setting, the menu, the harness option and the translations.**
6. **Measure the depths** and write the research note. Bring back with numbers: the threshold's
   value, and the A/B of lamps at deep landings against the sky alone.
7. **Fold the lasting decisions into `docs/rtx/architecture.md`** and delete this file.

## Phase B: a radiance cache (a separate proposal)

The outline, to decide after the depth ships:

- **L1, the previous frame.** The composite also writes the frame's outgoing diffuse radiance
  without the fill (the lights by the albedo, and the glow), held one frame beside the held
  surface (`GBuffer::getHeld`). A landing projects its hit into the previous frame. It takes the
  held light where the held surface's depth agrees within 1 %, the hit faces the same side, and the
  point is inside the screen, as `ReprojectIrradiance` does. The path then stops there. Cost: one
  image the size of the trace (7 MB in RGBA16F at 1280×720), and one read for each landing.
- **L2, a world-space hash grid**, a port of SHaRC (Apache 2.0): an update pass over about 4 % of
  the pixels, a resolve pass over the table, and a query at each landing after L1 misses. 2²²
  entries at 40 B is 160 MiB. Every pass costs the same on every frame, which suits this tree's
  rule on uniform frame times.
- **Why L1 may come first.** L1 is one image and one read for each landing, and gives the light
  of every frame before it where the hit is on the screen, which in a room is most hits. The
  traced depth then becomes the reference that L1's bias is measured against.
- **What the user decides first:** whether the cache keeps the fill (the content's `AMBI`) out of
  what it stores, so the ambient is not multiplied again on each frame of feedback; whether its
  bias is acceptable against a converged reference (`noise`); and whether its determinism holds
  under `repeat`, since a hash insertion's slot depends on the order of the atomics.
