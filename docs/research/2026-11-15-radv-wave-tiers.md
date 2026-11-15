# What the trace's stages hold on RADV, and the waves it costs them: 2026-11-15

An AMD handheld's integrated GPU (RDNA 2, 8 CUs) under RADV, Mesa 26.2.0-devel (`git-5c65d4fc31`), its clock held at
1600 MHz; and a high-end NVIDIA laptop GPU, driver 620.70.06. Vanilla Morrowind, the Fullrest Repack and the PBR
stack; the release harness at `9da862d673` and at the branch's tip. Handheld: `bench --window=false --size=1280x800
--frames=300` (853x533 traced), one leg a side. NVIDIA: `bench --window=false --seconds=10` at 1920x1080, three
legs a side in turn. Zones are medians in ms.

## How it was read

- **RADV compiles for the handheld's card on any machine**: Mesa's `amdgpu` drm-shim (`AMDGPU_GPU_ID` naming its
  chip, `LD_PRELOAD=libamdgpu_noop_drm_shim.so`, RADV's ICD alone in `VK_DRIVER_FILES`) with SDL's offscreen video
  driver runs `bench --frames=1 --pipeline-code=<dir>` in six seconds: every pipeline's statistics and its code,
  none of it executed. Mesa 26.2.7 there against the handheld's 26.2.0: of 334 pipelines, 314 hold the same
  registers, scratch and shared memory; `direct` held a step more in 20 tuples at the start and holds the same at
  the tip.
- **The modules with their source compile to the shipped ones' code** (`--shader-source`): the same statistics in
  all 334 pipelines on both drivers, and zones within the spread on both cards (the eye's stage 1.3 to 2.9% longer
  outdoors on NVIDIA). So one binary benches any shader set by the folder it reads.
- **Liveness off ACO's own IR**: a backward dataflow over the "After Spilling" listing, vector temporaries along
  the logical flow, gives each kernel's most registers live, which matches `Pre-Sched VGPRs` on every kernel
  read, where it stands, and what each loop carries untouched with the instruction that defined each.
- **The handheld repeats to 0.05 ms a zone**: shipped against source modules, two legs a side, every zone within
  0.04 ms and the frame within 0.13.

## What RADV makes of a register

- **A wave tier is sixteen registers**: 32-lane waves share 1024 a SIMD, so 80, 96, 112, 128, 144, 160 registers
  run 12, 10, 9, 8, 7, 6 waves (Mesa's `aco_live_var_analysis.cpp`, `max_suitable_waves`).
- **The count is the demand before scheduling, and a little**: of the stage kernels at the tip, a demand of 84 to
  94 came to 96, of 99 to 109 to 112, of 111 to 128 to 128; the same demand of 106 to 110 came to 112 in most
  tuples and to 128 in the landing's with both moons. The scheduler's budget is the tier of the demand plus 24
  (`aco_scheduler.cpp`, `schedule_program`).
- **A buffer row is loaded where the GLSL reads it, whole**: a structure read off a buffer reference becomes a
  load a field at that statement, RADV sinks arithmetic toward its use and not a `load_global`
  (`nir_opt_sink` under `nir_move_load_ssbo`), and a second read of the same field is the same load. So a field
  read only past a loop, of a row read ahead of it, stands in a register through the loop; read again past the
  loop, it is loaded there.
- **A ray query's last hit rides the loop its traversal stands in**: `rayQueryInitializeEXT` sets the closest
  hit's distance and type and leaves its other fields (`radv_nir_lower_ray_queries.c`, `lower_rq_initialize`), so
  across a loop that traces a turn each they are values of the turn before, eleven registers through everything
  between two traversals.
- **A scratch figure is a subgroup's**, "private memory in bytes per subgroup" (`src/util/shader_stats.xml`): a
  launch's 10240 is the 320 bytes a lane of its ray generation stack, which the summary warned of as a spill at
  every start.

## Where the stages stood (`9da862d673`), registers (demand before scheduling)

| tuple | `visibility` | `direct` | `bounce` | `landing` | `panes` | `water` |
|---|---|---|---|---|---|---|
| none | 112 (107) | 96 (94) | 96 (84) | 112 (97) | 192 (180) | — |
| `sun sea` | 128 (118) | 128 (111) | 96 (85) | 112 (105) | 256 (194) | 144 (134) |
| `sun moons sea` | 128 (118) | 128 (112) | 96 (85) | 112 (105) | 256 (198) | 144 (134) |
| `maps` | 144 (135) | 128 (119) | 128 (113) | 128 (126) | 256 (206) | — |
| `sun sea maps` | 144 (137) | 128 (119) | 128 (114) | 128 (126) | 256 (210) | 160 (153) |
| `sun sea maps relief` | 144 (140) | 128 (128) | 112 (109) | 128 (121) | 256 (207) | 160 (150) |

- **Under a sky, `direct` and `landing` peaked inside their traversals**: 111 and 105 live in the candidate's
  test, 61 and 55 of them carried untouched through the traversal's loop. The sky's two legs were asks held whole
  from before the first ray, sixteen numbers.
- **Everywhere else a stage peaked in the ground stack's loop of `resolve`**: 94 to 97 without maps, 119 to 128
  with them, 57 to 66 carried untouched. Fourteen were the material's tints and its texture transform, loaded with
  the row ahead of the stack and read past it.
- **The eye's stage peaked in the sprites' walk**, 118 with 92 live into it: everything the stage stores after it.
  Under that, its resolve inside the peel's loop held the peel's state and the traversal's stale hit, 28.
- **The bounce peaked in its candidate's test**, 85: the traversal's own 32, the candidate's 21, and 28 of the
  stage's, of which the weight, the modulation and the record's four words were handed on untouched.

## What each change took off

Each on top of the one before; registers on the handheld's Mesa.

| change | what moved |
|---|---|
| a gather's rays asked at their turns, of the source's id, the leaving point and the draws read again (`GatherRays`) | `direct` under a sky 128 → 112 (demand 111 → 95), `landing` demand 105 → 97, `panes sun sea` 256 → 192 |
| the sprites' walk past the eye's stores | `visibility sun sea` 128 → 112 (118 → 109) |
| the material's and the mesh's rows read again past the stack | `direct` 112 → 96 without maps and 128 → 112 with them; `landing` 112 → 96 and 128 → 112; `bounce maps` 128 → 112; `visibility maps` 144 → 128; `water … maps relief` 160 → 144; `panes … relief` 256 → 192 without the sea |
| the lamp list and the bounce's hand-over read where they are used | `direct maps` demand 111 → 108, `landing maps` 114 → 108 |
| the bounce's hand-over and its record stored ahead of its ray, read back on a miss | `bounce` 96 → 80 without maps (85 → 74); with maps demand 110 → 106 |
| the direct channel stored ahead of the rays, the sky bit's pass-through drawn past them | `direct` demand 92 → 89 |
| the eye's peel weighs a layer by its opacity alone and resolves what it ends on once, past its loop | `visibility` 112 → 96 without maps (100 → 87), 144 → 128 with a relief (131 → 116), demand 128 → 113 with maps |

Tried and not kept, with what each came to on RADV:

- **The placement's row read again past the stack** for its opacity: a register more in the eye's stage, one less
  in `panes`.
- **The material's fields grouped by where a hit reads them**: no kernel moved, since the row is loaded whole where
  it is first read.
- **The landing resolved once, off whichever images hold its hit**: its code 35% shorter (6876 → 4795
  instructions in `landing sun sea`) and four registers more, which took its tuples with maps from 112 back to 128.
- **The eye's hit stored ahead of its resolve and read back past it**: two registers off the eye's stage with
  maps, 112 in a room's tuples, and its zone 2% longer under a sky for the three stores and loads
  (9.02 against 8.85 ms at the ship).
- **The eye's ray's start made again past the resolve** in place of carried: the resolve moved into the branch
  that met a surface held 16 registers more through the stack's loop.

## Beside a mod's maps

Laid over the commit that reads a mod's detail, decal, bump and gloss maps under a constant of their own
(`c83efc723c`, `2026-11-15-mod-maps-registers.md`), whose reads past the ground stack take the rows read again
there. RADV under the shim, 574 pipelines:

- **The tuples a scene ran before keep the code they had here**: 326 of 327 the same code hash, the sprites'
  composite the one, at the registers it held.
- **Against that commit alone, 328 pipelines hold a tier or two fewer registers and none more**, the tuples that
  read a mod's maps as the ones that do not: 100 from 128 to 112, 98 from 144 to 128, 48 from 112 to 96, 30 from
  256 to 192, 26 from 128 to 96, 14 from 144 to 112, 8 from 160 to 144, 4 from 96 to 80.
- **NVIDIA's old tuples hold what they held here**, registers, shared and local memory; twelve of the eye's
  tuples with the maps and a mod's maps, under the moons or the sea with a relief, hold 120 to 124 registers and
  no shared memory where the rest hold 96.
- **No picture moved against that commit**: vanilla 74 of 74 the same, the PBR stack and the Repack every view
  judged, the Repack's two walks' sprites and emitters not repeating as they did not before.

Not timed on either card.

## What it came to

Handheld, the branch's start against its tip, one leg a side:

| content | place | frame | `trace` | `direct` | `bounce` | `landing` |
|---|---|---|---|---|---|---|
| vanilla | ship | 86.89 → 81.97 (−5.7%) | 9.98 → 8.85 (−11.3%) | 11.54 → 10.06 (−12.8%) | 15.87 → 14.22 (−10.4%) | 11.81 → 11.08 (−6.2%) |
| vanilla | ship-dawn | 101.13 → 94.84 (−6.2%) | 10.10 → 8.97 (−11.2%) | 22.23 → 19.11 (−14.0%) | 15.00 → 13.61 (−9.2%) | 13.62 → 12.80 (−6.0%) |
| vanilla | guild | 54.98 → 53.04 (−3.5%) | 4.77 → 4.60 (−3.4%) | 8.12 → 8.12 | 9.10 → 8.05 (−11.5%) | 10.78 → 10.10 (−6.3%) |
| Repack | ship | 118.47 → 113.56 (−4.2%) | 16.00 → 14.85 (−7.2%) | 12.46 → 11.80 (−5.3%) | 23.35 → 21.47 (−8.1%) | 17.21 → 15.90 (−7.6%) |
| Repack | ship-dawn | 133.83 → 130.42 (−2.5%) | 13.57 → 12.58 (−7.3%) | 24.96 → 23.21 (−7.0%) | 21.54 → 19.81 (−8.1%) | 21.39 → 21.55 (+0.8%) |
| Repack | guild | 80.53 → 76.04 (−5.6%) | 8.37 → 7.84 (−6.2%) | 13.01 → 11.88 (−8.7%) | 16.95 → 15.46 (−8.8%) | 16.59 → 15.37 (−7.3%) |
| PBR | ship | 99.45 → 97.38 (−2.1%) | 12.15 → 11.39 (−6.3%) | 13.12 → 13.14 | 18.37 → 18.52 (+0.8%) | 16.61 → 15.17 (−8.7%) |
| PBR | ship-dawn | 117.80 → 117.11 (−0.6%) | 12.24 → 11.47 (−6.3%) | 24.48 → 24.58 (+0.4%) | 18.36 → 18.50 (+0.8%) | 19.69 → 19.61 (−0.4%) |
| PBR | guild | 66.08 → 64.61 (−2.2%) | 6.39 → 6.17 (−3.6%) | 10.91 → 10.93 | 11.39 → 11.43 (+0.4%) | 13.26 → 12.23 (−7.8%) |

The PBR stack's `water` 1.91 → 1.74 (−9.1%) and its guild's `panes` 2.15 → 1.82 (−15.4%). Where a zone moved it had
gained a wave: `direct` one under a sky took 13 to 14% off, the eye's stage 7% a wave, `landing` 6 to 8%, the
bounce 10 to 11% for its two. A stage that carried less and gained no wave ran as long or a little longer: the PBR
stack's bounce, 0.8% for the stores ahead of its ray, and the Repack's eye stage, 1.3% for the peel's own read of
the opacity (14.66 → 14.85 ms at the ship).

NVIDIA, the start against the tip, the median of three legs:

| content | place | frame | `trace` | `direct` | `bounce` | `landing` |
|---|---|---|---|---|---|---|
| vanilla | ship | 5.94 → 5.77 (−2.9%) | 0.527 → 0.531 | 0.518 → 0.477 (−7.9%) | 0.385 → 0.377 (−2.2%) | 0.558 → 0.402 (−28.1%) |
| vanilla | ship-dawn | 6.43 → 6.13 (−4.7%) | 0.529 → 0.534 | 0.849 → 0.723 (−14.8%) | 0.388 → 0.379 (−2.4%) | 0.626 → 0.450 (−28.1%) |
| vanilla | guild | 5.18 → 5.18 | 0.361 → 0.360 | 0.507 → 0.510 | 0.366 → 0.363 | 0.517 → 0.511 (−1.3%) |
| Repack | ship | 7.30 → 7.31 | 0.645 → 0.634 (−1.8%) | 0.581 → 0.549 (−5.4%) | 0.611 → 0.618 (+1.2%) | 0.652 → 0.664 (+1.7%) |
| Repack | ship-dawn | 7.70 → 7.70 | 0.638 → 0.629 (−1.3%) | 0.875 → 0.830 (−5.1%) | 0.609 → 0.619 (+1.5%) | 0.725 → 0.734 (+1.2%) |
| Repack | guild | 6.40 → 6.30 (−1.5%) | 0.492 → 0.459 (−6.9%) | 0.691 → 0.617 (−10.8%) | 0.696 → 0.704 (+1.1%) | 0.951 → 0.956 |
| PBR | ship | 6.79 → 6.79 | 0.610 → 0.633 (+3.9%) | 0.701 → 0.708 (+1.0%) | 0.649 → 0.584 (−9.9%) | 0.718 → 0.724 |
| PBR | ship-dawn | 7.33 → 7.27 (−0.7%) | 0.606 → 0.615 (+1.4%) | 1.067 → 1.038 (−2.7%) | 0.649 → 0.586 (−9.8%) | 0.846 → 0.855 (+1.1%) |
| PBR | guild | 6.26 → 6.29 | 0.482 → 0.485 | 0.739 → 0.772 (+4.6%) | 0.556 → 0.513 (−7.8%) | 0.782 → 0.800 (+2.3%) |

- **NVIDIA's compiler chose again in every stage**: no kernel at 168 registers, the eye's stage at 96 in every
  tuple (128 with the sea and the maps before), `landing` 96 with 384 bytes of shared memory where it held 3200,
  `bounce` 80 to 110 where it held 80 to 122.
- **Fewer live values sent `direct` with maps from 128 registers to 96 and shared memory**, as
  `2026-11-14-nvidia-register-spills.md` found of other changes, and what that cost followed the bytes: 96 with
  3200 bytes ran the Repack's `direct` 5 to 11% longer than the start's 128 with 1664, and 96 with 2688, once the
  direct channel was stored ahead of the rays, 5% shorter. The PBR stack's room keeps the cost: `direct maps
  relief` 128 with 896 bytes → 96 with 3968, 4.6% longer.
- **`panes` went from 168 registers to 128 with a lane's local memory** in the tuples that held 168 (48 to 96
  bytes): 0.072 → 0.077 ms at the ship and 0.102 → 0.109 at the Repack's dawn, its other tuples 2 to 4% shorter.

Pictures: `shot --views=all --map --upscale=off` against the start's on NVIDIA, no picture of vanilla's 74 or the
PBR stack's moved; of the Repack's, the two that do not repeat between two runs of the start itself.

## Not done

- **The layers' frame made again past the stack in every tuple with maps**, as it is where the scene has a relief:
  six registers, which would take the Repack's eye stage (demand 113) and its landing at dawn (108, compiled to
  128) down a step on RADV. Refused on 2026-11-13 for what it cost AMD's Windows driver, which no host of this
  session runs.
- **A resolve's own registers with a relief**: the PBR stack's `direct` stands at 128 on a demand of 118 and its
  bounce at 112 on 100, each a step from the next wave.
- **The water's stage spills 65 to 70 scalar registers into vector ones** in every tuple, and the sprites'
  composite 4 vector registers to its stack.
