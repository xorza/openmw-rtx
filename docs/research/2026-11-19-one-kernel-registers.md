# What one kernel a stage would hold, against the tuples — 2026-11-19

Every trace kernel of every tuple, compiled for the handheld's chip (RDNA 2, Van Gogh) by RADV under Mesa's `amdgpu`
drm-shim on the desk (`2026-11-15-radv-wave-tiers.md` says how), `bench --frames=1 --window=false
--views=seyda-neen-ship --pipeline-code=<dir>` at `5af8ad5b96`; registers are the highest VGPR the final code names,
plus one. A 32-lane wave tier is sixteen registers: 80, 96, 112, 128 run 12, 10, 9, 8 waves an RDNA 2 SIMD.

## A stage's registers by the scene it draws

| stage | room | day exterior | day, normal maps | day, maps and a relief | day, a dark map's alpha | one kernel for every scene |
|---|---|---|---|---|---|---|
| visibility | 96 | 96 | 128 | 128 | 96 | 128 |
| water | — | 144 | 160 | 144 | 160 | 160 |
| direct | 96 | 96 | 112 | 128 | 112 | 128 |
| panes | 192 | 192 | 192 | 192 | 192 | 253 |
| landing | 96 | 96 | 112 | 112 | 112 | 128 |
| bounce | 80 | 80 | 112 | 112 | 96 | 112 |
| unbounced | 80 | 80 | 80 | 80 | 80 | 80 |
| media | — | — | — | 173 | — | 189 |

The bounce, unbounced and media stages read neither the sun nor the moons, and media runs only with the sea; their
columns are the tuples they run.

## What each constant adds

Against the same tuple without it, over every tuple that has a partner:

- **The normal and specular maps (`HAS_MAPS`)**: 32 registers in the eye's stage, 16 to 32 in the bounce and the
  landing, up to 16 in the rest.
- **A dark map's alpha (`HAS_DARK_ALPHA`)**: 16 in the bounce, the landing, `direct`, the water and media, where its read
  stands inside a traversal's candidate test.
- **A relief (`HAS_RELIEF`)**: 16 in `direct`.
- **The sun, the moons, the sea and a mod's maps**: none in a scene with neither the maps nor a dark map's alpha, in
  every stage. Beside the maps, up to 16 in the landing and the eye's stage with a relief, and `panes` 192 to 253.

## The sun, the moons, the sea and a mod's maps folded in

For each set of the other three constants, the least and the most a stage holds over the sixteen combinations of these
four, and what it holds with all four on, which is what one kernel for them would hold:

| stage | no maps, no dark alpha | a dark map's alpha | maps | maps and a relief |
|---|---|---|---|---|
| visibility | 96, all on 96 | 96, 96 | 128, 128 | 112–128, 128 |
| water | 144, 144 | 160, 160 | 160, 160 | 144, 144 |
| direct | 96, 96 | 96–112, 112 | 112, 112 | 128, 128 |
| panes | 192, 192 | 192, 192 | 192–253, 253 | 192, 192 |
| landing | 96, 96 | 96–112, 112 | 112–128, 128 | 112–128, 128 |
| bounce | 80, 80 | 96, 96 | 112, 112 | 112, 112 |
| unbounced | 80, 80 | 80, 80 | 80, 80 | 80, 80 |

## On AMD's Windows driver

The same `bench` on the Windows machine's integrated GPU (RDNA 2), AMD's driver, `--pipeline-code`, the highest VGPR
the driver's own listing names plus one:

| stage | room | day exterior | day, normal maps | day, maps and a relief | day, a dark map's alpha | one kernel for every scene |
|---|---|---|---|---|---|---|
| visibility | 96 | 110 | 128 | 128 | 109 | 128 |
| water | — | 161 | 177 | 177 | 190 | 190 |
| direct | 109 | 112 | 125 | 128 | 125 | 128 |
| panes | 192 | 250 | 255 | 255 | 256 | 255 |
| landing | 96 | 112 | 112 | 112 | 112 | 128 |
| bounce | 96 | 96 | 112 | 112 | 96 | 125 |
| unbounced | 88 | 96 | 96 | 96 | 96 | 96 |

- **The sun, the moons and a mod's maps cost nothing here either, but in `panes` beside the sea**: `panes` with a sea
  holds 192 without the sun and the moons and 238–250 with either, five waves to four, and with a dark map's alpha
  and a sea `direct` holds 112 without them and 127 with them, nine waves to eight.
