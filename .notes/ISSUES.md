# Open issues

The three views in `resources/rtx/views.cfg` that give a cell and no camera — `seyda-neen-shore`,
`sadrith-mora` and `dagon-fel` — render a bare water quad against fog. No terrain, and nothing the
cell holds, against descriptions that name a shoreline, a seabed and a village.

`RtxFrameCostTest.aSteadyFrameDoesNotTouchTheHeap` hangs indefinitely with the GPU pinned at 100%,
after printing its two pipeline lines and before any frame is measured. It reproduces on a clean
tree, and it passed in the same working copy earlier the same session, so it is not a code change.

Distant terrain carries no statics. Past the loaded cells the ground arrives on its own — no
buildings, no trees, no rocks — so a hillside a few cells out is bare where the same hillside inside
the grid is not.

Creatures stay where they were after the camera leaves the cells that hold them. In `openmw-rtxtool`
they remain placed and posed once their cell is no longer loaded; whether the game does the same is
not known.
