# Open issues

Distant terrain carries no statics. Past the loaded cells the ground arrives on its own — no
buildings, no trees, no rocks — so a hillside a few cells out is bare where the same hillside inside
the grid is not.

Whether the game leaves creatures standing after their cell unloads is not known. The harness did
and
no longer does; `MWWorld::Scene` unloads a cell's references through its own path and may already be
correct.