- **The sea costs a tier where it has no stage of its own**: the eye's stage 96 to 110, the landing 96 to 112, `panes`
  192 to 238–250 with the sun or the moons, and the ambient's stage 88 to 96 within its tier. A room pays it under one
  kernel. RADV holds the sea at no register in a scene without the maps.

## The sun, the moons and a mod's maps folded in

Their constants gone, the sun's and the moons' tests read off the frame block (`sunUp`, `moonsUp`) and a mod's maps
behind a material's bit (`MATERIAL_MOD_MAPS`): every stage's kernel of every remaining tuple under the drm-shim holds
exactly what the old tuple with all three on held, and a scene with neither the maps nor a dark map's alpha holds what
its leanest tuple did, in every stage. 16 tuples from 128, 11 compiled behind the whole one from 95. `shot
--views=all`: no picture of 74 moved past the denoiser's noise; ten repeat pairs identical; `check` clean.

On AMD's Windows driver at `d3503eccd0`, every kernel of every remaining tuple holds what the old tuple with all three
on held, within a few registers and never across a tier, so what the sun and the moons cost beside the sea is paid by
every scene with a sea: `panes` in a room with water (Vivec's canals) or a moonless night by the sea 192 to 243, five
waves to four, and `direct` with a dark map's alpha by the sea 112 to 127, nine to eight. A room with no water, a day
or a night by the sea run as they did.

## Step 1 against master, timed

`bench --window=false --size=960x540`, the default suite, master (`5af8ad5b96`) and `d3503eccd0` interleaved twice
after a throwaway leg, medians of the two legs' medians:

| machine | content | place | frame | stages past the legs' spread |
|---|---|---|---|---|
| handheld, RADV | vanilla | ship, noon | 47.19 → 47.29 ms | `direct` +1.4% |
| handheld, RADV | vanilla | guild | 28.21 → 28.40 ms | `direct` +1.1%, landing +1.3% |
| handheld, RADV | Repack | ship, noon | 63.44 → 64.44 ms | landing 8.36 → 9.16 ms, +9.6% |
| Windows, AMD | vanilla | ship, noon | 34.85 → 35.44 ms | landing +2.4%, `direct` +1.3% |
| Windows, AMD | vanilla | guild | 26.10 → 26.27 ms | `panes` +3.7%, `direct` +1.2% |
| Windows, AMD | Repack | ship, noon | 45.56 → 45.98 ms | landing +2.5% |
| Windows, AMD | Repack | guild | 33.95 → 34.58 ms | `direct` 4.92 → 5.24 ms, +6.7%; `panes` +4.1% |
| Windows, AMD | vanilla | Vivec's canals | 19.37 → 19.52 ms | `direct` +2.4%, `panes` 1.06 → 1.08 ms |

The dawn legs moved within 1% on both machines. The handheld's landing on the Repack at noon is the moons' code in a
kernel that held none (112 to 128); the canals' `panes`, 192 to 243 registers, costs 2%; the rest moved with no
register count changed.

## The sea on AMD's Windows driver, piece by piece

Each kernel built here from the working tree, copied over the Windows tree's at `fbc480cc03` (the same source builds
them byte for byte there: every listing of a run on the copies matched the tree's own), and `--pipeline-code` taken.
Against the sea's tuples at `fbc480cc03`:

| taken out of the sea's kernels | eye's stage | landing | `panes` | `direct`, dark alpha | ambient's |
|---|---|---|---|---|---|
| nothing | 110 | 112 | 243 | 127 | 96 |
| the depth under water (`waterOver` nought) | 110 | 110 | 192 | 112 | 89 |
| the eye's stop at the sea and its hand-off to the water's stage | 96 | — | — | — | — |
| the sea a runtime test of the water level, one kernel | 110 | 125 | 243 | — | 96 |

- **The depth under water is what `panes`, `direct` and the ambient's stage hold for the sea**: the light that reaches
  a point under the surface, the caustic and the column.
- **The eye's stage holds its 14 for stopping at the sea at all.** Handing the sea on before it is resolved, the
  later stages told no water reaches them, moved nothing; only no stop at the sea took it back.
- **This driver's register count is not a function of what a kernel computes.** The sea a literal `false` in place
  of the constant set false, the same code once folded: the landing holds 110 where the constant's room kernel holds
  96, and the landing with a dark map's alpha 112 where it holds 126. A tier here moves with the shape of the module
  the driver is handed as much as with what it holds live, so a constant's cost on this driver is read in time, not
  in registers alone.

## The sea a runtime test, timed

The sea's constant replaced by a test of the water level (`!isinf(frame.mWaterLevel)`), every other line as at
`fbc480cc03`; `bench --window=false --size=960x540`, vanilla, interleaved twice after a throwaway leg, medians:

| place | handheld, RADV | Windows, AMD |
|---|---|---|
| ship, noon | frame +0.4%; water 0.97 → 1.09 ms | frame +1.2%; landing 4.28 → 4.55 ms |
| ship, dawn | frame +0.3%; water 0.95 → 1.07 ms | frame +0.8%; landing 5.08 → 5.40 ms |
| mages guild | frame +0.3%; `direct` +1.7% | frame +4.2%; landing 3.86 → 4.75 ms, `panes` 0.83 → 1.06 ms |
| Seyda Neen customs | frame +0.3%; `direct` +1.7% | frame +4.1%; landing 2.11 → 2.63 ms |
| Vivec's canals | frame +0.1%; `direct` +2.0% | frame +4.4%; landing 2.16 → 2.79 ms, `panes` 1.08 → 1.29 ms |

A room pays the sea's code it never runs on AMD's Windows driver, a quarter on its landing and `panes`; the
handheld pays it in the water's own stage, an eighth.

## Where a dark map's alpha falls below solid

A probe at `MaterialResolver`'s fact (`Material::mDarkBelowSolid`, since gone) over `scene --views=all`, 74 places:
vanilla none, the PBR stack none, theybleed's list none, and the Fullrest Repack one material on three placements,
`textures/glow/tex02_dark1.dds` blended with no cut over the dark map `textures/glow/tex00.dds` on texture unit 1,
512 by 128 against the diffuse's 1024. The places are a sample, not a tour.

## A dark map's alpha out of coverage

Its read gone from the candidate test, every kernel of the eight tuples left holds what the same tuple without a dark
map's alpha held before, under the drm-shim for the handheld's chip exactly, and on AMD's Windows driver at or under
it (the kernels built here with the binary's constant numbering, copied over the Windows tree's): the water's stage
with the maps 177 to 161, `panes` beside the sea and a relief 248 to 247, the ambient's stage beside the sea 96 to 95.
`shot --views=all`: no picture of 74 moved; ten repeat pairs identical.

## What a relief holds in `direct`

Under the drm-shim for the handheld's chip, `--shader-source`, against `b010dfe9c1`'s `direct` with the maps and a
relief, 128 registers (112 without a relief):

| taken out | `direct`, maps and a relief |
|---|---|
| nothing | 128 |
| the march toward the light (`reliefLit`), for the held lamps and the sky's source | 112 |
| the held lamps' march alone | 128 |
| the sky source's march alone | 128 |
| neither; the map's frame held constant, so the site carries no tangents | 128 |

The eye's march, which the trace runs and stores (`STAGE_RELIEF`), holds no register there: the eye's stage stands at
128 with the maps, with a relief or without, on RADV and on AMD's Windows driver (128 and 127).

## What the maps hold in the bounce and the landing

Under the drm-shim for the handheld's chip, `--shader-source`, against `92ea747ce4`, each change made in the one
stage alone:

| taken out | bounce, maps | landing, maps | landing, sea and maps |
|---|---|---|---|
| nothing | 112 | 112 | 128 |
| the eye's normal map, where the bounce resolves the eye's hit | 96 | — | — |
| and the eye's specular map | 96 | — | — |
| the landing's normal map | — | 112 | 128 |
| and the landing's specular map | — | 112 | 112 |
| the landing's lobe, its normal map with it, the specular map read for the metal and the occlusion | — | 96 | 96 |
| the same, the lobe's albedo added to the diffuse | — | 96 | 96 |
| every map the landing reads, the maps' constant false in its module | — | 96 | 96 |

- **The bounce's maps are the eye's hit's**: its normal map 16 registers and its lobe, the bounce it draws, the other
  16.
- **The landing's are its own lobe**: the normal map alone moves nothing, and with the lobe gone every tuple holds
  what it held without the maps, whether the lobe's light is dropped or made diffuse.
- **On a high-end NVIDIA laptop GPU** the landing holds 96 registers and 384 bytes of shared memory in every tuple,
  where its tuples with the maps held 128 and none. Its glow taken past the gather, which held it through, took
  1408 bytes.
- **On AMD's Windows driver** (the kernels built here, copied over the Windows tree's at `92ea747ce4`) the landing
  with the maps holds 96 where it held 112, and beside the sea and a relief 112 where it held 127; nothing else moved.
