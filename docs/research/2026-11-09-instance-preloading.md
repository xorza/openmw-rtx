# Scene instances made by the cell preloader

Measured 2026-11-09, on a high-end NVIDIA laptop GPU with the Fullrest Repack: `bench --suite=streaming`, the
release build, two runs of each shape back to back, with the harness's peak resident memory sampled every
0.2 s. The cell change clones each object's scene graph on the main thread (`SceneManager::getInstance`);
the preloader readies templates and collision-shape instances, and no scene graph instance.

## Upstream's history

- **2016, `e28dc3e72f` "Preload instances in SceneManager"**: the preloader cloned a scene instance for every
  model a cell listed (`SceneManager::cacheInstance`), and `getInstance` took one from a `MultiObjectCache`
  before cloning.
- **2020, `6fa12a6eb8` "preload tweak"**: only models with a `.kf` beside them, and each model once.
- **2021, `147ed39900` (#3095)**: the scene instances dropped from the preloader in the fix for bug #4602, a
  crash inside `Skeleton::clone` under `cacheInstance` on the preloader's thread
  ([issue](https://gitlab.com/OpenMW/openmw/-/issues/4602)). The same change made `SceneUtil::attach` clone
  a part out of its template where it had moved nodes out of a shared instance. `5fde6867a2` (#3129) then
  removed the cache as unused.

## Shapes measured

| shape | frame p99 ms | frame worst ms | update p99 ms | update worst ms | peak memory |
|---|---|---|---|---|---|
| as it stands | 18.11, 18.09 | 37.0, 38.3 | 15.15, 14.85 | 32.4, 33.7 | 5234, 5224 MiB |
| an instance for every model listed | 17.52, 17.04 | 34.9, 34.0 | 13.81, 13.49 | 30.5, 30.6 | 5888, 5860 MiB |
| an instance of each reference's own model, NPCs left out | 17.17, 16.98 | 38.1, 35.9 | 13.48, 13.76 | 32.2, 31.0 | 5228, 5208 MiB |

Every model listed includes an NPC's body parts, which are attached out of their templates and never taken
from the cache, and every creature a levelled list may spawn. With each reference's own model alone,
`getInstance` by path was asked 11,861 times over a run and took 8,430 from the cache; 123 instances expired
unused.

## Where the worst crossing goes

The two worst cell changes in a profile spent 44% and 59% of their time building NPCs, whose skeleton is
instanced through a cache of its own (`getModelInstance`'s base-only path) and whose parts are attached out
of their templates: no preloaded instance reaches either.
