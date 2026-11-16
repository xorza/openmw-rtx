# Scene instances made by the cell preloader — 2026-11-09

A high-end NVIDIA laptop GPU with the Fullrest Repack, `bench --suite=streaming`, release, two runs a shape. A cell
change clones each object's scene graph on the main thread (`SceneManager::getInstance`); the preloader readies
templates and collision shapes, no scene graph instance.

## Upstream's history

Upstream preloaded instances from 2016 (`e28dc3e72f`) and dropped them in 2021 (`147ed39900`) in the fix for a crash in
`Skeleton::clone` on the preloader's thread ([#4602](https://gitlab.com/OpenMW/openmw/-/issues/4602)), when parts began
to be attached out of their templates.

## Measured

| shape | frame worst | update p99 | peak memory |
|---|---|---|---|
| as it stands | 37.0, 38.3 ms | 15.15, 14.85 ms | 5,234 MiB |
| an instance for every model listed | 34.9, 34.0 | 13.81, 13.49 | 5,888 MiB |
| an instance of each reference's own model, NPCs left out | 38.1, 35.9 | 13.48, 13.76 | 5,228 MiB |

Not kept: every model listed buys 3 ms of the worst frame for 650 MiB, and each reference's own model buys the p99 but
not the worst frame. The two worst cell changes spent 44% and 59% of their time building NPCs, whose skeletons and parts
no preloaded instance reaches.
