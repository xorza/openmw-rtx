# `NO_DUPLICATE_ANY_HIT` on every geometry — 2026-10-22

A high-end NVIDIA laptop GPU, driver 615.78.08, release at `74925d54de`, 1920x1080 at the quality upscale, eight places
in the order A B B A after a throwaway leg: A every geometry built with
`VK_GEOMETRY_NO_DUPLICATE_ANY_HIT_INVOCATION_BIT_KHR`, B none, which bounds what keeping the flag to blended meshes
alone could save.

| leg | frame medians, summed | p99s, summed | structures |
|---|---|---|---|
| A, the flag | 48.91, 49.78 ms | 55.10, 58.89 ms | 159.7 MiB |
| B, none | 49.65, 49.89 ms | 57.80, 57.00 ms | 167.9 MiB |

**Duplicates allowed traverse no faster on this driver and build 5% larger structures.** The DXR specification's note
that allowing duplicates can help traversal does not show here, so the flag stays on every geometry, which also keeps a
see-through surface counted once wherever it is placed.
