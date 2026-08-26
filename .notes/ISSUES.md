# Open issues

The three views in `resources/rtx/views.cfg` that give a cell and no camera — `seyda-neen-shore`,
`sadrith-mora` and `dagon-fel` — render a bare water quad against fog. No terrain, and nothing the
cell holds, against descriptions that name a shoreline, a seabed and a village.

`RtxFrameCostTest.aSteadyFrameDoesNotTouchTheHeap` hangs indefinitely with the GPU pinned at 100%,
after printing its two pipeline lines and before any frame is measured. It reproduces on a clean
tree, and it passed in the same working copy earlier the same session, so it is not a code change.

`--distant-terrain` loses ground the grid renders. At Ald-ruhn from a camera inside the staged cell
(`--pos=-12288,53248,2500 --look=-12000,90000,500`), the paged world draws the harness's sea plane
where the street should be and no hills on the horizon; the same camera without the flag draws both.
At Balmora the near ground survives but the distant hills do not — 98.47% of primary rays hit against
99.35% for the grid. It is not the composite path: it happens at `--distant-cells=0` too.
