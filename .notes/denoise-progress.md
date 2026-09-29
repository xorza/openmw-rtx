# Denoise and upscale: the run's record

The plan is `.notes/denoise-and-upscale.md`. What a person has to look at is
`.notes/verify-manually.md`. After a context compaction, read this file and the plan first.

- Branch `denoise-night`, from `bd42e0e219`. One commit per green phase, pushed; CI through
  `gh workflow run ci.yml --ref denoise-night`.
- Scratch: `/tmp/claude-60354/-home-xxorza-Projects-openmw/522b3b54-94d6-4bd6-b957-63fcbeef427d/scratchpad/run`.

## Summary

| Phase | State | Commit |
|---|---|---|
| 4 shadow denoiser | done | 85c2647ea6 |
| 5a lamps join the diffuse signal | done | a5d9d089da |
| 6a fast history | stopped, reverted; a lag guard kept | f426a7e18e |
| — race in the shadow classification | fixed | 5ff7a51f80 |
| 7 the upscaler's sizes in the core | done | f7c1f5c891 |
| 8 FSR 3.1.4 | done | a815be83d9 |
| 9 the glossy filter | done | 84b3aefc2e |
| 3 the driver floors | done | 7952c16b0f |
| 11 parity of the optional features | done | 4a2680be3b |
| — `noise --strafe` | done | 7406a4e4b5 |
| 12 the documents | done | 56e5a44d90 |
| 10 the rates and the levels | done | 577d532959 |
| 5b ReSTIR DI temporal | not run: its condition does not hold | — |

## Phase 4: the shadow denoiser

Baselines at `bd42e0e219` (release, 1920×1080, `omw noise`; frame mean / p99, bar mean / p99):

| Profile | Place | Frame | Bar |
|---|---|---|---|
| vanilla | seyda-neen-pier | 18.54 / 92 | 5.92 / 30 |
| vanilla | balmora-mages-guild | 7.11 / 90 | 4.54 / 38 |
| PBR | seyda-neen-pier | 22.75 / 106 | 7.06 / 34 |
| PBR | balmora-mages-guild | 12.21 / 111 | 7.34 / 53 |

Checkpoints:

1. The sky source's term split off into `CHANNEL_SUNLIT` (payload 12 → 15 words): every picture of
   `shot --views=all --map`, filtered and unfiltered, within 1 level of the baseline, on 0.00% of
   the pixels.
2. `lib/surfacematch.glsl` (the accumulator's history test, the wavelet's plane and facing tests):
   every filtered frame identical to checkpoint 1 by hash.

Where the port differs from the plan, and why:

- **No mask pass and no mask buffer.** The classification reads the 24×24 square of bits its kernel
  and its three filter levels reach straight off `CHANNEL_SUNLIT` into shared memory. One dispatch
  fewer, no buffer, no `R32UI` format, no subgroup operation.
- **The tile record is one bit (`R8`)**: a cleared tile's value is already the temporal pass's own
  answer, written exactly nought or one, and the SDK's middle level leaves it there.
- **No `lib/shadowdenoise.glsl`**: the port lives in the two shaders that use it, each with the notice.
- **`lib/surfacematch.glsl`** in place of the planned `lib/history.glsl`: it holds the temporal
  test and the two spatial tests, so the shadow filter stops at the wavelet's edges.
- **Moments in `RGBA16F`**: the SDK's `R11G11B10_FLOAT` has no layout here.
- **The SDK's contrast step is not ported**: `pow(mean, max(1.2 - variance, 1))` darkened a penumbra
  by 3.4% against a 256-frame reference, on purpose. Correctness before looks.
- **The frame's edge is not a shadow**: the SDK counts pixels off the frame as shadowed in its local
  kernel, which held a penumbra's mean 1.3% low; the port normalises by the pixels inside.
- **Velocity dilation dropped**: the SDK takes the nearest-depth velocity of a quad; the port uses the
  accumulator's footprint and surface test, one rule for both histories.

Test figures (`theShadowDenoiserTakesTheNoiseOffAPenumbraAndLeavesItsLightWhereItWas`): RMS error
against a 256-frame reference 0.0762 raw, 0.0058 denoised; mean 0.64% under the reference, the
history clamp's own bias.

