# Performance

Where the ray tracer's frame stands, how it is measured, what is still open, and what was tried and refused. The
figures behind each line are in the dated notes of `docs/research/`, and a kept change's own numbers are in its
commit.

## Hosts and method

- **Desk** (a high-end NVIDIA laptop GPU): `./omw release bench --window=false` at 1920x1080, the default suite, three
  legs a side in turn after a warm-up leg a side, each run ending the turn. A/A: frame within 1%, a zone within 1–2%,
  p99 within 15%. Its host rows move 10–20% between identical runs, so a host change is judged on the other two.
- **Handheld** (AMD, RADV, its clock held): built in the release container, staged and run by `./omw release remote`,
  1280x800, 300 frames, one or two legs a side. A/A: frame and every zone within 0.1%, p99 within 2%.
- **AMD iGPU** (a low-end integrated GPU, AMD's Windows driver): native MSVC release of the harness alone, in `base`
  and `cand` worktrees fed by `git push`. A run needs the logged-on session, so a job is a script a scheduled task
  starts there; over SSH a run compiles its pipelines and logs their registers but makes no swapchain
  (`docs/research/2026-11-01-windows-tests-over-ssh.md`).
  `bench --size=1280x720 --window=false --frames=200`, two rounds. A/A: frame within 0.3–0.8%. A shader set's cold
  compile is four minutes, a warm vanilla leg 55–85 s; a build beside a run moved a frame by 48%.
- **One binary a host, a shader set a side**: `--shader-source` reads a folder's modules, which compile to the shipped
  ones' code on every driver, so a shader candidate is a folder and no build. The desk builds every side's set; each
  host reads a desk-built set as its own (every zone within 0.3%).
- **RADV on the desk**: Mesa's `amdgpu` drm-shim (`../openmw-perf/mesa-shim/`), `AMDGPU_GPU_ID` naming the handheld's
  chip: `bench --frames=1 --pipeline-code` compiles every pipeline for that card in six seconds, and the kernel
  ceilings' test runs under it in ten. A liveness pass over ACO's IR gives each kernel's peak and what rides each loop.
- **What a kernel holds** is the driver's own answer, printed a pipeline a line in a run's log; its code is
  `--pipeline-code=<dir>`. The census (`shot`, `--pipeline-code`) runs the modules that count, which hold up to 17
  registers more than the shipped ones `bench` runs.
- **Build RAM peaks**: the desk's release harness from nothing 16.6 GiB (35 min cold, 4 min on a warm compiler
  cache), the debug flavour with its tests 16.5 GiB; the AMD iGPU's harness 13.5 GiB. One build at a time.

## Where the frame stands

