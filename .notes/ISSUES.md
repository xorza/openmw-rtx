# Open issues

The three views in `resources/rtx/views.cfg` that give a cell and no camera — `seyda-neen-shore`,
`sadrith-mora` and `dagon-fel` — render a bare water quad against fog. No terrain, and nothing the
cell holds, against descriptions that name a shoreline, a seabed and a village.

`RtxFrameCostTest.aSteadyFrameDoesNotTouchTheHeap` loses the device. Its frame loop fails with
`VK_ERROR_DEVICE_LOST` after about five seconds, on a scene of one wall at 64 by 64. It is
intermittent over hours — four runs in a row passed, and four in a row failed later the same session
— but reliable while it lasts. Every other test on the same device passes in the same run.

It presented as an indefinite hang until the waits were given deadlines: the loop passed `UINT64_MAX`
and discarded the return, so a lost device was indistinguishable from a wait that had not finished.

Distant terrain carries no statics. Past the loaded cells the ground arrives on its own — no
buildings, no trees, no rocks — so a hillside a few cells out is bare where the same hillside inside
the grid is not.

Creatures stay where they were after the camera leaves the cells that hold them. In `openmw-rtxtool`
they remain placed and posed once their cell is no longer loaded; whether the game does the same is
not known.