- **The picture**: `shot --views=all` on vanilla moved none of 74; on the PBR stack all 37 moved in their noise, each
  picture's mean within 0.64%, the rooms brighter by up to that, where a metal's bounce now carries its lobe's light.

## The tiles each fact stands on

A probe painting each pixel's solid by whether its material wears a normal or specular map (or its ground's first
layer a normal map), whether it carries a relief (`MATERIAL_PARALLAX`), and whether the eye's path met the sea,
`shot --views=all --show=specular --exposure=1` at `34533cd2c0`, read as the share of 8 by 4 tiles holding any pixel
of each (the sea's own pixels light the other two a little, so beside water those overstate):

| content | maps, median (range) | relief, median | sea, median (range) |
|---|---|---|---|
| vanilla | — | — | 8% (0–100) |
| the Fullrest Repack | 6% (0–89) | 1% | 1% (0–99) |
| the PBR stack | 99.5% (1–100) | 95% | 6% (0–100) |

- **The Repack wears its maps on a few tiles**: none in most rooms (the guild, the customs office, Wolverine Hall, a
  tomb), 87–89% in two caves its mods retexture, and outdoors no more than the sea's own pixels light; yet every one
  of its frames runs the tuple with the maps, the eye's stage at 128 registers on every tile.
- **The PBR stack wears them everywhere**, a relief on most tiles.
- **The sea stands on none of a room's tiles and on 5–99% of a view by the sea.**

## The trace without its resolve

The eye's stage with what its peel ends on stored and not resolved (the surface it lights left as its hit, a
measure of the trace alone), under the drm-shim for the handheld's chip: 96 registers in every tuple, where the maps'
tuples hold 128; the water's stage 128 in every tuple, where it holds 144–160. The peel still reads a layer's normal
map for its relief's shift.