The modules of `c83efc723c` (a mod's maps read, `HAS_MOD_MAPS`) against `f3793d01bf`, one binary a host; frame median
ms, the handheld one leg a side, the desk three, the AMD iGPU two.

| content | place | handheld | desk | AMD iGPU |
|---|---|---|---|---|
| vanilla | ship | 86.89 → 82.04 (−5.6%) | 5.84 → 5.64 (−3.4%) | −1.0% |
| vanilla | ship-dawn | 101.13 → 94.98 (−6.1%) | 6.31 → 6.02 (−4.6%) | −1.8% |
| vanilla | guild | 54.96 → 53.07 (−3.4%) | 5.09 → 5.08 | −1.0% |
| PBR | ship | 99.83 → 97.74 (−2.1%) | 6.78 → 6.73 (−0.8%) | 0.0% |
| PBR | ship-dawn | 118.13 → 117.57 (−0.5%) | 7.30 → 7.22 (−1.1%) | −1.6% |
| PBR | guild | 66.14 → 64.53 (−2.4%) | 6.22 → 6.25 (+0.5%) | −2.7% |
| M[FR] | ship | 119.26 → 114.17 (−4.3%) | 7.31 → 7.34 (+0.5%) | +0.6% |
| M[FR] | ship-dawn | 134.74 → 130.71 (−3.0%) | 7.69 → 7.70 | −1.3% |
| M[FR] | guild | 82.43 → 76.40 (−7.3%) | 6.38 → 6.32 (−0.9%) | −3.8% |

Since then the fog's scatter asks its rays at one traversal site (`cd7d4b58a9`) and the layers' stage asks a hit's
opacity before it resolves it; neither moved a picture.

- **AMD's Windows driver took the maps' change unevenly**: `direct` −3.9 to −7.0% under a sky and +3.2% in the vanilla
  guild (96 → 109 registers), `landing` +1.0 to +4.1% on vanilla and at the ship, `panes` +2.2 to +4.4% on vanilla,
  `water` +4.7% at the Repack's ship; `bounce` and `trace` down elsewhere.
- **Tours of the desk** (`3234741bbe`), median over places and worst frame: vanilla with both expansions 93 places,
  no fault, 1.21 and 77.9 ms; the Repack 155 places, 2.11 and 247.4 ms; the PBR stack 93 places, 1.36 and 71.7 ms.
  Their faults are in `.notes/ISSUES.md`. A vanilla tour names both expansions on its line
  (`--content=Tribunal.esm --content=Bloodmoon.esm`): the profile names Morrowind alone, 81 places.
- **Every host is GPU-bound**: the host's walk, place and upload are under 1 ms on the desk and 5 ms on the handheld.
  The AMD iGPU's host runs at about 3.5 times the handheld's CPU time (update 23.4 ms at the ship), which costs no
  frame under its GPU's 115 ms yet.

## What the drivers do

- **RADV caps a kernel's waves by its shared memory as by its registers**: a workgroup's bytes rounded up to 1024, and
  65536 over that many workgroups a compute unit — 11264 bytes run 6 waves a SIMD, 9216 and 8192 run 8, 7168 and 6144
  run 10, 5120 run 12, 4096 run 16 — and the registers the driver reports are then that wave count's budget. A
  stage's ray query stack is 2048 bytes there and caps nothing (`docs/research/2026-11-15-radv-wave-tiers.md`).
- **RADV rebuilds the top level whole** every frame something moves, and its update costs its build; a top level
  built for a fast build traces no slower there.
- **AMD's Windows driver steps a kernel between register tiers** (96, 112, 128, then 141–145, 177, 190–193, 256): a
  peak past about 126 goes to 145. The trace's stages run 32-lane waves, the denoiser and the upscaler 64. It unrolls a
  stage's rays whatever `[[dont_unroll]]` asks, and holds no scratch, so registers and shared memory are what there
  is to lower (`docs/research/2026-11-13-amd-integrated-register-tiers.md`).
- **NVIDIA's compiler trades registers for shared memory**: 96 registers with 4 kB shared ran 11–18% longer than 128
  with 384 bytes. An NVIDIA kernel is judged by its time, and a change that frees registers there is measured before
  it is believed. A regression its compiler chooses is weighed by the frame: no shape of a stage keeps both drivers
  at their best (`docs/research/2026-11-14-nvidia-register-spills.md`).
- **A stage's registers peak in `resolve`, the ground stack's loop and the gather**, not at its rays; what rides the
  stack's loop untouched sets the tier (`docs/research/2026-11-15-mod-maps-registers.md`).
- **A tuple with maps costs the bounce on both cards, whatever the pixels wear**: vanilla under the whole tuple's
  kernels ran its bounce 26–42% longer.

## Open

| idea | evidence | state |
|---|---|---|
| The clamp's tile keeps each surface as its channel packs it, decoded where a short history reads it (`lds1`) | the default clamp declares 10816 bytes and runs 6 waves on RADV | RADV 9216 bytes and 8 waves; handheld flat to +2.5%, desk +2.1 to +8.0%. The AMD iGPU pending; `lds2`–`lds4` no better |
| The wavelet's first level's tile with its normals packed | `atrous` declares 8064 bytes and runs 8 waves on RADV; 6912 would run 9 | waits on the AMD iGPU's answer for `lds1` |
| The wavelet's first level's taps as a loop the driver keeps | 25 taps unrolled into 8568 instructions, 81 registers at 64 lanes (five waves, six at 80) | not tried: whether its registers or its shared tile set its waves is not measured |
| The one-field shadow filters walk their taps in a loop the driver keeps | 93–95 registers on AMD where the two-field ones hold 40–46 | not tried |
| The trace's stages over two tile lists, with a mapped solid and without | the Repack runs every stage's `maps` tuple for ten mapped instances: its bounce 12.6 ms where vanilla's runs 8.7 on the AMD iGPU | not spiked; the eye's stage cannot be split, and a scene mapped throughout pays the second dispatch |
| The direct stage's second lamp ray | `direct` +48% in the guild on the AMD iGPU since the lamps' two strata (`1d7d4140ec`) | not read off the driver's code yet |
| The material's own texture point made past the ground stack | seven registers ride the stack's loop for the dark, emissive and opacity reads | spiked on NVIDIA with a single-map branch change, not judged: landing +11–16% in the guilds there |
| A resolve's own registers with a relief | the PBR stack's `direct` at 128 on a demand of 118, its bounce at 112 on 100 | not tried |
| The water stage's scalar spills; the sprites' composite's four spilled registers | 65–70 spilled SGPRs in every water tuple on RADV | not tried: 1.5–2.4 ms and under 0.1 ms on the handheld |
| A coarser alpha level for the indirect rays' cutout tests | every cutout opaque (picture wrong) bounds it: handheld frame −6.9% at the ship, −6.1% in the guild; cutouts solid for the landing's rays alone moved 45 of 70 pictures | not tried |
| 64-lane trace workgroups | at 128 registers a Turing or Ampere SM's 16-block cap binds the kernels under 128 | needs such a card |
| The à-trous edge stop states its departure, or matches SVGF | — | an `./omw release noise --ab` suite decides |
| NVIDIA's checkpoints on by default | — | their cost on a bench |
| The cell ring adopts by a budget | — | only if a cell's arrival measures a worst frame |
| A staging ring, with DeviceScene recording across submits in its builds | — | for a host peak and a submit length no run has measured yet |

## Refused

Each measured unless it says otherwise; not to be tried again without a new reason.

- **Distant statics merged into one bottom level a cell**: the handheld −5.2% at the ship, but the AMD iGPU +5.5% at
  noon, and the pictures move, since a merged hit is intersected in the structure's space and rounds otherwise.
- **Landings sorted by where they hit**: the handheld's landing −4.5 to −6.7%, the desk's +9–10% outdoors; a path for
  RADV alone would buy 1–2% of its frame for a pass, a list and a vendor switch.
- **Opacity micromaps**: no gain past the noise on the desk and a loss at dawn; RADV offers none (also `AGENTS.md`).
- **The landing resolved once off whichever images hold its hit**: RADV four registers more, NVIDIA 14–18% slower on
  the mapped contents.
- **The layers' frame made again past the stack in every tuple with maps**: the Repack's bounce +6.6–9.5% on AMD; kept
  only where the scene has a relief.
- **Stores ahead of a traversal that gain no wave**: the eye's hit ahead of its resolve, the eye's ray start made
  again past it, the placement's row read again past the stack, the material's fields grouped by where a hit reads
  them, a relief site's axes made again at the lights, a shadow ray held as a narrow record
  (`docs/research/2026-11-09-passage-record.md`) — each a register or more, or no change.
- **The direct stage handing the bounce its surface**, **the ambient ray as a fourth turn of the direct stage**, a
  surface kept apart for later rays, and every ray of a pixel in one dispatch: each costs the direct stage more than
  the bounce gains, or a landing slot a ray (130 MB at 1080p).
- **The fog's history read ahead of its rays** (`fog2`) and the moon's turn skipped with no moons (`fog4`): the read
  past the rays is the desk's and the AMD iGPU's whole gain.
- **Smaller shared tiles in the clamp by decoding the short history's taps a pixel**: 6 → 8 waves on RADV but the clamp
  +7–13%; **the clamp's means as two reads a tap**: +2.2–2.9%.
- **The water's stage tracing the peel again**: desk +0.7%, handheld water 2.95 against 1.91 ms.
- **The pane filter over the layers' tiles alone**: at most 0.3 ms on the handheld (filter off), and any faded actor
  is a layer.
- **A top level built for a fast build**: RADV builds it the same either way.
- **RADV's wave64 for the ray tracing launches** (`RADV_PERFTEST=rtwave64`): the handheld's dawn air 13.29 → 16.75 ms.
- **A fence per acquire slot in the presenter**: the wait moved into `vkQueuePresentKHR`.
- **One moon ray a froxel where the sun is down**: the moons fall under `FOG_SHAFT_FLOOR` at dawn already.
- **Scene instances made by the cell preloader**: 3 ms off a crossing's worst frame for 650 MiB, where NPCs, which no
  preloaded instance reaches, make the worst (`docs/research/2026-11-09-instance-preloading.md`).
- **Capping the shadow ray's nearest search where the penumbra saturates** (analysis): an open ray then walks two
  traversals, and open sky rays are most of them at noon.
- **Declined untried**: `FFX_HALF` in the upscaler (moved a still picture 0.30–0.58 px, `fsrcallbacks.glsl`); `fsr
  accumulate` under 112 registers (AMD's own code, kept as it came); the wavelet's divisions as reciprocals (a few per
  cent of a zone at most, and it rounds otherwise); sorting `SceneDesc::orderLights` (tens of microseconds, and the
  order is the one the frame needs, `docs/research/2026-10-22-light-ordering-share.md`); `[[dont_unroll]]` on the
  clamp's square (the shared memory sets its waves).
