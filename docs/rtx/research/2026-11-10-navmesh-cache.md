# What the navmesh disk cache keys and holds

Read 2026-11-10 from the code at `80a59422ac` and, read-only, from the `navmesh.db` of the vanilla profile and of the
Fullrest Repack's profile, each as the game and `openmw-navmeshtool` left it.

## The key

A tile is a row of `tiles`, found by its worldspace, its tile position and an `input` blob compared byte for byte, both
LZ4-compressed (`NavMeshDb::findTile`, `getTileData`). The blob is `serialize(RecastSettings, AgentBounds, RecastMesh,
DbRefGeometryObject...)` (`serialization.cpp`):

- `recastMeshMagic` and `recastMeshVersion` (2);
- the 15 recast settings and the agent's bounds;
- each cell's water: its position, size and level;
- each heightfield by value, every height, and each flat heightfield;
- for each mesh source, a shape id and its `ObjectTransform`: the position, the rotation and the scale.

A shape id is a row of `shapes`, unique by the shape's path, its type (collision or avoid) and its file hash. The file
hash is `Files::getHash`, MurmurHash3_x64_128 over the whole file: for a NIF in `NIFFile`, for every other model in
`SceneManager`, which stores it as the node's `fileHash` user value. Both read it at every load. A read also checks the
row's `version` against `navMeshFormatVersion` (2). The payload is `PreparedNavMeshData`, serialized
(`preparedNavMeshDataVersion` 1) and LZ4-compressed; its `mUserId` is the row's id.

What a content change moves in the key:

| change | moves |
|---|---|
| a mesh replaced, in a folder or a BSA | the shape's file hash |
| an object added, moved or removed | the list of sources |
| terrain heights | the heightfields |
| a cell's water | the water |
| a navigator setting | the settings |

What does not move it:

- **The code between the bytes and the tile**: `BulletNifLoader`'s collision, the recast mesh builder, Recast and
  Detour. Two versions changed by hand stand for it.
- **An animated collision's pose**: a source is its shape and its object's transform (`MeshSource`), and a compound
  shape's child transforms, which `RecastMeshObject::update` follows, are not in it. A tile stored with a part in one
  pose answers for every pose of that object at that transform.

Off-mesh connections, a door's, are added as a tile is made from the prepared data (`makeNavMeshTileData`) and are not
stored. A tile that generates nothing is not stored either; in the debug log of one start of the integration suite on
the Windows laptop, none of 364 processed jobs did, and 159 ended before generation on an empty recast mesh.

## When it is read and written

Only a job whose change is `add` or `remove` reads the cache (`processInitialJob`); an `update`, an object that moved,
generates and never reads or writes. A read runs on the database worker's thread, ranked by the player's tile; a miss
generates on the updater's thread and queues a write. The game never deletes a row. `openmw-navmeshtool` writes every
tile of the content and, with `--remove-unused-tiles`, deletes the rows of each position but the current one and the
positions outside the content's range; a run that changed any row vacuums.

## What the two caches hold

| | vanilla | Fullrest Repack |
|---|---|---|
| file | 341 MB | 367 MB |
| tiles | 86,569 | 87,640 |
| payload, compressed | 142 MiB | 156 MiB |
| mean `input`, compressed | 561 B | 586 B |
| largest payload | 12 KiB | 12 KiB |
| payloads of 4 KiB or less | 84,623 (98 %) | 83,529 (95 %) |
| positions holding more than one input | 20,131 | 13,492 |
| worldspaces | 65 | 27 |
| shapes | 1,901 | 4,313 |

## What reads SQLite

`components/detournavigator/navmeshdb*` and `navmeshdbutils*`, `apps/navmeshtool`, and the tests of both and of
`components/sqlite3`. Nothing else in the tree. The build fetches the amalgamation or finds the system's
(`OPENMW_USE_SYSTEM_SQLITE3`), and the Linux CI installs `libsqlite3-dev`.

## Upstream's churn

16 upstream commits since 2026-06-01 touch `components/detournavigator`, `apps/navmeshtool` or `components/sqlite3`,
two of them in the week of this note: "Wait for empty navmesh tiles to rebuild" and "Do not post empty and out of range
tiles".
