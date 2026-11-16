# Ray query compile memory on AMD's Windows driver — 2026-10-19

A low-end AMD integrated GPU (RDNA 2), AMD's Windows driver 26.8.1, 30 GB RAM. Release, `shot --views=seyda-neen-ship
--variants=false` inside a job object with a commit cap; every other pipeline from the driver's cache, so the peak
above the cached run is the visibility launch's compile.

## Each traversal site is a whole traversal

The visibility hit module of the time inlined 50 `OpRayQueryProceedKHR` loops (44 of them from one helper).

| module | peak memory | compile |
|---|---|---|
| cached | 2.5 GB | 4 ms |
| no traversal loop | 10.8 GB | 41–110 s |
| one loop | 11.8 GB | 104 s |
| six loops | over 18.6 GB, stopped | — |
| 50 loops, as shipped | over 52 GB and growing, stopped | — |
| as shipped, not inlined | `VK_ERROR_OUT_OF_HOST_MEMORY` at a 14 GB cap | — |

- **A loop the compiler is told not to unroll stays one copy**: one call 11.07 GB, two calls 13.70 GB, two calls in a
  `[[dont_unroll]]` loop 11.07 GB.
- **A specialization constant does not take code out**: the water shading left in the module but dead in every stage
  still peaked at 10.8 GB, where removing it from the source took the module to 1.5 GB.
- **The traversal in a callable stage** left no loop in the hit stages and compiled in 42 s at 6.1 GB.

## The walk: one traversal site

The visibility launch rewritten as one loop over every ray a pixel casts, one `OpRayQueryProceedKHR` in the module:

| shape | compile (debug) | peak memory | stack or scratch an invocation |
|---|---|---|---|
| a ray generation stage | 99–119 s | 2.1 GB | 81,312 bytes of stack |
| the same module as a compute dispatch | 63 s | 1.2 GB | 67,408 bytes of scratch, 256 VGPRs |

What it drew is in `2026-10-19-amd-windows-driver-faults.md`, what it cost in
`2026-10-21-amd-integrated-stage-times.md`.
