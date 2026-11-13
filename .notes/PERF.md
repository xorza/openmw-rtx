# perf-rtx-renderer-2 — the maps tuples' registers, on the desk and the AMD integrated GPU

Target: the ray tracer's kernels under vanilla, the Fullrest Repack and the PBR stack, on the desk (a high-end
NVIDIA laptop GPU) and the laptop with a low-end AMD integrated GPU (AMD's Windows driver 26.8.1). Started from
`0011749652`. **Stopped after the first batch was built**: nothing is kept, and what stands below is what was
measured and what is left to judge. The branch holds this section alone.

## Hosts

- **Desk**: `openmw-rtxtool bench --size=1920x1080 --window=false --frames=600`, the default suite, run from
  each worktree's build folder. Base against base, one run a side: frame within 2.6%, a GPU zone within 1.8%.
  The release harness from nothing, with the compiler cache, 16.0 GiB peak in 238 s; `cand` after it 1.2 GiB.
- **AMD iGPU**: native MSVC release, the harness alone, 13.5 GiB peak. `bench --size=1280x720 --window=false
  --frames=200` (853x480 traced). Base against base, one run a side: frame within 0.8%, a GPU zone within
  0.8%. A leg is 85 s on vanilla and 150 s on the Repack and the PBR stack.
- **The AMD host's harness runs in the logged-on session alone**: over SSH `bench` and `shot` compile their
  pipelines, log each one's registers and then fail to make a swapchain, `--window=false` or not. So a run
  goes through the scheduled task, and a register count alone can be read off an SSH run's log.
- **Variants a binary each**: a variant's shader files copied over `cand`'s, the harness built and its binary
  kept under the variant's name, so one tree holds several and a leg names the one it runs.

## Numbers at the start (`0011749652`)

Desk, median ms at 1920x1080, two runs a side:

| content | place | frame | p99 | bounce | landing | direct | trace | air | upscale | clamp | temporal | filter0 | shadow | water | media | panes |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| vanilla | ship | 5.98 | 6.62 | 0.39 | 0.57 | 0.52 | 0.54 | 0.25 | 0.45 | 0.40 | 0.22 | 0.33 | 0.23 | 0.09 | 0.10 | 0.07 |
| vanilla | ship-dawn | 6.48 | 7.17 | 0.39 | 0.63 | 0.85 | 0.54 | 0.36 | 0.44 | 0.41 | 0.22 | 0.33 | 0.27 | 0.09 | — | 0.10 |
| vanilla | guild | 5.22 | 5.66 | 0.37 | 0.53 | 0.52 | 0.35 | 0.14 | 0.45 | 0.41 | 0.24 | 0.35 | 0.23 | — | — | 0.12 |
| M[FR] | ship | 7.47 | 8.49 | 0.62 | 0.65 | 0.61 | 0.66 | 0.30 | 0.45 | 0.43 | 0.25 | 0.34 | 0.25 | 0.14 | 0.11 | 0.09 |
| M[FR] | ship-dawn | 7.91 | 8.99 | 0.62 | 0.73 | 0.97 | 0.64 | 0.42 | 0.44 | 0.44 | 0.25 | 0.34 | 0.28 | 0.14 | — | 0.10 |
| M[FR] | guild | 6.45 | 7.24 | 0.72 | 0.96 | 0.69 | 0.48 | 0.30 | 0.43 | 0.42 | 0.26 | 0.35 | 0.21 | — | — | 0.14 |
| PBR | ship | 6.93 | 8.08 | 0.66 | 0.72 | 0.71 | 0.62 | 0.26 | 0.46 | 0.48 | 0.33 | 0.36 | 0.23 | 0.13 | 0.11 | — |
| PBR | ship-dawn | 7.38 | 8.32 | 0.63 | 0.85 | 1.08 | 0.61 | 0.37 | 0.46 | 0.48 | 0.32 | 0.33 | 0.26 | 0.13 | — | — |
| PBR | guild | 6.32 | 7.32 | 0.57 | 0.79 | 0.74 | 0.47 | 0.15 | 0.46 | 0.51 | 0.38 | 0.37 | 0.23 | — | — | 0.14 |

AMD iGPU, median ms at 1280x720, two runs a side:

| content | place | frame | p99 | bounce | landing | direct | trace | air | upscale | clamp | temporal | filter0 | shadow | water | media | panes |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| vanilla | ship | 56.34 | 66.62 | 8.68 | 7.73 | 7.24 | 5.23 | 3.25 | 2.95 | 2.75 | 1.65 | 1.80 | 1.40 | 1.23 | 1.30 | 0.38 |
| vanilla | ship-dawn | 67.21 | 75.06 | 8.97 | 9.32 | 14.21 | 5.83 | 5.59 | 2.92 | 2.82 | 1.65 | 1.80 | 1.63 | 1.27 | — | 0.44 |
| vanilla | guild | 42.02 | 49.45 | 5.84 | 6.56 | 6.59 | 3.14 | 0.90 | 2.80 | 2.69 | 1.58 | 1.86 | 1.40 | — | — | 1.31 |
| M[FR] | ship | 75.12 | 92.96 | 12.54 | 10.42 | 8.41 | 8.38 | 4.05 | 2.94 | 2.95 | 1.97 | 1.89 | 1.56 | 1.85 | 1.62 | 0.48 |
| M[FR] | ship-dawn | 85.64 | 103.13 | 12.85 | 13.88 | 15.13 | 7.28 | 6.42 | 2.86 | 3.00 | 1.97 | 1.89 | 1.72 | 1.84 | — | 0.48 |
| M[FR] | guild | 57.56 | 68.00 | 11.04 | 10.86 | 8.44 | 5.10 | 2.05 | 2.80 | 2.78 | 1.87 | 1.90 | 1.37 | — | — | 1.61 |
| PBR | ship | 62.67 | 74.62 | 11.20 | 9.07 | 7.94 | 5.80 | 3.61 | 2.95 | 3.12 | 2.18 | 1.87 | 1.41 | 1.29 | 1.25 | — |
| PBR | ship-dawn | 77.98 | 88.25 | 12.27 | 12.35 | 15.61 | 6.86 | 6.54 | 2.92 | 3.20 | 2.18 | 1.87 | 1.64 | 1.51 | — | — |
| PBR | guild | 52.35 | 58.16 | 8.43 | 9.36 | 8.58 | 4.38 | 0.95 | 2.81 | 3.13 | 2.20 | 1.93 | 1.39 | — | — | 1.42 |

**The direct stage since the lamps' two strata** (`1d7d4140ec`), against the last session's start on the AMD
iGPU: 6.29 → 7.24 ms at the ship, 12.44 → 14.21 at dawn, 4.44 → 6.59 in the guild; its tuple with no sun
96 → 110 registers and `sun moons sea` 112 → 125.

## What the kernels hold

Registers of the shipped modules for the tuple each content runs, AMD's first, then NVIDIA's with its shared
memory in bytes after the plus:

| stage | vanilla ship | vanilla guild | Repack ship | Repack guild | PBR ship | PBR guild |
|---|---|---|---|---|---|---|
| visibility | 128 / 96+2944 | 112 / 96+1408 | 128 / 128+0 | 128 / 96+3200 | 128 / 128+0 | 128 / 128+0 |
| bounce | 96 / 80+640 | 96 / 80+640 | 128 / 122+0 | 128 / 120+0 | 128 / 120+0 | 128 / 114+0 |
| landing | 125 / 96+3200 | 96 / 96+1152 | 128 / 128+384 | 128 / 128+384 | 128 / 127+0 | 128 / 126+0 |
| direct | 112 / 96+4480 | 110 / 80+4224 | 128 / 96+4992 | 112 / 96+3968 | 128 / 128+1920 | 128 / 128+896 |
| water | 161 / 128+896 | — | 177 / 128+2688 | — | 177 / 128+2688 | — |
| panes | 234 / 168+4736 | 192 / 128+6272 | 243 / 128+6272 | 192 / 128+6272 | 243 / 128+6272 | 192 / 128+6272 |

- **A tuple with maps costs the bounce on both cards, whatever the pixels wear.** Vanilla under the whole
  tuple's kernels (`--variants=false`) on the desk, two runs a side: bounce +35.7%, +36.4%, +26.1% at the ship,
  at dawn and in the guild; direct +14.5%, −0.6%, +23.0%; landing −8.6%, −11.9%, +20.6%; the frame +2.0%,
  +2.2%, +6.8%. NVIDIA's bounce holds 80 to 93 registers and 30 to 35 kB of code without maps and 114 to 124
  and 99 to 114 kB with them; AMD's 96 and 128, and 144 with a dark map's alpha beside them.
- **Where the bounce's registers peak with maps** (AMD's code of `bounce sun moons maps`, the modules that
  count, 9002 instructions against 3123 without maps): 121 live inside the ground stack's layer loop, 108 at
  the hit's fetches ahead of it and 111 in the traversal's cutout test, against 89 at most without maps.