Debug noise (vanilla), first run: pier 18.54 / 92 → 4.34 / 18 (bar 5.92 / 30); guild unchanged.

The cost, and what it changed:

- The first port read the 24×24 square of bits straight off the channel (no mask pass). The bench
  split said the classification cost 0.24 ms whether it filtered or not, 0.34 ms in a room. The
  SDK's structure was the right one: the mask pass is back (`shadowmask.comp`, one `R32UI` word an
  8×4 tile), the classification reads 18 words, and a frame whose sky lights nothing
  (`Shaders::skySourceLights`) records no shadow pass at all; the composite then takes the channel's
  own bit (`CompositeConstants::mShadowed`).
- The filter levels no longer read `CHANNEL_SUNLIT`: the temporal pass marks a pixel that receives
  nothing with a variance of `SHADOW_NO_RECEIVER`, and the levels carry it.

Final figures (release, 1920×1080, `omw noise`, frame mean / p99 against bar mean / p99):

| Profile | Place | Before | After | Bar |
|---|---|---|---|---|
| vanilla | seyda-neen-pier | 18.54 / 92 | 2.33 / 12 | 5.92 / 30 |
| vanilla | balmora-mages-guild | 7.11 / 90 | 7.11 / 90 | 4.54 / 38 |
| PBR | seyda-neen-pier | 22.75 / 106 | 6.70 / 68 | 7.06 / 34 |
| PBR | balmora-mages-guild | 12.21 / 111 | 12.21 / 111 | 7.34 / 53 |

The PBR pier's p99 is the glossy speckle, which phase 9 is for.

Cost, back to back against `bd42e0e219` built in `~/Projects/openmw-base` (default suite, two legs
each after a warm-up; taken under the session):

| Place | Trace base → new | Shadow | Frame median base → new |
|---|---|---|---|
| seyda-neen-ship | 4.18, 4.21 → 4.23, 4.26 | 0.43, 0.44 | 9.09, 9.16 → 9.66, 9.75 |
| seyda-neen-ship-dawn | 4.64, 4.73 → 4.74, 4.81 | 0.51, 0.57 | 9.23, 9.40 → 10.05, 10.54 |
| balmora-mages-guild | 2.83, 2.85 → 2.83, 2.82 | — | 7.33, 7.71 → 7.38, 7.69 |

The trace's own growth, 0.05–0.1 ms outdoors, is the payload's three new words.

Proofs: every frame of `shot --views=all --map --upscale=off --filter=0` within 1 level of the
baseline; the 27 map tiles moved, because a map is a picture the wavelet always filters and the
shadow denoiser now filters it too (one frame, no history). `repeat --pairs=10` identical. `./omw
test` green. `./omw gate` clean.


## Phase 5a: the lamps join the diffuse signal

Where `gather` splits, its diffuse half is the lamps alone, so no new field was needed:
`shadeSolid` adds it to the bounce, and `litSurface` now takes the diffuse and the specular light
apart instead of a `DirectLight`.

Test (`theFilterTakesTheLampsNoiseOffAFloorAndLeavesTheirLightWhereItWas`, four coloured lamps):
the filtered error is 0.21–0.23 of the raw, channel by channel, and the mean within 0.2–0.7%.

| Profile | Place | Before | After | Bar |
|---|---|---|---|---|
| vanilla | seyda-neen-pier | 2.33 / 12 | 2.29 / 12 | 5.92 / 30 |
| vanilla | balmora-mages-guild | 7.11 / 90 | 4.77 / 76 | 4.54 / 38 |
| PBR | seyda-neen-pier | 6.70 / 68 | 6.59 / 68 | 7.06 / 34 |
| PBR | balmora-mages-guild | 12.21 / 111 | 10.25 / 105 | 7.34 / 53 |

**What is left in the guild is the paper screens.** Nearly every pixel over 60 levels off the
reference is on the screens the lamps light from behind: a see-through layer's light is composed in
the trace and never reaches a filter. FSR's accumulation (phase 8) is the next thing that acts on
it; measured again there.

