# The term a mapped normal's light fades by — 2026-11-07

A high-end NVIDIA laptop GPU, release builds, `shot --upscale=off --maps=metal-roughness` on a PBR replacer profile
(MVR PBR and Tamrielic Armors). Hard edges are the share of pixels whose display luminance steps by over 0.25 in two
pixels, across and along, summed.

## The three

- `chiang`: Chiang, Li and Burley 2019, `g = min(1, base / (mapped * d))` smoothed by `g + g^2 - g^3`.
- `ggx`: Conty Estevez, Lecocq and Stein 2019 in the form Cycles writes (`bump_shadowing_term`): GGX's shadowing at
  the base's cosine, from an `a` of `tan^2(d) / 8`. Here on the diffuse half and the lobe, where Cycles softens the
  diffuse half alone.
- `none`: no term, the shadow rays' lift alone (`Surface::mLift`).

## Why `chiang` drew patches

For a mapped cosine `x` and a base's cosine `e`, the light is `x * (g + g^2 - g^3)` with `g = e / x` (the normals'
cosine near one). Where `x` is much over `e` this comes to `e`, whatever `x` is; at `x = 2e` it is `1.25 e`; between
`e` and nought it falls to nought. A sun that grazes a wall makes `e` small, so the map's relief goes flat, a rim stands
a quarter over beside the mapped terminator, and the light ends within a sliver of the normal.

## At a stone wall a few units away

`seyda-neen-wall-noon`'s camera, 2560x1440.

| hour | hard edges, `chiang` | `ggx` | `none` | mean display, `chiang` | `ggx` | `none` |
|---|---|---|---|---|---|---|
| 9:00 | 0.89% | 0.58% | 0.24% | 0.346 | 0.348 | 0.355 |
| 11:50 | 0.91% | 0.04% | 0.31% | 0.297 | 0.316 | 0.341 |
| 14:00 | 0.00% | 0.00% | 0.00% | 0.234 | 0.234 | 0.234 |

With the bounce off and with the denoisers off, `chiang`'s patches stood, and they were gone at night.

## The facet line

`seyda-neen-fence` at other hours. Linear luminance over 24 pixels each side of the line between two facets.

| hour | `chiang` | `ggx` | `none` |
|---|---|---|---|
| 15:00 | 2.95 | 3.48 | 5.16 |
| 17:00 | 1.30 | 1.37 | 1.55 |

## Normals square or more apart

The first `ggx` kept `chiang`'s nought where the two normals stand square or more apart. MVR PBR's maps go there: they
paint texels whose own `z` is under nought: 2352 of the 3829 `_n` and `_nh` maps hold some, and 948 hold over 1% of
their texels. The wall's own map was not identified. Beside it the term keeps `2c / (c + 1)`, so a map crossing the square drew a hard black
patch under either term. `none` drew none of them. Keeping `2c / (c + 1)` past the square removes them, where Cycles
returns nought for a diffuse closure on such a normal (`cosNsI * cosNsN * cosNI < 0`).

At a camera a few units from the wall, 9:58, 2560x1440, hard edges and mean display:

| `chiang` | `ggx`, nought past the square | `ggx`, kept past the square | `none` |
|---|---|---|---|
| 2.24%, 0.360 | 1.89%, 0.366 | 1.15%, 0.364 | 1.43%, 0.369 |

At `seyda-neen-wall-noon` the kept form reads 0.03% and 0.315. The facet line reads 3.48 at 15:00 and 1.38 at 17:00. The
numbers under "At a stone wall" and "The facet line" and "Cost" are the first `ggx`.

## Elsewhere

At `ald-ruhn`, `balmora`, `dagon-fel`, `vivec`, `seyda-neen-boulder`, `seyda-neen-boulder-close`, `seyda-neen-customs`,
`seyda-neen-fence` and `seyda-neen-ship`, mean display luminance differs by at most 0.002 between the three.

## Cost

`ggx` against `chiang`, the middle of three 15-second runs at the shipped upscale, back to back after a warm-up, in ms.

| place | median, `chiang` | median, `ggx` |
|---|---|---|
| `seyda-neen-fence` | 5.87 | 5.89 |
| `seyda-neen-ship` | 6.45 | 6.43 |
| `balmora` | 5.34 | 5.39 |
| `addamasartus` | 5.47 | 5.53 |
| `vivec-canalworks` | 5.07 | 5.00 |

One `balmora` run of `ggx` read 6.19 and a p99 of 12.12, and the other two 5.37 to 5.39 and 7.45 to 7.68. Noise holds to
0.03 at the frame, and fireflies at `balmora` read 0.07 in a thousand against 0.03.

## Sources

- [Chiang, Li and Burley, "Taming the Shadow Terminator"](https://media.disneyanimation.com/technology/publications/2019/TamingtheShadowTerminator.pdf), which says Estevez et al. soften the terminator less.
- [Cycles `bsdf.h`](https://github.com/blender/cycles/blob/main/src/kernel/closure/bsdf.h), `bump_shadowing_term`.
- [MoonRay's shadow terminator fixes](https://docs.openmoonray.org/user-reference/how-to-guides/shadow-terminators/), which offer both.
