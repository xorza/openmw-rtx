# A mod's detail, decal, bump and gloss maps in the kernels — 2026-11-15

A low-end AMD integrated GPU (RDNA 2) under AMD's Windows driver. Registers are `numUsedVgprs` of every kernel of
every tuple, which `RtxVisibilityKernelsTest.noKernelSpillsPastItsCeiling` makes; waves are an RDNA 2 SIMD's 1024
registers in steps of 16 (`2026-11-13-amd-integrated-register-tiers.md`). "Unchanged" for a tuple's code is
`omw kernels`: the module specialized to the tuple, optimized and digested.

## Where the Repack binds them

Two legs of a Fullrest Repack tour refused 29 surfaces for one of the four: 16 for a bump map, 11 for a detail map,
2 for a decal. Bottles, pots, the slaughterfish and crab meat, a menhir, tanning racks and workbenches: clutter, so
most of the Repack's places hold one, where 10 placements wore a normal or specular map at the ship.

## Forms measured

Kernels of the tuples a scene ran before, against the build before any of it: moved, and of those a tier up and
down, without and then with the normal and specular maps.

| form | without the maps | with the maps |
|---|---|---|
| four fields in `GpuMaterial`, 112 to 160 bytes, under `HAS_MAPS` | 21; 8 up, 1 down | 48; 2 up, 5 down |
| a row of their own (`GpuMaterialMaps`), under `HAS_MAPS` | 20; 8 up | 53; 5 down |
| a row of their own, under `HAS_MOD_MAPS` | 28; 11 up | 37; 2 up, 1 down |

- **A use of a field the frame already loads moves where the optimizer loads the row's fields**, in every tuple: the
  decal's weight read off the caller's `GpuMaterial::mOpacity` reordered the unmapped `direct` tuple's loads, and
  read off its own row it left all 936 tuples' code as it was.
- **A frame block 8 bytes longer, with the new address unread, moved no kernel's registers.**
- **The driver's registers move with code no tuple runs.** With every tuple's specialized code unchanged, 65 kernels
  of the existing tuples moved: 13 up a tier (`direct`, `direct sea` and the four `bounce … dark` 110 to 96; `direct
  moons sea`, `direct sun moons sea`, `direct moons maps` and `direct sun moons maps` 125 to 112; `landing sun sea`,
  `landing moons sea` and `landing sun moons sea` 128 to 112), one down (`panes sun moons maps relief` 192 to 224,
  five waves to four, where its siblings stood at 215 to 246 already), the `unbounced` tuples 84 to 95, all at ten
  waves. The driver is handed the module unspecialized, with the constants beside it.

## The tuples that read them

Against the same tuple without them: six `direct` tuples with the maps 112 to 128 (nine waves to eight), `direct
sun sea` 112 to 125 (nine to eight), the rest of `direct` 125 or 127 to 128;
`panes sun moons maps relief` 224 to 192 (four waves to five); `water sea maps relief` 161 to 177 and the
`unbounced` relief tuples 88 to 96 within their tiers. No kernel past its ceiling.

## Cost of the constant

95 tuples behind the full one, from 47: 648 stage pipelines of which 164 shared, from 324 and 80, and the froxels'
launch for the 16 tuples with neither maps as before. A cold run of the kernels test made 523 pipelines in 221 s.