- **What rides the stack's loop untouched there**, 54 registers written before it and read after it: the six
  of the layers' frame, as noughts where the hit is no picture; the material's own texture point, its place
  and four gradients; the stored ray's direction, three; three corner offsets for the vertex colours; the
  turned normals and the plane; the pixel's two coordinates and its stored place; the record's first word and
  its kind; five fields of the material's row; and four constants a branch's other side sets.
- **The landing's kernel holds `resolve` twice**: `storedLandingOf` resolves the solid's images in one branch
  and the landing's own in the other, a branch the dispatch takes one way, and AMD's code shows two layer
  loops of 575 and 584 instructions. NVIDIA's landing is 132 kB a tuple at the mean, its bounce 82.
- **The shadow filter's one-field modules still unroll their walk**: 93 to 95 registers on AMD where the
  two-field ones hold 40 to 46, and 48 against 40 on NVIDIA.
- **AMD's water stage stands a register over a step**: 161 in vanilla's tuples, 177 with maps.

## Ideas

| idea | evidence | verdict |
|---|---|---|
| The landing resolves once, off whichever images hold its hit (`spike/landing-one-resolve`, local) | two copies of `resolve` in its kernel, above | **not judged**. NVIDIA: its tuples with maps 128 registers and 384 bytes of shared memory → 96 and 3968 to 4736, its code 132 → 93 kB at the mean, and its zone **+17.4%, +18.0%** at the Repack's ship and dawn and **+14.4%, +14.6%** at the PBR stack's, the guilds −0.7% and −1.3%, vanilla inside its spread; one run a side, the second round cut. AMD: the variant is built there and not run. No picture checked |
| With it, the material's own texture point made past the stack: the single map's branch under the path that is no stack | seven registers ride the loop, above; ground wears its layers' maps and none of its own (`CellPlacer::adoptGround`), which no assert holds yet | **not judged**. NVIDIA: every landing tuple at 80 or 96 registers, the eye's stage at 128 in 16 tuples of 47 from 24, the direct stage in 12 from 18, the bounce with maps 122 → 116 and with a relief 120 → 112; the guilds' landing **+10.9%** on the Repack and **+15.7%** on the PBR stack and their trace −7.8% and −5.8%; one run a side. AMD not built |
| With both, the layers' frame made again past the stack in every tuple with maps | six registers ride the loop; refused alone last session, tried again for a step it might cross with the point | **not judged**. NVIDIA: the eye's stage at 128 in 12 tuples from 16; the Repack's zones where the row above left them; the PBR stack's leg cut. AMD not built |
| **On NVIDIA fewer registers is no gain by itself**: its compiler trades them for shared memory | the two rows above: 96 registers with 4 kB of shared memory ran 11 to 18% longer than 128 with 384 bytes | a finding, not an idea: an NVIDIA kernel is judged by its time and its shared memory, and a change that frees registers there is measured before it is believed. Whether AMD's driver, which spills nothing here, takes the step to 112 is the open half |
| The bounce's stage carries less through its ray: what it stores of the bounce stored before the traversal and read back after a miss, the record's words read again at the end | 19 registers ride its traversal with maps against 9 without: the lobe's modulation word, the weight, the position, six noughts of the escape | not tried |
| The stack's loop reads the stored ray's direction, the pixel's coordinates and the record again past it | three, three and two registers of the 54 above | not tried |
| The one-field shadow filters walk their taps in a loop the driver keeps | 93 to 95 registers against 40 to 46, above; the guild's shadow zone did not move last session where the exteriors' fell 3 to 7% | not tried |
| The history clamp's fast means as three numbers, the luma saying whether a pixel holds one, as the bounce's clamp now does | `historyclamp.comp` keeps the fourth as a flag: 4096 bytes on AMD where three numbers a texel would hold 3584 | not tried |
| The direct stage's second lamp ray | the zone's rise since the lamps' two strata, above: +48% in the guild on AMD | not tried: which of its registers and its second traversal the rise is has not been read off the driver's code |
| The trace's stages over two lists of tiles, with a mapped solid and without | last session's row; the Repack's bounce stands at 128 registers for ten mapped instances where vanilla's holds 96 | not spiked |

## Left standing

- **Worktrees** `base`, `cand` and `spike` beside the repository under `openmw-perf/` on both hosts, `base` and
  `cand` at `0011749652`; the desk's `cand` build folder also holds the three variants' binaries.
- **`spike/landing-one-resolve`** (local, `ae67ebbfe4`): the first two rows' code. The third row is those plus
  `layerFrameOf` called past the stack whatever `HAS_RELIEF` says.
- **On the AMD host**: the driver's code for every pipeline under each content at the start, in
  `omw-logs/rtx2/isa-start-{vanilla,mfr,pbr}/code`; the first variant's binary in `cand`'s build folder; the
  scheduled task's `run.cmd` now starts `rtx2/job.sh`, its earlier text beside it as `run.cmd.before-rtx2`; and
  the diff an earlier session left uncommitted in its `spike` tree, saved as
  `omw-logs/spike-leftover-2026-11-13.patch`, the tree untouched.

# perf-rtx-renderer — the kernels' registers and shared memory, on the AMD integrated GPU