Cost: unchanged (default suite, trace 4.25 / 4.77 / 2.82, filter 2.38 / 1.85 / 3.12, shadow 0.42 /
0.52). Proofs: every frame within 1 level unfiltered; the map tiles moved again, because the lamps
are now filtered in them too. `repeat --pairs=10` identical, `./omw test` green, gate clean.

## Phase 6a: the fast history — stopped

The design (plan 5.3): a fast mean of each pixel's luminance over `ACCUMULATE_FAST_FRAMES = 4`,
kept in the accumulator's free moment channel, and the long mean clamped to it ± k sigmas of one
sample, with its frame count cut to the fast one's where the clamp moved it.

What it did (GPU tests):

| Setting | Lamp out, light left after 8 frames | Four lamps, filtered/raw error | Lone pixel with history |
|---|---|---|---|
| no clamp (today) | 24% | 0.21–0.23 | quieter than alone, mean in 2% |
| k = 2 | 8% | 0.31 | noisier than alone, mean 2.4% dark |
| k = 3 | 8% | fails the mean by 1% | noisier, mean 2.1% dark |
| k = 4 | ≤ 12% | passes | 0.0044 against 0.0048 alone: history nearly worthless |

**Why:** one lamp drawn out of four gives heavy-tailed samples, and a per-pixel mean of four of
them sits below its expectation most of the time. Clamping the long mean to it biases the picture
dark and throws the history away on noise. ReLAX clamps to the fast history's *spatial
neighbourhood* (a 3×3 or 5×5 box of it, in a pass of its own), which is the second design; its cost
and its benefit want a person looking at a flickering torch, so it is not built tonight. The code is
reverted. `aLampThatGoesOutLeavesTheHistoryWithinAFewFrames` stays, as a guard on today's lag (24%,
bound 30%).

## A race in phase 4's classification (found in phase 7)

Phase 7 changes no shader, yet `shot --against` phase 5a's pictures differed on 0.1–0.2% of the
pixels of every sunlit exterior, by up to 147 levels, and on no interior. The cause: rewriting the
classification for the mask words dropped the barrier between lane 0's reset of the shared
receiver count and the other lanes' additions, so a tile was skipped or filtered by its lanes'
timing. `repeat --pairs=10` had passed over it twice; a hash comparison of two runs found it. Fixed
in 5ff7a51f80, and three runs of every view agree by hash. The phase 4 and 5a noise figures were
taken with the race in; they may move by a little.

## Phase 7: the upscaler's sizes in the core

- `upscaleRatio`, `extentsFor`, `jitterPhasesFor` and `sUpscaleLevelBias` in `frame/upscale.hpp`;
  `Reconstruction::mJitterPhases`; the level bias takes FSR's minus one; `sampleFrame` wraps the
  Halton index at the phase count.
- `device/upscalerextensions.hpp` and its uses in the instance, the device and the profile are gone;
  `Upscaler::renderSizeFor` is gone and `createTargets` asks `extentsFor`.
- Tests: the 1920×1080 table (640×360/72, 960×540/32, 1129×635/23, 1280×720/18, 1920×1080/8),
  the level biases with the upscaler's own level, the wrapped jitter.
- Proof: nothing upscales yet, and three runs of every view agree by hash; `./omw test` green, gate
  clean.

## Phase 8: FSR 3.1.4

- `extern/fidelityfx/`: the nineteen SDK headers the seven passes include, unchanged, at tag v1.1.4
  (commit c6efa6bf7f), with the SDK's licence and a README. Every pass compiled, pinned and validated
  as the SDK wrote it with `FFX_HALF=0`: no pinning refusal, no wave operation.
- `shaders/upscale/fsrcallbacks.glsl`, derived from the SDK's callbacks: the constant blocks are
  `fsr.h`'s structs (scalar); one linear sampler; the depth worked out from `CHANNEL_SURFACE`'s
  distance through the eye the puffs channel names (reversed, infinite, near plane 1); the motion
  vector the channel's minus `Jitter()`; the reactive and composition masks answered as nought and
  bound to nothing (the SDK reads a 1×1 default at full pixel coordinates, undefined in Vulkan); the
  luma history declared `rgba16f`, the format the SDK's host creates it in (the SDK declared `rgba8`,
  a format mismatch).
