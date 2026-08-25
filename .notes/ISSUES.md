# Open issues

`.notes/rtx/plan.md` §2 cites `.notes/rtx/mirror.md` for the mirror's measurement and shape. No such
file exists.

`apps/rtxtool/main.cpp` calls `world.pageTerrain(variables["distant-terrain"].as<bool>())` twice in a
row in five command branches.

`.notes/rtx/plan.md` §4 and `.notes/rtx/backends.md` §2 both cite `.notes/rtx/merge.md` for why
`components/rtxbridge` folded into `components/rtx`. No such file exists.

The three views in `resources/rtx/views.cfg` that give a cell and no camera — `seyda-neen-shore`,
`sadrith-mora` and `dagon-fel` — render a bare water quad against fog. No terrain, and nothing the
cell holds, against descriptions that name a shoreline, a seabed and a village.