Target: the ray tracer's kernels — the trace's stages, the denoiser and the upscaler's passes — by the registers
and the shared memory each holds, on the laptop with a low-end AMD integrated GPU (AMD's Windows driver 26.8.1),
the one host this session has, over three kinds of content: vanilla, the Fullrest Repack and the PBR stack.
Started from `0c7e5fdf6f`, then from `84b3089826` once master gained the firefly ceiling.

## Hosts

- **AMD iGPU**: native MSVC release, the harness alone (`omw release build openmw-rtxtool`), in a Remote Desktop
  session. `base` and `cand` under `../openmw-perf/`, each a build of its own, sharing `out/deps` by a junction.
  `omw release bench --size=1280x720 --window=false --frames=200` (853x480 traced), the default suite, base and
  cand interleaved after a throwaway leg; the profiles by `--replace=config --config=<profile>`.
- **Build RAM peak**: 7.3 GiB of commit for the harness from nothing, alone, in 17 minutes (13.0 GiB with a
  290-step build of the main tree and a harness run beside it). One build at a time.
- **Noise** (base against base, two runs a side): a GPU zone's median within 0.1 to 1%, the frame within 0.3%
  on vanilla and PBR and 2 to 4% on the Fullrest Repack, whose host rows move; a zone's p99 within 10%.
- **The census costs registers**: `shot` and `--pipeline-code` run the modules that count, `bench` the shipped
  ones, and a stage holds up to 17 registers fewer shipped (`direct` 96 against 103, the whole `bounce` 144
  against 145). The shipped figures are the ones below unless a line says the census's.
- **What a kernel holds** is the driver's own answer (`numUsedVgprs`, `ldsUsageSizeInBytes`,
  `scratchMemUsageInBytes`), which a run's log prints a pipeline a line; its code is `--pipeline-code=<dir>`.
  No kernel holds scratch on this driver, so the registers and the shared memory are what there is to lower.
- **Liveness off the driver's code**: a script over the ISA text (labels, branches, a backward dataflow over the
  vector registers) gives each kernel's most registers live, where, and what each loop carries untouched —
  written before it, named nowhere in it, read after it — with the instruction that wrote each. Its count of
  registers named is the driver's `numUsedVgprs` on every kernel.

## What the driver's code says (`0c7e5fdf6f`)

- **The compiler steps a kernel between tiers.** The trace's stages (32 lanes) land on 96, 112, 128 exactly, or
  on 141 to 145, 177, 190 to 193 and 256, with 5 to 15 fewer live at the peak: a kernel whose peak passes about
  126 goes to 145. An RDNA 2 SIMD holds 1024 registers a 32-lane wave in steps of 16 (Mesa's
  `aco_ir.cpp`, `vgpr_alloc_granule`), so 128 is eight waves, 144 seven and 145 six, if AMD's driver
  allocates as Mesa's does.
- **The denoiser and the upscaler run 64-lane waves** (512 registers in steps of 8), the stages 32.
- **A stage's registers peak in `resolve` and the gather, not at its rays.** `direct` holds 70 to 100 live at
  its intersections and 96 to 134 in the ground stack's loop, the maps' reads and the lobe's arithmetic.
- **What rides through the ground stack's loop untouched**: the vertex tint (4), the material's own texture
  point (7), the layers' tangent and bitangent (6), the interpolated normal beside the turned one (3), and a
  relief site's three axes (9); and through a stage's first traversal, the asks of the rays after it, about
  ten registers a ray, half of them a `noRay`'s constants a select keeps.
- **The driver unrolls a stage's rays**: `direct`'s three turns are three traversals in its code, whatever
  `[[dont_unroll]]` asks.

## Numbers at the start (`84b3089826`, 1280x720, 853x480 traced)

Median ms, two runs a side.

| content | place | frame | p99 | bounce | landing | direct | trace | air | upscale | clamp | temporal | filter0 | shadow | water | media | panes |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| vanilla | ship | 55.98 | 63.26 | 8.69 | 7.73 | 6.29 | 5.21 | 3.24 | 2.96 | 2.75 | 1.66 | 1.80 | 1.48 | 1.25 | 1.30 | 0.37 |
| vanilla | ship-dawn | 65.01 | 76.20 | 8.95 | 9.30 | 12.44 | 5.78 | 5.57 | 2.93 | 2.83 | 1.65 | 1.79 | 1.75 | 1.26 | — | 0.43 |
| vanilla | guild | 39.92 | 46.27 | 5.97 | 6.75 | 4.44 | 3.12 | 0.89 | 2.81 | 2.67 | 1.57 | 1.86 | 1.40 | — | — | 1.19 |
| no bounce | ship | 40.41 | 50.66 | 4.78 | — | 6.25 | 5.19 | 3.23 | 2.95 | 2.40 | 1.57 | 1.71 | 1.49 | 1.24 | 1.30 | 0.37 |
| no bounce | ship-dawn | 49.14 | 58.10 | 4.77 | — | 12.43 | 5.79 | 5.57 | 2.92 | 2.47 | 1.58 | 1.71 | 1.76 | 1.25 | — | 0.43 |
| no bounce | guild | 26.41 | 32.89 | 2.20 | — | 4.43 | 3.12 | 0.89 | 2.80 | 2.28 | 1.51 | 1.95 | 1.40 | — | — | 1.18 |
| M[FR] | ship | 74.78 | 96.41 | 12.59 | 10.45 | 7.82 | 8.44 | 4.04 | 2.94 | 2.97 | 1.95 | 1.89 | 1.63 | 1.83 | 1.62 | 0.47 |
| M[FR] | ship-dawn | 84.87 | 103.69 | 12.89 | 13.82 | 13.87 | 7.33 | 6.40 | 2.86 | 3.03 | 1.95 | 1.88 | 1.83 | 1.83 | — | 0.45 |
| M[FR] | guild | 54.23 | 66.74 | 11.10 | 10.85 | 5.73 | 5.04 | 2.04 | 2.79 | 2.74 | 1.86 | 1.89 | 1.36 | — | — | 1.59 |
| PBR | ship | 62.67 | 70.95 | 11.26 | 9.14 | 7.19 | 6.79 | 3.57 | 2.97 | 3.12 | 2.17 | 1.86 | 1.47 | 1.26 | 1.23 | — |
| PBR | ship-dawn | 78.22 | 88.41 | 12.27 | 12.35 | 14.74 | 8.13 | 6.47 | 2.94 | 3.20 | 2.18 | 1.87 | 1.76 | 1.43 | — | — |
| PBR | guild | 49.85 | 57.27 | 8.37 | 9.31 | 6.35 | 4.72 | 0.94 | 2.82 | 3.11 | 2.18 | 1.92 | 1.39 | — | — | 1.43 |

The tuples, by the record's counts: vanilla wears no map; the Repack 10 mapped instances at the ship and 6 in
the guild, no relief; the PBR stack 66 556 mapped and 19 184 with a relief at the ship, 1058 and 783 in the guild;
no dark alpha anywhere. So the Repack runs `sun sea maps`, `sun moons sea maps` and `maps`, and the PBR stack the
same with `relief`.

**What a tier is worth here**: vanilla under the whole tuple's kernels (`--variants=false`), which run the same
rays through 144 registers where the frame's own tuples hold 96 to 128: bounce +42% at every place (96 to 144,
ten waves to seven), trace +21 to +25%, direct +13 to +23%, landing +9 to +25%, the guild's panes +35%; the
frame +12 to +17%. The Repack's and the PBR stack's bounce stand where that one does.

