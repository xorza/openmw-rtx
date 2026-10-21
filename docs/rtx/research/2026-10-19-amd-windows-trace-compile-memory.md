# Visibility launch compile memory on AMD's Windows driver — 2026-10-19

Radeon 680M (RDNA 2, integrated), Adrenalin 26.8.1 ("AMD proprietary shader compiler"), Windows 11,
30 GB RAM, 16 logical processors. Release build, shaders pinned without fusion. Each run is
`openmw-rtxtool shot --views=seyda-neen-ship --variants=false` (one visibility launch) inside a job
object with a 14 GB commit cap; earlier runs used a polling watchdog. Every other pipeline came from
the driver's cache, so the peak above the cached run is the visibility launch's compile.

## The module

`visibilityhit.rchit.spv` after `spirv-opt -O`: 1.71 MB, one function, about 100,000 lines of
assembly, 8,283 blocks, 90 loops, 605 switches, 50 `OpRayQueryProceedKHR` traversal loops. The next
largest ray-tracing module, `visibility.rgen.spv`, is 0.27 MB.

Traversal loops by the source site they are inlined from: `passageToward` 44, `traverse` 5,
`surfaceWithin` 1; `gatherAlong` is not in this module.

## Peaks

| Hit module | Peak job memory | Visibility compile |
|---|---|---|
| Cached (no compile) | 2.5 GB | 4 ms |
| No traversal loop, two hit stages | 10.8 GB | 110 s |
| No traversal loop, one hit stage | 10.8 GB | 41 s |
| One traversal loop (`surfaceWithin`) | 11.8 GB | 104 s |
| Six traversal loops (`traverse`, `surfaceWithin`) | over 18.6 GB, stopped | — |
| 50 loops without candidate handling | over 18.6 GB, stopped | — |
| 50 loops, as shipped | over 52 GB and growing ~300 MB/s, stopped | — |
| As shipped, `spirv-opt` without inlining (323 functions, 3 loops) | 14 GB cap, `VK_ERROR_OUT_OF_HOST_MEMORY` | — |

The default build specializes 16 visibility launches and compiles them on 16 threads
(`VisibilityPass::compileEvery`, `runInParallel`); that run passed 19 GB within 22 s.

## A loop the compiler is told not to unroll stays one copy

`openmw-rtxtool info` (no frame drawn) on the one-loop variant, with `solidWithin` calling
`surfaceWithin` once, twice in a row, and twice inside a `[[dont_unroll]]` loop:

| `solidWithin` | Traversal loops in the module | Peak job memory | Visibility compile |
|---|---|---|---|
| One call | 1 | 11.07 GB | 139 s |
| Two calls | 2 | 13.70 GB | 116 s |
| Two calls in a `[[dont_unroll]]` loop | 1 | 11.07 GB | 140 s |

## The water shading is the base cost, and a specialization constant does not take it out

`info` on the module without traversal loops, one of `main`'s three answers removed from the source:

| Removed | Hit module | Peak job memory | Visibility compile |
|---|---|---|---|
| `answerPane` | 1.33 MB | 8.4 GB | 80 s |
| `answerSolid` | 1.10 MB | 6.8 GB | 55 s |
| `answerWater` | 0.53 MB | 1.5 GB | 11.5 s |

With every hit group's `SPEC_WATER` set false instead, which leaves the water shading in the module
but dead in every stage, the launch still peaked at 10.8 GB in a `shot` whose cached run peaks at
2.5 GB: the compiler pays for code a specialization constant turns off.

## The traversal in a callable stage

`queryScene`, run by the callable stage `scenequery.rcall` and called from both hit modules, left
no traversal loop in the hit stages (17 and 50 calls): the visibility launch compiled in 42 s at a
6.1 GB peak (`info`, every other pipeline cached).

What the launch then drew is in `2026-10-19-amd-windows-driver-faults.md`.

## The walk: one traversal site (2026-10-20)

The visibility launch rewritten as one loop over every ray a pixel casts
(`docs/rtx/architecture.md`, "The shaders"): one `OpRayQueryProceedKHR` in the optimized
module, 0.47 MB, and every function body once.

| Shape | Compile (debug) | Peak job memory | Stack or scratch per invocation |
|---|---|---|---|
| Ray generation stage alone | 99–119 s | 2.1 GB | 81,312 bytes (`vkGetRayTracingShaderGroupStackSizeKHR`) |
| The same module as a compute dispatch | 63 s | 1.2 GB | 67,408 bytes scratch, 256 VGPRs |

Every other trace launch reports a stack of 0 or 48 bytes.

What the walk drew as a ray tracing pipeline is in `2026-10-19-amd-windows-driver-faults.md`.