- Seven pass entry files, generated from the SDK's with the tree's binding names.
- Host: `FsrFrame` (the SDK's per-frame constants, no device, tested by hand-worked numbers) and
  `Upscaler` (the SDK's resources, clears and dispatch order, from binding tables). The upscaler is
  a plain member of the renderer, made with it; `makeUpscaler`, `describeUpscaling`,
  `sUpscalerBuilt` and `RtxSettings::playedIn` are gone. A menu's mode change and a stop's own mode
  (`RtxRun::getUpscale`, `Schedule::mUpscale`) take effect at the next frame's start.
- `noise` traces its reference and its bar with no upscaler, the frame at the run's mode.
- `[RTX] upscale = native` by default (D8); the harness keeps quality (D14).

**The jitter sign, derived and measured.** FSR reads a sample at `pixel + 0.5 - Jitter()`, the trace
aims at `pixel + 0.5 + jitter`, so FSR's jitter is the trace's negated. Flipped for one run, native
`noise` was worse at both places: pier 2.77/15 against 2.64/13, guild 4.51/38 against 4.18/35.

`omw release noise` (frame mean / p99; bars 5.92/30 and 4.54/38):

| Mode | seyda-neen-pier | balmora-mages-guild |
|---|---|---|
| off | 2.29 / 12 | 4.77 / 76 |
| native | 2.64 / 13 | 4.18 / 35 — every frame as clean as the bar |
| quality | 4.27 / 21 | 5.40 / 42 |

Native's pass at the guild is FSR accumulating the paper screens no filter reaches.

Cost (default suite, taken under the session): `upscale` 0.60 / 0.56 / 0.64 ms at native, 0.38 /
0.44 / 0.41 at quality; frame median off 9.60 / 10.54 / 7.61, native 10.50 / 11.27 / 8.26, quality
5.81 / 6.29 / 4.32.

Proofs: `anEvenFrameReconstructsToItself` (native, within a byte), `theSameFramesUpscaleToTheSamePicture`
(byte for byte), the `FsrFrame` tests; `shot --upscale=off` identical to phase 7 by hash; `repeat
--pairs=10` identical; `./omw test` green; gate clean (its `check` now runs at quality).

A comment-checker note: `RtxSourceTreeTest.everyMemberACommentNamesIsDeclared` reads `Resource::…`
in any comment as a member of any enum called `Resource`; the port's enum is named `Bound` for it.

## Phase 9: the glossy filter

- `CHANNEL_SPECULAR` (8): the lamps' lobe and the bounce's lobe times the transmittance in `rgb`,
  the lobe's roughness in `a`, `SPECULAR_NO_LOBE` (−1) where there is no lobe. The payload grows to
  18 words; the roughness rides in a spare byte of the flags word.
- `specular.comp`, `SpecularPass`, `SpecularHistory`: ReLAX's surface-motion rule (the history is kept
  while the view turns less than `atan(3 r²) · N·V`, floored at a pixel's angle), the accumulator's
  bilinear reprojection against its own surface history, and its outlier clamp, counted by the
  history the view kept. Recorded only where `TraceSubject::mMapped` (the answer `HAS_MAPS` reads);
  the composite reads the channel itself everywhere else.
- `TraceSubject::mMapped` replaces the pass's own `getCounts().mMapped > 0`.

Where the build differs from the plan, and why:

- **The mean is a full float.** In halves, sixteen frames of a metal floor stood 0.13–0.2% under the
  average of the same frames: this card rounds a stored half toward nought, and a running mean
  stores it sixteen times. With a full float the filter over a still eye equals the average of its
  frames (`overAStillEyeTheGlossyFilterIsTheMeanOfItsFrames`). The first moment is the mean's
  luminance, so the second rides in its fourth channel and the count in an `r16f` of its own.
- **The lobe's presence travels explicitly.** A first version took a sample of nought as "no lobe",
  so a glossy pixel whose lamp draw came back shadowed lost its history. Found in review, fixed
  before any measurement below.
- **The history length follows the view's turn** (ReLAX), not a fixed eight frames: the plan's fixed
  length would drag a sharp highlight behind the camera.

Tests: `overAStillEyeTheGlossyFilterIsTheMeanOfItsFrames` (to a thousandth of the mean);
`aLobeKeepsItsHistoryOverATurnOfTheViewAsWideAsTheLobe` (a 0.395 rad turn: roughness 0.302 drops the
history, the frame equals the raw one to a half's rounding; roughness 1 keeps it, 11–12% nearer the
reference in red and green, level in blue); the lamp-noise test asserts a floor with no map writes
nought to the channel.

Proofs: `shot --views=all --map --upscale=off`, filtered and unfiltered, identical to the phase 8 and
phase 5a pictures pixel for pixel (the hashes cannot be compared across the new channel's column);
`repeat --pairs=10` identical under both profiles; vanilla `noise` at native unchanged (2.64/13,
4.18/35).

`omw release noise` under PBR (frame mean / p99; bars 7.06/34 and 7.34/53):

| Mode | seyda-neen-pier before | after | balmora-mages-guild before | after |
|---|---|---|---|---|
| off | 6.59 / 68 | 5.09 / 19 | 10.25 / 105 | 7.67 / 97 |
| native | 5.33 / 22 | 5.10 / 20 | 7.04 / 49 | 6.35 / 47 |
| quality | 5.66 / 25 | 5.57 / 24 | 7.68 / 53 | 7.53 / 54 |

The guild at `off` keeps the paper screens no filter reaches (phase 8); at quality it stands one p99
level over its bar.

Cost (noise run's zones, PBR): `specular` 0.31 (off) and 0.35 ms (native), 0.15 ms at quality; the
trace 3.41 → 3.43 ms at native, inside a run's spread. None in a vanilla scene.

## Phase 3: the driver floors

Order changed: phase 3 ran before phase 10, because phase 10's measurements need a quiet card for
hours and phase 3 does not touch the picture.

- `DriverFloor { mDriver, mDriverName, mRelease }`, a span of them per required extension.
  `VK_KHR_shader_fma`: NVIDIA driver 595, AMD driver 26.3.1 (Adrenalin 26.3.1 release notes), Mesa
  26.2 for RADV, NVK and ANV (Mesa 26.2.0 release notes, `docs/relnotes/26.2.0.rst`, which add the
  extension to all three at once). The refusal: "(AMD driver 26.3.1 or later; this one is 26.2.1
  (LLPC))".
- Fixtures: the plan wanted each fixture from a database report. The database's listing is loaded by
  script and answered nothing to a plain request; a single report page loads. The refusal quotes the
  driver's own text beside the floor and compares nothing, so what a fixture needs is the text's
  form: AMD's from report 51246 ("26.7.1 (LLPC)", an RX 760M on Windows), Mesa's from a release
  string. The test holds AMD, RADV and NVK each to its floor, and a driver the table does not name
  (MoltenVK) to the extension alone.
- The drm-shim: `~/Projects/mesa/build-shim` at `mesa-26.2.3`, built and not installed (`meson setup
  build-shim -Dbuildtype=release -Dgallium-drivers= -Dvulkan-drivers=amd -Dtools=drm-shim
  -Dplatforms= -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dllvm=disabled`). The build's ICD file
  names the install path, so the run uses one in the scratch that names the built library.

**A bug the shim found.** navi21 and navi31 stopped at start-up: `vmaCreatePool failed:
VK_ERROR_FEATURE_NOT_PRESENT`. RADV on RDNA 2 and 3 lists AMD device-coherent memory types that are
device-local; VMA leaves those types out unless the allocator asks for them, and refuses a pool over
one; the allocator made a pool over every device-local type. AMD's own driver lists the same types,
so every Radeon of those generations was refused. The content pools now skip them. After the fix,
under the shim, `openmw-rtxtool info` stands the device up and compiles every kernel on all three:

| Chip | Device | Trace kernels: VGPRs | Spilled VGPRs, fewest – most | Scratch, bytes | Slowest compile |
|---|---|---|---|---|---|
| navi21 | RX 6800 | 128 | 2204 – 27241 | 26624 – 44032 | 18.9 s |
| navi31 | RX 7900 XTX | 144 | 551 – 20168 | 24064 – 41728 | 20.0 s |
| gfx1201 | RX 9070 XT | 144 | 420 – 15830 | 23808 – 40704 | 20.8 s |

The fewest is the plain `visibility` kernel, the most `visibility sun moons sea maps`. Every compute
pass (the denoisers, the glossy filter, FSR's seven) compiles with no spill. The trace kernels spill
heavily on every AMD chip; what that costs cannot be measured without the hardware.

**No `compile` verb.** `info` already builds every one of the 81 pipelines and logs, per pipeline
and at verbose level, what the driver reports of it (`device.cpp`, the pipeline statistics): on
RADV the registers, spills, scratch size and compile time, the figures above. A verb would print the
same lines again. NVIDIA reports statistics for the compute pipelines and "no executable" for the
ray tracing ones, so the NVIDIA column the plan wanted beside the AMD one does not exist for the
trace.

## Phase 11: parity of the optional features

- `DeviceOption::BufferMarkers` for `VK_AMD_buffer_marker`, taken where the build names things, as
  the checkpoints are. `Device::checkpoint` writes a number at the top and the bottom of the pipe
  into two host-read words; `MarkerRing` keeps what the last 256 numbers name, and
  `describeCheckpoints` reads the words back after a loss. Under the shim, navi31's RADV offers the
  extension and the device takes it. Nothing here could make a device lose itself on AMD, so the
  report's text after a real loss is unseen (`verify-manually.md`).
- `instruments/amdgpu.{hpp,cpp}`: the current core and memory clock levels (`pp_dpm_sclk`,
  `pp_dpm_mclk`) and the edge temperature, from the first card whose PCI vendor is AMD's. `CardWatch`
  reads NVML where it opens and amdgpu where it does not, which is `Nvml`'s own assumption of one
  card a box. Parsers tested on fixtures. **Not built**: the busy share and the power the plan
  listed. The report has no line for either on NVIDIA; adding them is a new report feature for both
  vendors, not parity. The holders stay NVML's: amdgpu counts use per open file, not per card.
- `DriverCache`: `MESA_SHADER_CACHE_DISABLE=false`, `MESA_SHADER_CACHE_DIR` and
  `MESA_SHADER_CACHE_MAX_SIZE=8G` beside NVIDIA's variables (Mesa's `docs/envvars.rst`).

## Phase 12: the documents

`architecture.md` (the target, the three denoisers and where each runs, the upscaler, the frame's
record order), `README.md` and `rtx.rst` (AMD RDNA 2 and later as a target, every driver's floor),
`AGENTS.md` (the target and the shim; `shot` baselines with `--upscale=off`; `noise --strafe`).
Intel is named nowhere (D9). The settings text and its translations were brought to FSR in phase 8.

## Phase 10: the rates and the levels

Order: 10 ran after 3, 11 and 12, in the `~/Projects/openmw-base` worktree, so its builds did not
mix with the work in this tree. One constant at a time from the tree's values, `noise` at `native`
under both profiles. The still frame could not see what the rates were chosen for (their comments
say "judged on a moving camera"), so `noise --strafe=150` was built (7406a4e4b5) and the sweep ran
again with it. The first strafe run summed its thirty frames into one picture; fixed before any
figure below.

Frame mean / p99, `native` (bars: vanilla 5.92/30 and 4.54/38; PBR 7.06/34 and 7.34/53):

| Change | vanilla pier | vanilla guild | PBR pier | PBR guild | strafed: vanilla pier | guild | PBR pier | guild |
|---|---|---|---|---|---|---|---|---|
| none | 2.64/13 | 4.18/35 | 5.10/20 | 6.35/47 | 2.32/12 | 4.40/36 | 5.12/21 | 6.99/52 |
| `INDIRECT_LIGHT_RATE` 0.25 | 2.64/13 | 4.18/35 | 5.11/20 | 6.35/47 | 2.33/12 | 4.40/36 | 5.14/21 | 6.99/52 |
| `INDIRECT_LIGHT_RATE` 1 | 2.64/13 | 4.18/35 | 5.10/20 | 6.35/47 | | | | |
| `BOUNCE_RATE` 0.25 | 3.03/14 | 4.26/41 | 6.14/22 | 6.86/55 | | | | |
| `BOUNCE_RATE` 1 | 2.51/13 | 4.10/30 | 4.50/19 | 6.01/42 | 2.05/11 | 4.34/32 | 4.56/19 | 6.67/48 |
| `AMBIENT_EXTERIOR_RATE` 0.25 | 2.64/13 | 4.18/35 | 5.10/20 | 6.35/47 | 2.32/12 | 4.40/36 | 5.12/21 | 6.99/52 |
| `AMBIENT_EXTERIOR_RATE` 1 | 2.64/13 | 4.18/35 | 5.10/20 | 6.35/47 | | | | |
| `ATROUS_LEVELS` 4 | 2.64/13 | 4.16/35 | 5.09/20 | 6.35/47 | 2.33/12 | 4.35/36 | 5.12/21 | 6.97/52 |
| `ATROUS_LEVELS` 3 | 2.63/13 | 4.15/35 | 5.09/20 | 6.34/47 | 2.33/12 | 4.32/36 | 5.12/21 | 6.97/52 |

**The rule's spread.** `noise` is deterministic, so two runs never differ; the spread the plan's
rule needs is the reference's own error. A 256-frame reference stands about a sixteenth of a raw
frame's error from the truth: 18.54 / 16 = 1.16 at the vanilla pier, 12.21 / 16 = 0.76 at the PBR
guild.

**Decisions:**

- `ATROUS_LEVELS` 5 → **3**. Level with five, still and strafing, under both profiles; the filter's
  zone 2.98 and 3.23 ms → 1.76 and 1.72 at the two places (taken on a busy desktop, but two fewer
  full-screen dispatches is not a figure the desktop moves). The local map tiles, which have no
  history and are all the wavelet's: at most 20 levels apart on Vivec's dark tile, most pixels
  within one, nothing visible (`maps-compare`). `theFilterRebuildsAnArmThroughTheArmsOwnEye`
  measured again: the arms' eye 3.2%, the world's 6.4%; the bound went from 2.5% to 4.5%.
- `BOUNCE_RATE` stays **0.5**. One is cleaner, by less than the reference's error, for 0.5–0.6 ms
  of trace; a quarter is outside the error at the PBR guild (6.86 against 6.01, spread 0.76) and
  fails its p99 bar.
- `INDIRECT_LIGHT_RATE` and `AMBIENT_EXTERIOR_RATE` stay **0.5**. A quarter and one leave the noise
  where a half does, to 0.02. The rule would take the cheaper value, but a quarter is not shown
  cheaper: the trace zones on a busy desktop moved by less than their run-to-run scatter, and both
  comments record why a warp waits out a skipped short ray anyway. A cost that cannot be shown is
  not taken.
- The two issues: `STAR_RADIANCE` 0.45 → **0.36** (the level matched through DLSS's fifth; FSR at
  native leaves a star's peak where no upscaler does, a median of 119 against 113 of 255 at
  Balmora, clear, one in the morning). The indirect rate is judged, above. Both are gone from
  `ISSUES.md`.

Proofs, after the change: `noise` at native 2.63/13 and 4.15/35, PBR 5.09/20 and 6.34/47, strafed
2.33/12 and 4.32/36, the filter's zone 1.75–1.90 ms; `repeat --pairs=10` identical; unfiltered
`shot --views=all --map --upscale=off` identical to phase 9's but for the 26 map tiles, which the
wavelet always filters (at most 32 levels on Arkngthand's dark tile, nothing visible); the filtered
pictures move by up to 25 levels, as fewer levels should.

## Phase 5b: not run

Its condition: the guild's frame still fails its bar at p99 after 4, 5a and 6a. At `native`, the
played default, it passes under both profiles (4.18/35 against 4.54/38; 6.35/47 against 7.34/53).
At `off` it fails (4.77/76), on the paper screens: pane light, which no filter reads and which
ReSTIR's reuse of lamp samples would not reach.

## The finished frame

`omw release bench`, default suite, 1920×1080, after a warm-up leg; frame median / p99 / worst, ms.
Taken under the session (the `card` lines name kwin and zed in a few samples), so a quiet desktop
may read lower (`verify-manually.md`).

| Build | ship | ship at dawn | guild |
|---|---|---|---|
| phase 8, native | 10.50 / 14.05 / 20.47 | 11.27 / 14.85 / 15.72 | 8.26 / 11.29 / 12.05 |
| now, native | 9.14 / 13.59 / 16.41 | 10.11 / 13.34 / 14.98 | 6.97 / 9.43 / 9.77 |
| now, native, PBR | 10.91 / 14.41 / 15.82 | 12.30 / 15.65 / 16.62 | 8.05 / 10.55 / 11.10 |
| phase 8, quality | 5.81 / 7.67 / 8.59 | 6.29 / 8.85 / 10.51 | 4.32 / 6.28 / 7.04 |
| now, quality | 5.21 / 6.89 / 7.85 | 5.70 / 7.73 / 8.70 | 3.60 / 5.39 / 7.78 |
| now, quality, PBR | 6.12 / 8.43 / 10.56 | 6.77 / 8.81 / 9.95 | 4.17 / 5.96 / 6.66 |

The filter's zone at the ship: 2.47 → 1.49 ms at native, 1.10 → 0.64 at quality. The glossy filter
under PBR: 0.19–0.21 ms at the ship.

## The upscaler shook the frame (found after the run)

Reported: at ultra performance the whole frame shimmered and shook. Measured with a still camera on
a square floor against a black sky, the shown picture's centroid swung with the jitter's period by
0.6 of a pixel at native, 0.9 at quality, 1.6 at performance and 3.2 at ultra performance. FSR's own
masks said every pixel accumulated fully, with no disocclusion and no shading change, and its output
swung by the same amounts, so the display chain was clear. The output edge followed each frame's
jitter (correlation −0.74 to −0.83, slope about −0.55 of the ratio).

**Cause:** `LoadInputMotionVector` took `Jitter()` off `CHANNEL_MOTION`, which is already the unjittered
motion of the point the ray hit (both ends are that point, on unjittered screens). A still picture was
handed a motion of one jitter offset a frame, and the history, reprojected by it, followed the jitter.
The jitter's sign was checked again with the fix in: the derived sign holds (flipped, the swing is
0.23 px at ultra performance against 0.02).

**A second fault the test found at 96 pixels:** ultra performance traces 32, the SPD pyramid at half
of it has five levels, and the pyramid passes declare six. `Upscaler::record` now binds a level past
the chain's end as its last, as AMD's Vulkan backend does (`ffx_vk.cpp`).

`aStillPictureHoldsStillThroughEveryUpscale` holds every mode to a quarter of a pixel (measured 0.01
to 0.08; with the fault 0.5 to 2.6).

`omw release noise` after the fix (frame mean / p99; bars 5.92/30, 4.54/38; PBR 7.06/34, 7.34/53):

| Mode | vanilla pier | vanilla guild | PBR pier | PBR guild |
|---|---|---|---|---|
| native, before | 2.63 / 13 | 4.15 / 35 | 5.09 / 20 | 6.34 / 47 |
| native | 2.28 / 10 | 4.04 / 32 | 5.94 / 21 | 6.30 / 45 |
| quality, before (phase 8/9) | 4.27 / 21 | 5.40 / 42 | 5.57 / 24 | 7.53 / 54 |
| quality | 2.59 / 12 | 3.99 / 33 | 5.08 / 18 | 6.21 / 45 |
| ultra performance | 3.76 / 22 | 5.14 / 39 | 5.54 / 25 | 7.33 / 52 |

Strafed at native: 2.32 / 11 and 4.36 / 36. The PBR pier at native is the one figure that rose (5.09
to 5.94, p99 20 to 21), still under its bar; the others fell, quality most. Every phase's figure
taken through the upscaler before this was taken with the fault in.

## `off` in the menus

The launcher and the settings window offer `off`, first: the denoisers and no upscaler.
`sUpscaleMenu` is every mode `sUpscaleNames` spells; each language's label reuses its launcher's
existing word for Off.
