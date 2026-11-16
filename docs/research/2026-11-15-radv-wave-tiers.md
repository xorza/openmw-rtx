# What the trace's stages hold on RADV, and the waves it costs them — 2026-11-15

An AMD handheld (RDNA 2, 8 CUs) under RADV, its clock held at 1600 MHz, `bench --window=false --size=1280x800
--frames=300`, one leg a side; a high-end NVIDIA laptop GPU, `bench --window=false --seconds=10` at 1920x1080, three
legs a side. Vanilla, the Fullrest Repack and the PBR stack, the release harness at `9da862d673` and at the branch's
tip.

## How it was read

- **RADV compiles for the handheld's card on any machine**: Mesa's `amdgpu` drm-shim (`AMDGPU_GPU_ID` naming its chip,
  `LD_PRELOAD=libamdgpu_noop_drm_shim.so`, RADV's ICD alone in `VK_DRIVER_FILES`) under SDL's offscreen driver runs
  `bench --frames=1 --pipeline-code=<dir>` in six seconds: every pipeline's statistics and code, none executed. A
  newer Mesa there held the same figures in 314 of 334 pipelines.
- **The modules with their source compile to the shipped ones' code** (`--shader-source`): the same statistics in every
  pipeline on both drivers, so one binary benches any shader set by the folder it reads.
- **Liveness off ACO's own IR** (a backward dataflow over the "After Spilling" listing) matches `Pre-Sched VGPRs` on
  every kernel, and says where the peak stands and what each loop carries untouched.
- **The handheld repeats to 0.05 ms a zone.**

## What RADV makes of a register

- **A wave tier is sixteen registers**: 32-lane waves share 1024 a SIMD, so 80, 96, 112, 128, 144, 160 registers run
  12, 10, 9, 8, 7, 6 waves (`aco_live_var_analysis.cpp`, `max_suitable_waves`).
- **The count is the demand before scheduling, rounded up a tier or so**: the scheduler's budget is the demand's tier
  plus 24 (`aco_scheduler.cpp`).
- **A buffer row is loaded whole where the GLSL reads it**: RADV sinks arithmetic toward its use but not a
  `load_global`, so a field read only past a loop, of a row read ahead of it, stands in a register through the loop;
  read again past the loop, it is loaded there.
- **A ray query's last hit rides the loop its traversal stands in**: `rayQueryInitializeEXT` resets the closest hit's
  distance and type and leaves its other fields, eleven registers through everything between two traversals.
- **A scratch figure is a subgroup's** ("private memory in bytes per subgroup"): a launch's 10240 is 320 bytes a lane of
  its ray generation stack, not a spill.

## Where the stages peaked, and what took it off

At the start a stage peaked inside its traversal under a sky (`direct`, `landing`: the sky's two legs' asks held whole
through the loop), in the ground stack's loop of `resolve` elsewhere (57–66 values carried untouched, fourteen of them
the material's row loaded ahead of the stack), in the sprites' walk for the eye's stage, and in its candidate's test
for the bounce. Each change below on top of the one before:

| change | what moved on RADV |
|---|---|
| a gather's rays asked at their turns (`GatherRays`) | `direct` under a sky 128 → 112, `panes sun sea` 256 → 192 |
| the sprites' walk past the eye's stores | `visibility sun sea` 128 → 112 |
| the material's and the mesh's rows read again past the stack | `direct`, `landing` a tier each; `bounce maps` 128 → 112; `visibility maps` 144 → 128 |
| the bounce's hand-over and record stored ahead of its ray, read back on a miss | `bounce` 96 → 80 without maps |
| the eye's peel weighs a layer by its opacity alone and resolves once, past its loop | `visibility` 112 → 96 without maps, 144 → 128 with a relief |

Tried and not kept: the placement's row read again past the stack (a register more in the eye's stage); the material's
fields grouped by where a hit reads them (nothing moved); the landing resolved once off whichever images hold its hit
(four registers more, its tuples with maps back to 128); the eye's hit stored ahead of its resolve (two registers off,
its zone 2% longer under a sky); the eye's ray's start made again past the resolve (16 registers more through the
stack's loop).

Laid over the commit that reads a mod's maps (`2026-11-15-mod-maps-registers.md`), the tuples a scene ran before kept
their code, and 328 of 574 pipelines held a tier or two fewer, none more.

## What it came to

Frame medians, start against tip:

| content | place | handheld | NVIDIA |
|---|---|---|---|
| vanilla | ship | 86.89 → 81.97 (−5.7%) | 5.94 → 5.77 (−2.9%) |
| vanilla | ship-dawn | 101.13 → 94.84 (−6.2%) | 6.43 → 6.13 (−4.7%) |
| vanilla | guild | 54.98 → 53.04 (−3.5%) | 5.18 → 5.18 |
| Repack | ship | 118.47 → 113.56 (−4.2%) | 7.30 → 7.31 |
| Repack | ship-dawn | 133.83 → 130.42 (−2.5%) | 7.70 → 7.70 |
| Repack | guild | 80.53 → 76.04 (−5.6%) | 6.40 → 6.30 (−1.5%) |
| PBR | ship | 99.45 → 97.38 (−2.1%) | 6.79 → 6.79 |
| PBR | ship-dawn | 117.80 → 117.11 (−0.6%) | 7.33 → 7.27 (−0.7%) |
| PBR | guild | 66.08 → 64.61 (−2.2%) | 6.26 → 6.29 |

- **On RADV a zone moved where it gained a wave**: `direct` 13–14% for one under a sky, the eye's stage 7% a wave,
  `landing` 6–8%, the bounce 10–11% for two. A stage that carried less and gained no wave ran as long or a little
  longer, paying for its stores or its extra read (0.8–1.3%).
- **NVIDIA's compiler chose again in every stage**, trading registers for shared memory: `direct` with maps went from
  128 registers to 96 with shared memory and ran 5–11% longer on the Repack until a later change cut its bytes; the
  PBR stack's room `direct` stayed 4.6% longer while its bounce ran 8–10% shorter. `landing` −28% at the ship and dawn.
- **No picture moved** on vanilla or the PBR stack; the Repack's two that differed do not repeat between runs of the
  start itself.
