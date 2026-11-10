# Proposal: navmesh tiles in the content store

Temporary. When it lands, its lasting decisions go into `docs/rtx/architecture.md` ("The content store"),
`.notes/UPSTREAM.md` gains its entry, and this file is deleted. The facts it stands on are in
`docs/rtx/research/2026-11-10-navmesh-cache.md`.

## The problem

The navigator keeps the tiles it generates in SQLite: one `navmesh.db` a profile, in the user data folder, keyed by a
blob compared byte for byte and by shape ids a second table hands out. The fork keeps everything else it derives from
the content in `ContentStore::Store`, one file an entry under a content key, where SQLite was refused by the user's
decision. Two stores for one kind of thing, a dependency for one of them, and a cache no two profiles share although
most of their tiles are the same.

## Established practice

- **Content-addressed disk caches**: ccache and Bazel's disk cache file each output under a hash of what made it, one
  file an entry in folders by the hash's first byte, and evict by age or not at all. `ContentStore::Store` is that shape
  (`docs/rtx/architecture.md`, "One file an entry, and every entry untrusted").
- **Navmesh tiles**: Recast's own demo saves every tile to one file (`all_tiles_navmesh.bin`), and Unity and Unreal bake
  navigation data into the level's assets at build time: a cache valid for one content, made offline. OpenMW's is the
  one that keys a tile by its input and fills at run time, which a content-addressed store keeps as it is.
- **What a key must cover**: the inputs and the code. Bazel keys an action by its inputs and its command; ccache by the
  source and the compiler. The navigator's key covers the inputs, and two hand-moved versions stand for the code.

## Decisions

An item marked **Done** has landed, and its decisions stand in `docs/rtx/architecture.md` ("The content store").
The measurement under Tests is open; the proposal goes once it is taken.

