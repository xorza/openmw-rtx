# Open issues

The three views in `resources/rtx/views.cfg` that give a cell and no camera — `seyda-neen-shore`,
`sadrith-mora` and `dagon-fel` — render a bare water quad against fog. No terrain, and nothing the
cell holds, against descriptions that name a shoreline, a seabed and a village.

`RtxFrameCostTest.aSteadyFrameDoesNotTouchTheHeap` hangs indefinitely with the GPU pinned at 100%,
after printing its two pipeline lines and before any frame is measured. It reproduces on a clean
tree, and it passed in the same working copy earlier the same session, so it is not a code change.
