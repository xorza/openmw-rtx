# The handheld's content pool refuses small takes with room free in pieces — 2026-11-16

A handheld with a low-end AMD integrated GPU (RDNA 2) and 16 GB of unified memory, its video heap's budget 6144 MiB,
release builds, `tour --expected=<content>`. Every refused content take logs what it asked and how each content pool
stood (`RefusedTake`), and what rested beside a mesh left out.

## The two tours

| | Fullrest Repack, ahead of `3ab057cd39` | PBR replacers, `ee7852dc50` |
|---|---|---|
| refused takes | 859, from leg 17 on | 1212, from leg 2 on |
| what was refused | structures of 0.8–1.6 MiB (835), one of 8 MiB, textures of 0.4–5.4 MiB (23) | structures of 0.8–8 MiB (1110), textures of 0.4 MiB (102) |
| free in the pool's blocks at a refusal | 78–92 MiB | up to 136 MiB |
| the largest range free | 0.8–4.7 MiB | 0.2–0.8 MiB |
| what rested at a mesh left out | 2056–2112 textures on 904–910 MiB, 7031–7731 meshes on 1238–1536 MiB | 536–605 textures on 573–624 MiB, 474–499 meshes on 101–103 MiB |
| faults | a texture's stand-in, a mesh left out, `textures-readable` 139 times | a texture's stand-in, a mesh left out |

Every refusal was a take in one of content's 64 MiB pool blocks, which held the structures' storage beside the textures:
the pool of the second content type was empty on both. No new block was made: the process's usage stood under the
budget by 0.9–1.7 GiB, which the ceiling keeps for every use ahead of content to be made again (`MemoryAllocator`'s
`ceilingOf`).

## What it says

- **The room was there, cut too small.** Free bytes in the blocks would have held every refused take several times over;
  no range held one. The room an arrival's eviction was sized against counted those bytes whole
  (`MemoryAllocator::getRoom`), so it read as room and nothing that rested was let go for what waited.
- **Structures were in the pools' blocks already**, so holding their storage to half a block, the step the plan named
  for a structure refused for room beyond the blocks, changes nothing. The storage's blocks were sized to what each
  build had left to place and given back once empty: short-lived ranges of every size cut between textures that stay.
</content>
</invoke>