- **Done: one store in a component of its own, its kinds and residency the owner's.** In `components/contentstore`
  (`docs/rtx/architecture.md`, "One store, in a component of its own" and "The owner states its kinds and what is held
  in memory"). The navigator's store has no `Holding`: 95 to 98 % of tiles are 4 KiB or less, most of 142 MiB, which
  would flood the 64 MiB the frame's facts are held in, and a stamp only ranks that load.
  `DetourNavigator::NavMeshTileStore` owns the navigator's `ContentStore::Store` and its key.
- **Done: a folder of its own, shared by every profile.** `<cache>/navmesh`, beside `rtx-content`, which the game,
  `openmw-navmeshtool`, the harness and the integration tests open alike, as they share the player's `navmesh.db` and
  `rtx-content` now: an entry is right for whoever reads it. A content key makes a tile two profiles share one entry.
  The harness's `--content-cache` keeps choosing the ray tracer's store alone, as it does today. Refused: the ray
  tracer's folder, whose start sweep would list 87,000 more files; the user data folder, since losing a cache costs time
  and nothing else; a harness switch for the tiles, whose generation moves no pixel of a run.
- **Done: the key is the tile's input, hashed where it stands.** `ContentStore::Digest("navmesh tile", sTileVersion)`
  over the worldspace's text, the tile position, the recast settings, the agent's bounds, the water, the heightfields
  and the flat heightfields, and for each mesh source its area type, its shape's path and file hash, and its
  `ObjectTransform`. One function makes it, which the game and `openmw-navmeshtool` both call, and it allocates nothing
  but the worldspace's text. Refused: shape
  ids, which existed to shorten a blob a table compared and need that table; the serialized blob as the key, an
  allocation a job for bytes that are hashed anyway.
- **Done: one version for the tile, held by a test.** `recastMeshVersion` and `navMeshFormatVersion` become one
  `sTileVersion` in the key; the payload keeps its own magic and version, and a payload its codec refuses is discarded.
  A test generates tiles from fixed inputs and digests them beside `sTileVersion`, as `RtxPassVersionsTest` holds the
  passes: a change to `BulletNifLoader`, the recast mesh builder, Recast or Detour that moves a tile fails it until the
  version moves. Refused: the build's revision in the key, which empties the store at every build.
- **Done: a source with no file hash is not stored.** Its tile is generated and not written. A path
  alone would keep a replaced file's tile. Every model the game loads carries a hash today (`NIFFile`, `SceneManager`),
  so this is a guard and not a path the content takes.
- **Done: the payload is the tile as now.** `PreparedNavMeshData` serialized and LZ4-compressed (`Misc::compress`), a
  mean of 1.7 KiB. `mUserId`, which held the row's id, is 1 for a tile the store answered and 0 otherwise: the tile
  view's comparison and the rasterizer's debug view, which draws a cached tile brighter, are its only readers. Refused:
  an entry for a tile that yields nothing: none of 364 jobs in a sample did, and an empty mesh ends before generation.
- **Done: the read stays off the updater; the write is the store's.** The database worker becomes the tile store's
  reader, on its thread and ranked by the player's tile as now, and computes the key once a job, which the job keeps for
  its write. A generated tile goes to `ContentStore::Store::write`, whose own thread and bounded queue write it, and the
  worker's write queue goes. Only `add` and `remove` read the store, as now: a moved object regenerates. The profiler's
  `NavMesh DbJobs` and `NavMesh DbCache` rows become `NavMesh Store` rows with the stats they read.
  `openmw-navmeshtool`'s transaction and job queue go, and it reports tiles found and written.
- **Done: no cap and no removal**, as the content store decided. A tile another content made stays, as rows stay now
  (20,131 positions of the vanilla cache hold more than one input). `--remove-unused-tiles`, the vacuum, the launcher's
  check box and size limit and their strings go. Refused: removing unused tiles, which a store every profile shares
  cannot know.
- **Done: one setting.** `[Navigator] enable nav mesh disk cache` turns the store on, for reads and writes;
  `write to navmeshdb` and `max navmeshdb file size` go from the settings, `settings-default.cfg`, the launcher and the
  settings documentation. Refused: a read-only mode, a second switch for one store.
- **Done: the old database is left alone.** The game no longer opens `navmesh.db` and does not delete it; the release
  notes say it can go. Refused: deleting a file in the player's user data folder unasked.
- **Done: the animated pose stays out of the key**, as upstream has it; the issue log holds it.
- **Done: sQLite leaves the build.** `components/sqlite3`, its tests, the fetched amalgamation,
  `OPENMW_USE_SYSTEM_SQLITE3`, the `find_package`, the presets' entry and the CI's packages go, with `navmeshdb*`,
  `navmeshdbutils*`, `DbRefGeometryObject` and the shape and tile id types.
- **Done: the cache path reaches the navigator.** `makeNavigator` takes the cache path where it took the user data path,
  which `World` is handed by the engine as the renderer is (`Files::ConfigurationManager::getCachePath`).

## Upstream changes

The entry in `.notes/UPSTREAM.md`: "The navigator's tiles in the content store (`DetourNavigator::NavMeshTileStore`),
and SQLite out of the build: the fork keeps what it derives from the content in one store, every profile shares the
tiles they share, and the store's key covers the code by a version a test holds. The cost is a conflict with every
upstream change to the navigator's database worker, its input serialization or `openmw-navmeshtool`."

## Tests

- The key: each input moves it (each setting, the agent's bounds, a height, a water level, a source's hash, path, area
  type and transform, the worldspace and the tile position), and the same input keys the same on every run.
- The store: an owner's kind outside its range is refused; a tile store holds nothing in memory and stamps nothing;
  an entry the ray tracer wrote before the move reads after it.
- The worker: a miss writes, a second start reads it (`mDbGetTileHits` renamed with the stats), a payload its codec
  refuses is discarded and regenerated, a source with no hash is not written.
- `openmw-navmeshtool` fills the store the game then reads with no generation.
- The version test above.
- **Open:** measured on the Windows laptop and here, SQLite against files: the time from a new game to its last tile
  job, with an empty cache and a full one. A file a read is an open on Windows, which an antivirus scan can slow, where
  SQLite reads inside one open file.