## The eye's shading in `direct`

The trace storing its solid's hit and `direct` resolving it, marching its relief and writing the eye's channels,
under the drm-shim for the handheld's chip against `34533cd2c0`:

| stage | room, no maps | maps | maps and a relief |
|---|---|---|---|
| visibility | 96 → 96 | 128 → 96 | 128 → 96 |
| water (with a sea) | 144 → 144 | 160 → 144 | 144 → 144 |
| direct | 96 → 96 | 112 → 128 | 128 → 144 |

- **The resolve with the maps holds 128 wherever it stands**: `direct` with its light taken out, the resolve and the
  eye's channels alone, holds 128 with the maps, as the trace did, so a shading stage of its own would hold it too.
- **A relief's tier is its march's depth** (`Surface::mReliefMarched`): kept to be stored, it stands through the rest
  of the resolve; stored as nought, the tuple holds 128. Carried into the world there or past the rays, the same.
  The eye's motion measured to the relief's texel costs nothing placed past every store.
- **On a high-end NVIDIA laptop GPU** the trace holds 80 registers and 1152 bytes of shared memory with no sea and
  94 with one, in every tuple, where the maps' held 128 and 2688; the water's stage 128 and 640 bytes in every
  tuple; `direct` within its ceilings, 96 to 127 registers.

## The eye's shading in `direct`, timed

`bench --window=false --size=960x540`, the default suite, `34533cd2c0` and `c17041b1bc` interleaved twice after a
throwaway leg, medians of the two legs' medians:

| machine | content | frame | trace | `direct` | water |
|---|---|---|---|---|---|
| handheld, RADV | vanilla | −0.7% at every place | −7 to −11% | +1 to +2% | −11% |
| handheld, RADV | Repack | −0.5 to −1.7% | −19 to −20% | +10 to +11% | −19% |
| Windows, AMD | vanilla | −0.1 to −0.6% | −6 to −7% | −4 to +3% | −13 to −15% |
| Windows, AMD | Repack | −0.1 to +0.3% | −8 to −13% | +8 to +10% | −12% |

The trace's tier taken off the maps' tuples outweighs `direct`'s taken on, on the handheld; on AMD's Windows driver
the two come out even with the Repack, whose rooms wear the maps on few tiles.

## The trace with the maps in every tuple

The trace and the water's stage compiled with the maps whatever the frame's tuple, under the drm-shim for the
handheld's chip: the trace 96 registers and the water's stage 144 in every tuple, as before; 111 pipelines made
where 114 were, the trace's two tuples with neither the maps nor a relief and the water's one running the pipelines
with the maps.
