# What upstream's navmesh disk cache keys and holds — 2026-11-10

Read from the code at `80a59422ac` and from the `navmesh.db` of the vanilla and the Fullrest Repack profiles. It is what
the content store's navigator tiles replaced.

## The key

A tile is a row of SQLite's `tiles`, found by its worldspace, its position and an `input` blob compared byte for byte:
the recast settings and agent bounds, each cell's water, each heightfield by value, and for each mesh source a shape id
(its path, type and a MurmurHash3 of the whole file) and its object's transform.

What does not move the key:

- **The code between the bytes and the tile**: Bullet's NIF collision, the recast mesh builder, Recast and Detour; two
  versions changed by hand stand for it.
- **An animated collision's pose**: a source is its shape and its object's transform, and a compound shape's child
  transforms are not in it, so a tile stored with a part in one pose answers for every pose.

Only a job whose change is `add` or `remove` reads the cache; an `update` generates and never reads or writes. The game
never deletes a row; `openmw-navmeshtool --remove-unused-tiles` does, and vacuums.

## What the two caches hold

| | vanilla | Fullrest Repack |
|---|---|---|
| file | 341 MB | 367 MB |
| tiles | 86,569 | 87,640 |
| payload, compressed | 142 MiB | 156 MiB |
| payloads of 4 KiB or less | 98% | 95% |
| positions holding more than one input | 20,131 | 13,492 |

## What reads SQLite

The navigator's database files, `apps/navmeshtool`, and their tests; nothing else in the tree. Upstream touched those
folders in 16 commits between 2026-06-01 and this note.
