# `NO_DUPLICATE_ANY_HIT` on every geometry — 2026-10-22

RTX 4090 Laptop, driver 615.78.08, Linux, release build at `74925d54de`, 1920x1080 output, upscale
`quality`. A against B, in the order A B B A, each leg the eight places
`seyda-neen-shore,seyda-neen-pond,balmora,vivec,seyda-neen-pier,ald-ruhn,seyda-neen-ship-dawn,balmora-mages-guild`
after a throwaway leg: A every geometry built with `VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR`
(`describeTriangles`), B none. B bounds what keeping the flag to blended meshes could save, since
it takes the flag off every geometry, blended or not. Records in `.notes/perf/duplicate-ab/`.

| leg | frame medians, summed | p99s, summed | structures |
|---|---|---|---|
| A, the flag | 48.91 ms | 55.10 ms | 159.7 MiB |
| B, none | 49.65 ms | 57.80 ms | 167.9 MiB |
| B, none | 49.89 ms | 57.00 ms | 167.9 MiB |
| A, the flag | 49.78 ms | 58.89 ms | 159.7 MiB |

The zones, the better of each side's two legs, summed: the trace 4.42 against 4.48 ms, the direct
light 4.09 against 4.12, the landing 5.02 against 5.06, the top level 3.37 against 3.41.

**Duplicates allowed traverse no faster on this driver and build 5% larger structures.** The DXR
specification's note that allowing duplicates can help traversal does not show here, so the flag
stays on every geometry, which also keeps a see-through surface counted once wherever it is placed.