## Tip against the start (`fc51943de9` → `1de3401475`, 2026-11-13)

Median ms at 1280x720, two runs a side, interleaved; the start is the branch's start with the record's counts.

| content | place | frame | trace | direct | clamp | filter0 | shadow |
|---|---|---|---|---|---|---|---|
| vanilla | ship | 55.81 → 55.34 (−0.8%) | 5.23 → 5.23 | 6.29 → 6.24 (−0.8%) | 2.75 → 2.59 (−6.1%) | 1.79 → 1.79 | 1.49 → 1.42 (−5.1%) |
| vanilla | ship-dawn | 65.32 → 64.76 (−0.9%) | 5.79 → 5.80 | 12.45 → 12.30 (−1.2%) | 2.83 → 2.66 (−6.0%) | 1.78 → 1.79 | 1.76 → 1.65 (−6.6%) |
| vanilla | guild | 40.08 → 39.87 (−0.5%) | 3.13 → 3.12 | 4.45 → 4.47 | 2.67 → 2.47 (−7.8%) | 1.86 → 1.86 | 1.40 → 1.40 |
| no bounce | ship | 40.92 → 40.24 (−1.7%) | 5.20 → 5.22 | 6.25 → 6.19 (−0.9%) | 2.40 → 2.11 (−12.3%) | 1.72 → 1.64 (−4.7%) | 1.48 → 1.41 (−5.3%) |
| no bounce | ship-dawn | 48.55 → 48.19 (−0.7%) | 5.78 → 5.79 | 12.43 → 12.29 (−1.1%) | 2.48 → 2.19 (−11.9%) | 1.71 → 1.64 (−4.2%) | 1.75 → 1.64 (−6.7%) |
| no bounce | guild | 26.47 → 26.00 (−1.8%) | 3.13 → 3.12 | 4.42 → 4.46 | 2.28 → 1.96 (−13.9%) | 1.95 → 1.85 (−4.7%) | 1.38 → 1.40 |
| M[FR] | ship | 73.46 → 72.87 (−0.8%) | 8.44 → 8.40 | 7.83 → 7.80 | 2.96 → 2.77 (−6.4%) | 1.88 → 1.89 | 1.64 → 1.58 (−3.4%) |
| M[FR] | ship-dawn | 83.95 → 84.12 (+0.2%) | 7.28 → 7.27 | 13.92 → 13.95 | 3.02 → 2.83 (−6.3%) | 1.89 → 1.88 | 1.83 → 1.74 (−4.8%) |
| M[FR] | guild | 55.53 → 54.98 (−1.0%) | 5.08 → 5.08 | 5.80 → 5.84 | 2.77 → 2.55 (−8.0%) | 1.90 → 1.89 | 1.38 → 1.38 |
| PBR | ship | 62.53 → 62.18 (−0.6%) | 6.77 → 5.82 (−14.0%) | 7.19 → 7.17 | 3.12 → 2.95 (−5.4%) | 1.87 → 1.87 | 1.48 → 1.42 (−4.2%) |
| PBR | ship-dawn | 78.43 → 76.95 (−1.9%) | 8.11 → 6.82 (−15.9%) | 14.80 → 14.81 | 3.20 → 3.02 (−5.6%) | 1.87 → 1.87 | 1.77 → 1.65 (−6.7%) |
| PBR | guild | 49.63 → 48.52 (−2.2%) | 4.71 → 4.38 (−7.2%) | 6.25 → 6.14 (−1.7%) | 3.11 → 2.89 (−7.2%) | 1.92 → 1.92 | 1.40 → 1.39 |

The frame's GPU zones together: vanilla −0.4 to −0.6%, with no bounce −1.3 to −1.4%, the Repack −0.4 to −0.5%, the
PBR stack −1.7 to −1.9%. Its bounce and landing stand inside their spread on every content. What moved the other
way: the PBR stack's water stage +3.0% and +4.0% (0.04 and 0.06 ms), and `panes` at the ship +3.5% on vanilla and
+4.0% on the Repack (0.01 and 0.02 ms).

- **Pictures**: `shot --against` at 1280x720 with the map tiles, the upscaler off: every vanilla view the same,
  and six views of the Repack and of the PBR stack the same. On a high-end NVIDIA laptop GPU the trace is the
  same on every frame and no picture moved, and the shadow denoiser's images differ in the last place wherever
  both fields filter (the research note's last section).
- **`RtxVisibilityKernelsTest`**: its four pass on this card with AMD's `visibility` ceilings at the rule's 128.
- **The gate did not finish on the AMD iGPU's host**: its format check, spellings, lint and driver tests passed,
  and the session stopped it for memory in its release compile of every target. It ran clean on a high-end
  NVIDIA laptop GPU, whose kernels hold their ceilings; RADV's are not measured.
- **Commits**: `fc51943de9` the record's counts, `01f8050944` the research note, `2e35ad60be` the vertex
  colours, `714569e43d` the relief's frame, `0ea98c9f7d` the shadow filter's walk, `c5319e33a1` the clamp's
  ring, `1de3401475` the fill's tile.

## Ideas

Every figure is `docs/research/2026-11-13-amd-integrated-register-tiers.md`'s. Every kept change is the same
arithmetic in the same order.

| idea | evidence | verdict |
|---|---|---|
| The vertex colours fetched past the ground stack | read before the stack, four registers of them ride its loop untouched, where a stage's registers peak | **kept**: every `visibility … relief` 141 → 128 without the sea and 145 → 141 with it; the PBR stack's trace −8.1%, −8.7%, −6.2%; the Repack and vanilla inside their spread |
| The layers' frame made again past the stack | its tangent and bitangent ride the loop, six registers, for one use after it | made again in every tuple, **refused**: every `visibility … relief` → 128 and the PBR stack's trace −13 to −17%, but the Repack's bounce +6.6 to +9.5%, its traversal loop 24 instructions longer at 125 registers from 128. **Kept made again where the scene has a relief alone**, with the colours above: PBR trace −14.0%, −15.9%, −7.2%, its water stage +3 to +4% (0.05 ms), the Repack's bounce −0.4 to −0.6% |
| A relief site as its tangent, handedness and a flag, its other axes made again at the lights | nine registers of axes from the hit to the gather | **refused**: no kernel's registers moved and no zone's time |
| Two independent statements of `resolve` swapped | whether any change to a module moves the driver's code | no kernel's registers moved and no zone's time: what the rows above measure is theirs |
| No fill's tile in shared memory where the fill is the bounce whole, the wavelet's (`FILL_TILE`) | `atrous-wide-compose` declared the fill's 12×12 `vec3` and read none of it | **kept**: first level −4.0 to −4.8% with no bounce, over three rounds (8192 → 6656 bytes) |
| The same in the clamp (`FILL_SPAN`) | the no-fill clamps likewise | alone the clamp **+3.2 to +5.1%** with no bounce (12288 → 10752 bytes, 93 → 70 registers); **kept with the split below**: −11.9 to −13.9% (9216 bytes) |
| The clamp's fast means as the ring's luminance and the square's YCoCg | the ring reads the luminance alone, of 256 texels of 16 bytes | two reads a tap **refused**: the clamp +2.2 to +2.9% on every content. **Kept with the square's luma saying whether a pixel holds a surface, one read a tap**: −5.4 to −8.0% on every content (12288 → 11264 bytes, 85 → 81 registers) |
| The shadow filter's two fields in one walk of the taps | the two-field module decoded each tap's normal and weighed its plane once a field | **kept**: shadow −3.2 to −7.0% under a sky on every content over five rounds, the guild inside its spread; the two-field modules 101–102 → 40–46 registers |
| The trace's stages over two lists of tiles, those with a mapped solid and those without | the Repack runs every stage's `maps` tuple for ten mapped instances: its bounce 12.6 ms where vanilla's own tuple runs 8.7, and vanilla under the whole tuple +42% | not spiked: a list a stage and a record's bit as the water's and the layers' are, the lean tuple where no pixel of a tile wears a map; the eye's stage cannot be split, since it finds the surface. The gain is the Repack's alone, and a scene mapped throughout pays the second dispatch |
| A stage's later rays' asks not carried through its first traversal | about ten registers a ray ride the first traversal, half of them a `noRay`'s constants a select keeps; a held lamp's direction, distance and radius can be made again off its row | not tried: the stages' peaks stand in `resolve`, so their tiers do not follow the traversal's count until those fall |
| The material's own texture point made past the stack | seven registers ride the stack's loop for the dark, emissive and opacity reads after it | not tried: the single map's branch shifts the point before the stack, so it moves only with that branch |
| `fsr accumulate` under 112 registers | 113 registers at 64 lanes, four waves to 120 and still four to 104 | not tried: the pass is AMD's own code, which the tree keeps as it came, and a wave more needs 96 |
| The wavelet's first level's taps as a loop the driver does not unroll | 81 registers at 64 lanes, five waves, six at 80; its 25 taps are unrolled into 8568 instructions, 47 to 50 registers live between taps and 70 to 76 inside one, and the shadow filter's walk left unrolled runs at 40 | not tried: whether its registers or its 8192 bytes of shared tile set its waves is not measured, and `[[dont_unroll]]` did not hold the driver off a stage's rays |

# perf-bounce-modes — the frame with no bounce, with one bounce ray and with three

Target: the frame under `--indirect=false`, `--bounce-rays=1` and `--bounce-rays=3`, on the desk (a
high-end NVIDIA laptop GPU), the AMD handheld (RADV, its clock held) and the laptop with a low-end AMD
integrated GPU (AMD's Windows driver). Started from `479e4fdd85`.

## Hosts

- **Desk**: `./omw release bench`, 1920x1080, 1200 frames, three interleaved rounds. A/A: frame within 1%, and
  3% with no bounce; one host's zones within 2%. A turn of this session beside a run moved its frames by 5%, so
  each run ended the turn.
- **Handheld**: built in the release-base container, staged by `omw release remote`, 1280x800, 300 frames,
  two interleaved rounds. A/A: frame within 0.1%.
- **AMD iGPU**: native MSVC release, through the scheduled task in the logged-on session, 1920x1080, 200
  frames, two interleaved rounds. A/A: frame within 0.6%. A build beside a run moved a frame by 48%.
- **Build RAM peaks**: desk release from nothing 25.8 GiB, debug with the tests 25.4 GiB and the rest cached;
  container 23.7 GiB; AMD iGPU 9.7 GiB.
- **The gate did not pass in a worktree**, so it ran in a plain clone of the branch.

## Tip against the start (`479e4fdd85` → `c685154a79`, 2026-11-06)

Frame median ms.

| mode | place | handheld | AMD iGPU | desk |
|---|---|---|---|---|
| no bounce | seyda-neen-ship | 62.05 → 59.15 (−4.7%) | 84.63 → 80.13 (−5.3%) | 4.680 → 4.569 (−2.4%) |
| no bounce | seyda-neen-ship-dawn | 75.00 → 72.14 (−3.8%) | 102.22 → 97.68 (−4.4%) | 5.126 → 4.947 (−3.5%) |
| no bounce | balmora-mages-guild | 33.31 → 31.12 (−6.6%) | 57.55 → 52.74 (−8.4%) | 3.864 → 3.637 (−5.9%) |
| no bounce | vivec-canalworks | 26.80 → 24.80 (−7.4%) | 46.40 → 42.10 (−9.3%) | 3.464 → 3.265 (−5.8%) |
| one ray | seyda-neen-ship | 86.20 → 85.07 (−1.3%) | 116.11 → 113.87 (−1.9%) | 6.174 → 6.146 (−0.5%) |
| one ray | seyda-neen-ship-dawn | 99.53 → 98.62 (−0.9%) | 136.80 → 135.71 (−0.8%) | 6.600 → 6.569 (−0.5%) |
| one ray | balmora-mages-guild | 53.44 → 53.19 (−0.5%) | 87.01 → 84.95 (−2.4%) | 5.296 → 5.276 (−0.4%) |
| one ray | vivec-canalworks | 38.61 → 38.44 (−0.5%) | 65.21 → 63.69 (−2.3%) | 4.546 → 4.534 (−0.3%) |
| three rays | seyda-neen-ship | 145.39 → 142.04 (−2.3%) | 194.88 → 188.17 (−3.4%) | 8.613 → 8.571 (−0.5%) |
| three rays | seyda-neen-ship-dawn | 160.16 → 156.80 (−2.1%) | 223.67 → 217.53 (−2.7%) | 9.150 → 9.138 (−0.1%) |
| three rays | balmora-mages-guild | 94.16 → 93.86 (−0.3%) | 150.03 → 143.03 (−4.7%) | 7.663 → 7.617 (−0.6%) |
| three rays | vivec-canalworks | 60.68 → 60.35 (−0.5%) | 101.43 → 97.38 (−4.0%) | 6.173 → 6.133 (−0.6%) |

The desk's one and three rays stand inside its noise; its bounce zone fell 3 to 10% under them.

## Ideas

| idea | evidence | verdict |
|---|---|---|
| A kernel of its own for a frame with no bounce (`unbounced.comp`) | the branch in `bounce.comp` held 112 VGPRs on RDNA 2; apart, 80 and 96, and `resolve`'s fetches left to the compiler | **kept `e46b461ed7`**: bounce zone −12 to −26% with no bounce, −1 to −14% with rays (Commit's table); every picture to the bit |
| No split read where the frame holds no sea | a split comes off a water path alone | kept in `e46b461ed7` |
| The wide level composes the frame where it is the only level | the composite read back what it wrote | **kept `cce1af01b3`**: composite + filter0 −12 to −33%; every picture to the bit |
| No fill kept apart from the bounce with no bounce | the fill is the bounce whole, carried twice | **kept `c685154a79`**: clamp −8 to −26%, temporal −5 to −14%, filter0 −5 to −10%; still pictures to the bit but the fill's own images, `noise` the same on a strafe and after a cut |
| The direct stage hands the bounce its surface over the stored hit (`spike/handed-surface`) | `resolve` re-fetched what direct resolved, and no memory is added | **refused**: handheld bounce −2 to −4% with rays, −21% with none; desk direct +4 to +22% for the three stores, frame +0.2 to +1.7% with one ray; AMD iGPU direct +2 to +16%, canal +0.7%. Pictures to the bit |
| The ambient ray a fourth turn of the direct stage, no stage of its own (`spike/ambient-in-direct`) | one resolve where two ran | **refused**: handheld ship direct and bounce 16.5 → 20.4 ms, frame +4.6% (128 VGPRs, 8 waves in the tuple with the sea), guild level, canal −6%; desk level |
| Surface kept apart for rays after the first, written by ray 0 | three rays resolve the solid three times | refused by analysis from the handed surface's numbers: a third less gain than it, and the same stores |
| Tracing all of a pixel's rays in one dispatch | one resolve a pixel | refused by analysis: a landing's hit has one slot a pixel, so each ray's landing runs before the next ray; three slots cost 130 MB at 1080p |
| Pane filter over the layers' tiles alone | upper bound, filter off (picture wrong): handheld frame −0.6 to −1.5%, 0.3 ms | refused by analysis: the bound is small, and history off a tile needs a clear. Skipping it needs a frame with no layer, and any faded actor is one |
| Smaller shared tiles in the clamp and the wide level (`spike/smaller-tiles`) | on RDNA 2 a kernel's waves follow its shared memory: 11264 bytes 6 waves, 9216 and 8192 8, 7168 and 6144 10, 4096 16 | **refused**: clamp 6 → 8 waves, but decoding the short history's taps per pixel gave ship and guild clamp +7 to +13%, p99 worse; handheld canal alone −7 to −19%; desk frame +0.5%. Pictures to the bit |
| `[[dont_unroll]]` on the clamp's square | Pre-Sched VGPRs 52 against 168 | not measured: the 168 follows the wave count the shared memory sets, and unrolling does not set it |

# perf-rtx — the ray tracer's frame on three hosts

Target: the ray tracer's frame (the GPU's stages and passes, and the host's walk), on the desk (a
high-end NVIDIA laptop GPU), an AMD handheld (RADV, its clock held) and a laptop with a low-end AMD
integrated GPU (AMD's Windows driver). Started from `f361c9022c`.

## Hosts

- **Desk**: `./omw release bench` (default suite, 1920x1080, 1280x720 traced), three interleaved rounds
  after a warm-up leg. A/A (2026-11-02): frame medians within
  ±1 % a pair and ±0.1 % over three; zones ±1–2 %, `direct` bimodal (0.42 or 0.45 ms, dawn 0.67 or
  0.77); p99 ±15 %.
- **Handheld**: release build in the release-base container, staged by `omw release remote`, then run
  under omw's own remote script, 1280x800 (853x533 traced). A/A: frame and every zone
  within 0.1 % (`panes` 0.3 %), p99 within 2 %.
- **AMD iGPU**: native MSVC release build, run in the logged-on session through a scheduled task:
  an SSH session's window has no surface a swapchain takes. 1920x1080.
- **GPU-bound everywhere**: `wait` is 4 of 6 ms on the desk and 77 of 88 ms on the handheld; the host's
  walk, place and upload are under 1 ms on the desk and 5 ms on the handheld.
- **Host rows**: the handheld's within 1 % (update 0.2 %), the AMD iGPU's within 1 %; the desk's move
  10–20 % between identical runs, so a host change is judged on the handheld and the AMD iGPU.
- **Build RAM peaks**: desk release from nothing 25.8 GiB (perf-trace), a build of cached objects
  0.4 GiB, the debug flavour with its tests from nothing 33.2 GiB; the container's from nothing
  24.3 GiB; the AMD iGPU's from nothing 9.9 GiB. One build at a time on each host.
- **AMD iGPU noise** (A/A, two rounds): frame within 0.2 % (dawn 1.1 %), zones within 0.2 % (dawn
  0.7 %), p99 ±18 %.
- **The AMD iGPU's host is slow**: walk 11.7, place 5.0, upload 4.2, update 23.4 ms a frame at the
  ship, against the handheld's 3.3, 0.85, 0.75, 6.2 — 3.5 times the handheld's CPU. Under the GPU's 115 ms it costs no frame
  yet.

## Numbers at the start (`f361c9022c`)

Handheld, median ms:

| place | frame | p99 | trace | bounce | landing | direct | air | tlas | upscale | media | clamp | filter0 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| seyda-neen-ship | 88.44 | 97.88 | 15.88 | 14.98 | 12.03 | 9.73 | 7.87 | 3.62 | 3.29 | 3.08 | 2.92 | 2.74 |
| seyda-neen-ship-dawn | 103.37 | 117.48 | 16.06 | 15.13 | 13.94 | 19.06 | 13.51 | 3.62 | 3.28 | — | 3.01 | 2.74 |
| balmora-mages-guild | 58.72 | 63.60 | 5.16 | 9.19 | 16.19 | 6.32 | 1.42 | 0.39 | 3.20 | — | 2.79 | 2.91 |

Desk, median ms: ship 6.04 (trace 0.68, landing 0.61, direct 0.44, upscale 0.44, bounce 0.38), dawn
6.52 (direct 0.72, trace 0.69, landing 0.67), guild 5.32 (landing 0.76, upscale 0.45, bounce 0.37).

## Tip against the start (`421d2dba38` → `2861dd2fd9`, 2026-11-02)

Frame median ms; the handheld two runs a side, the AMD iGPU two rounds, the desk four, interleaved.

| place | handheld | AMD iGPU | desk |
|---|---|---|---|
| seyda-neen-ship | 88.45 → 84.12 (−4.9 %) | 120.81 → 114.23 (−5.4 %) | 6.039 → 6.011 (−0.45 %) |
| seyda-neen-ship-dawn | 103.31 → 99.36 (−3.8 %) | 148.35 → 137.72 (−7.2 %) | 6.520 → 6.475 (−0.7 %) |
| balmora-mages-guild | 58.72 → 58.70 (0.0 %) | 90.30 → 90.22 (−0.1 %) | 5.323 → 5.356 (+0.6 %, GPU zones' sum 4.818 → 4.820) |

Host update at the ship and dawn: handheld −7.2 % / −7.1 %, AMD iGPU −8.5 % / −6.0 %. Gate clean; CI
https://github.com/xorza/openmw-rtx/actions/runs/41385212538 green.

## Ideas

| idea | evidence | verdict |
|---|---|---|
| The trace's water walk as a stage of its own, over the tiles where the eye met the sea | RADV: `visibility … sea` 192 VGPRs (5 waves), 12.8–14.6 k instructions, 20–37 spilled SGPRs, against 128 (8 waves) and 5.2–5.8 k without the sea; the integrated GPU 234–247 against 112–128. Upper bound, the walk cut (picture wrong): handheld ship trace 15.88 → 10.02 ms, VGPRs 128 | **kept `3dd8dc8363`** (Decisions) |
| The water's stage traces the peel again | the simplest form | rejected: desk frame +0.66 / +0.78 % (water 0.135 ms against 0.111 restored), handheld water 2.95 against 1.91 |
| The media stage's phases a loop each (eye ray, eye column, water walk to its leg, the leg's march) | RADV `media … sea` 256 VGPRs, 37 SGPRs spilled; the integrated GPU 256; one loop over every walk's record | **kept `3a439f944c`**: handheld media 3.06 → 2.52 ms (VGPRs 192, no spill), ship frame −0.7 %; AMD iGPU media −3.6 % (225 VGPRs, still four waves); desk media 0.118 → 0.103; frames elsewhere inside the noise; no picture moved |
| A moved reference no held cell holds is searched for once (`CellPlacer::moveReference`) | desk profile: `CellPlacer::standAt` 1.95 % of samples, about 8 % of the main thread, under `PhysicsSystem::moveActors`: every actor the physics moves searched every held cell, each move | **kept `2861dd2fd9`**: handheld update −7.6 % ship, −7.2 % dawn (−0.47 / −0.43 ms); AMD iGPU −8.1 % / −9.9 % (−1.9 / −2.2 ms); guilds unchanged; frames unchanged (GPU-bound) |
| The NVIDIA driver's `[vkrt] Analysis` thread | 47 % of the desk's CPU samples: `clock_gettime` and `pthread_rwlock` in a loop | not the renderer's: the driver's own thread, on a core of its own |
| `SceneDesc::orderLights` sorts ~917 lamps every frame | 0.4 % of the desk's samples | not tried: tens of microseconds, and the order is the total order the frame needs |
| Distant statics merged into a structure a cell (redesign) | at the ship 57 054 of 64 179 instances are distant statics, 3.12 M placed triangles (55 a placement) of 5.84 M; RADV rebuilds the top level whole every frame something moves (Mesa 26.2 `vk_acceleration_structure.c`: a top level is always HPLOC, an update only a bottom level's). Upper bound, the distant statics off (picture wrong), handheld: tlas 3.62 → 0.71 ms, frame −6.7 % ship, −18.1 % dawn (direct −40 %, air −27 %, landing −18 %), guild −0.5 % | spiked, decided below (Decisions); the spike's first finding: 41 180 of the statics are two-sided or flipped, so a group is keyed by its facing as well. Before the spike: needs a hit's instance as custom index + geometry index, facing groups (cull and flip are an instance's flags), gates and moved references rebuilding a cell's structure, and a hit computed in another space, which moves pictures by rounding |

## Decisions

- **The sea's walk is a stage of its own, over the tiles where the eye met the sea** (`3dd8dc8363`,
  `docs/architecture.md` "The sea's walk, a stage of its own", `research/2026-11-02-water-stage.md`).
  Spiked on `spike/water-stage` (local). Handheld frame −4.2 % ship, −3.8 % dawn; AMD iGPU −5.3 %, −7.0 %;
  desk and every interior inside the noise; no picture moved. The shape is the tree's own
  classify-and-dispatch (the media and the layers' tile lists, FidelityFX's reflections) and
  Laine, Karras and Aila's case against one kernel carrying every walk (2013). Refused: the peel
  traced again in the water's stage (desk +0.7 %); the split on AMD alone (two builds of one
  kernel, which the tree refuses).

- **Statics merged into one structure a cell: refused as spiked** (spike
  `spike/merged-statics`, local). A cell's settled statics (8192 units square, one mask, one facing
  class) become one bottom level whose geometries carry their placements (`transformData`); its
  instance's custom index names a run of a merged table, and a hit's instance row is that run's
  entry at the geometry index (`instanceRowOf`). At the ship 22–29 k rows went into 350 structures.
  handheld: frame −5.2 % ship, −7.8 % dawn, −1.5 % guild (tlas 3.62 → 0.46 ms, trace −11 to −17 %,
  bounce +3 to +4 %, direct +8 % at the ship and −4 % at dawn); desk: −0.2 % ship (inside the
  noise; direct +8 %, air +9 %), −1.6 % dawn, −3.0 % guild (bounce and trace −14 %); AMD iGPU:
  **+5.5 % ship** (bounce +20 %, direct +14 %, air +14 %), −6.0 % dawn, −2.2 % guild. **Refused:
  it regresses the integrated GPU's noon exterior**, where a cell's one structure mixes opaque and cutout
  geometry over 8192 units, and **the pictures move**: a merged hit is intersected in the structure's space, so its
  barycentrics and distance round otherwise; raw frames at a held exposure moved on 0.97 % of
  Balmora's pixels (5 past 5 %), but the denoised shots moved on 5–99 % of the pixels, worst
  165–202/255, which `shot` does not call the denoiser's noise. No instanced hit can be
  reproduced bit for bit from another space. What landing takes beyond the spike: groups made by
  the cell placer from its held cells and not by settled rows; a gate, an enable, a blacklist or a
  move rebuilding its cell's structure; the structures in the content budget with their refusals
  and compaction; and the user's word on a picture that moves by rounding. The shape is NVIDIA's
  advice to merge static geometry whose instances overlap
  ([Best Practices: Using NVIDIA RTX Ray Tracing](https://developer.nvidia.com/blog/best-practices-using-nvidia-rtx-ray-tracing/)) and the
  `transformData` path the Vulkan specification gives a bottom level's geometries; refused before
  on the desk (`research/2026-10-22-instances-per-placed-model.md`, a top-level gain of 0.17 ms),
  where RADV's whole rebuild of the top level every frame was not measured.

## Tried on perf-trace (2026-10-31)

| idea | evidence | verdict |
|---|---|---|
| Sort landings by hit position | coherent-landing spike: landing −3.4 / −2.9 ms on the handheld, −24 % / −18 % on the desk | decided below (Decisions): refused |
| Rate the landing's two rays indoors | each ray 6–9 ms of landing; `look.h` refuses indoor rates by design | picture move not slight: document only |
| p99 is twice the median on the handheld | one present in 31 blocked 58–60 ms in Mesa's headless swapchain under SDL's offscreen driver | fixed in the harness (`bfecf2c3c0`): `omw remote` runs on the handheld's Wayland session; not the renderer's |
| Presenter: a fence per acquire slot in place of the wait on an image's last blit | the wait it removed moved into `vkQueuePresentKHR` | rejected: no gain, the stall was the WSI's |
| A drawn shadow bit's ray stops at the first occluder | its penumbra is `SHADOW_PENUMBRA_DRAWN` whatever stopped it | kept `9ff5929cda`: handheld dawn direct −22.8 %, desk dawn direct −9.9 %, pictures identical |
| Cap the nearest search at the distance where the penumbra saturates the shadow filter (4 footprints), then any-hit beyond | an occluder past it filters as one at it | rejected by analysis: an open ray then walks two traversals, and open sky rays are most of them at noon |
| `direct` at dawn 34 ms on the handheld | 12.7 ms at noon; every sky bit drawn at dawn and each traced to its nearest occluder | answered by `9ff5929cda`; 25.4 ms left |
| Update the top level in place where only transforms and refits moved | `tlas` 3.45 ms every exterior frame on the handheld, 0.40 on the desk, rebuilt whole | kept `7b147fc972`: desk tlas 0.40 → 0.09, frame −3.7 to −3.9 % outside; handheld unchanged (RADV's update costs its build) |
| Every cutout opaque (upper bound, picture not acceptable) | 39 905 cutouts at the ship; every ray asks each cutout candidate's alpha | measured: handheld frame −6.9 % ship, −6.1 % guild; desk ship −9 %. Next: opaque cutouts for indirect rays alone, and a coarser alpha level for them |
| Cutouts solid for the landing's two rays alone (for review) | the landing asks each cutout candidate's alpha for its ambient and its lamp ray | **not landed, picture move not slight**: handheld frame −2.0 % ship, −2.6 % guild (landing −10.2 % / −7.9 %); desk shot: 45 of 70 pictures moved, seyda-neen-boulder 57/255 on 53 % of the pixels, the ship 30/255 on 16 %. |
| One moon ray a froxel only where the sun is down | thought the dawn air paid a moon's ray beside the sun's | rejected: no picture moved and the handheld's air did not move (13.39 against 13.39 ms): the moons fall under `FOG_SHAFT_FLOOR` at dawn already; the dawn air is the low sun's grazing ray |
| RADV's wave64 for the ray tracing launches (`RADV_PERFTEST=rtwave64`) | the fog's two launches are the only `vkCmdTraceRaysKHR` work | rejected: handheld dawn air 13.29 → 16.75 ms |
| `FFX_HALF` for the upscaler | RDNA 2 runs packed halves | not retried: `fsrcallbacks.glsl` records it moved a still picture 0.30–0.58 px |
| The wavelet's per-tap divisions as multiplies by a reciprocal | the first level is ALU-bound (VALU 91 %) | not tried: the taps are already tiled in shared memory with the facing power as squarings; what is left per tap is two `exp`, a `sqrt` and two divisions, a few per cent of a 2.9 ms zone at most, and a reciprocal rounds differently |
| The landing's and the direct stage's light worked out before their rays, the rays at one site in a fixed order (live state across the traversal) | RADV: landing 160–256 VGPRs, 4–6 waves, VALU 46 % busy and the lowest cache hit ratios: latency-bound; direct 128–192 | kept: handheld landing −34 % ship, −34 % dawn, −21 % guild, direct −28 / −28 / −9 %, frame −10.3 / −12.8 / −7.5 %; desk landing −9 / −25 / −30 %, direct −17 / −12 / −15 %, frame −2.2 / −4.8 / −6.8 %. VGPRs: landing 128–144, direct 112–128. No picture moved (`shots/c2`), repeat 10 pairs identical, 472 GPU tests |
| A layer's light worked out before its rays, one `PathEndLight` for the landing and the layer (`seenPaneOf`); `VisitRun` and the eye walk's `EYE_PANE` step gone | panes 256 VGPRs with spills | kept: handheld panes −17.3 % guild, −0.5 / −1.5 % outside (256 → 192 without the sea; 256 with it, spills 37–45 → 0–11); desk panes −4.9 / −4.0 / −8.0 %; no picture moved (`shots/c4`) |
| The bounce's escape worked out before its ray, so the miss keeps 3 numbers and not 10 | bounce 96 VGPRs, pre-scheduling 80; 88 would give 11 waves | not kept yet: RADV still allocates 96 (pre-scheduling 80 again), yet the handheld's bounce went −0.5 to −0.6 % at every place (one run each); the desk not measured |

### Decisions on perf-trace

- **The landings stay in the order the bounce lists them; no sort by where they hit.** Spiked on
  `spike/landing-sort` (local): a global counting sort by the instance met cost the desk's landing
  +40 % to +80 % (77 bins indoors, every landing an atomic on a few of them); a sort by world cell
  (96 units, Morton over a 16-cell block) in shared memory, 1024 landings a block, both runs, gave
  the handheld landing −6.7 % guild, −5.3 % ship, −4.5 % dawn (frame −2.1 / −1.0 / −0.8 %) and the desk
  landing −1 % indoors but +9 to +10 % outdoors, the pictures identical. Refused: it regresses the
  desk, and a path for software traversal alone (RADV's RDNA 2) would buy 1–2 % of the handheld's frame
  for a pass, a list and a vendor switch. Reordering secondary rays by origin is the established
  approach (Pharr and Hanrahan's coherence work, extended to GPUs; Garanzha and Loop's
  compress-sort-decompress; the overhead such a sort carries ([Generate Coherent Rays Directly](https://arxiv.org/pdf/2310.07182))).
- **Cutouts stay tested in the shader; no opacity micromaps (asked again 2026-10-31, refused again).**
  Spiked on `spike/opacity-micromap` (local): 4-state micromaps baked on the host from the finest
  alpha level, conservative over every texel a bilinear read inside a microtriangle can take
  (`uv * size - 0.5`), one microtriangle about 8 texels (level ≤ 6) or 2 texels (level ≤ 9), special
  indices for whole triangles, `DISABLE_OPACITY_MICROMAPS` on a placement not wearing the baked
  material. At the ship 901 micromaps over 4 987 triangles, 1.0 MiB (2.8 MiB at level 9), baked in
  0.27 s at load (0.58 s); 20 % clear, 33 % opaque, 47 % unknown microtriangles (36 % at level 9).
  Desk, the same build on and off, three interleaved rounds: bounce −2.0 to −3.3 %, air −2 to −4 %,
  landing −1 to −2.6 %, but trace +1.7 to +4.5 % and, at dawn, direct +10 % and the shadow filter
  +8 %; frame +0.3 % ship, +3.3 % dawn, +0.6 % guild, −0.2 % vivec, +0.7 % pond — no gain past the
  noise, and a loss at dawn. Pictures: a slight move where a coned ray reads a coarser level than the
  bake (sparse dots in far canopies and water glints, 0.03 % of the pixels past 2/255), which the
  OMM SDK's guide names ("do not use dynamic mip level"). The handheld gains nothing: RADV
  offers no `VK_EXT_opacity_micromap`; the desk's driver offers it and `VK_KHR_opacity_micromap`.
  Refused: the gain is not there on the desk and the handheld cannot have it. Sources: the Vulkan
  specification's ray traversal chapter (micromap states against the instance flags),
  [NVIDIA OMM SDK](https://github.com/NVIDIA-RTX/OMM) and its integration guide.

# review-fork — left for a bench session

Each needs a quiet-desktop A/B per candidate, built side by side in an `optimize-perf` session.

## Ideas

- **64-lane trace workgroups**: at 128 registers a Turing or Ampere SM holds 16 warps, which its 16-block cap
  matches, so the cap binds the kernels under 128 (bounce at 80–124) on those cards alone; needs such a card.
- **Occupancy redesigns**: shared tiles in their source encodings, a ray's request not carried through the loop, the
  relief marches sharing a fetch, visibility storing before fog and sprites, panes carrying less through traversal,
  bounce keeping its record packed, then the register reductions (Nsight for the peaks).
- **The à-trous edge stop states its departure, or matches SVGF**: an `./omw release noise --ab` suite decides.
- **NVIDIA's checkpoints on by default**: their cost on a bench.
- **The cell ring adopts by a budget**: a resumable adoption through the models, the ground and the holds, worth it
  only if a cell's arrival measures a worst frame.
- **A staging ring**, and the submit patience covering bounded work on it: DeviceScene recording across submits in
  its builds, for a host peak and a submit length no run has measured past what the machines hold.

