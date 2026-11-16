# The term a mapped normal's light fades by — 2026-11-07

A high-end NVIDIA laptop GPU, release builds, `shot --upscale=off --maps=metal-roughness` on a PBR profile (MVR PBR and
Tamrielic Armors). "Hard edges" is the share of pixels whose display luminance steps by over 0.25 in two pixels.

## The three

- `chiang`: Chiang, Li and Burley 2019, `g = min(1, base / (mapped * d))` smoothed by `g + g^2 - g^3`.
- `ggx`: Conty Estevez, Lecocq and Stein 2019 as Cycles writes it (`bump_shadowing_term`): GGX's shadowing at the base's
  cosine, from an `a` of `tan^2(d) / 8`, here on the diffuse half and the lobe.
- `none`: no term, the shadow rays' lift alone.

## Why `chiang` drew patches

For a mapped cosine `x` and a base cosine `e`, `chiang`'s light is `x * (g + g^2 - g^3)` with `g = e / x`: where `x` is
well over `e` it comes to `e` whatever `x` is. A sun grazing a wall makes `e` small, so the map's relief goes flat and
the light ends within a sliver of the normal: hard-edged patches.

| `seyda-neen-wall-noon`, 2560x1440 | hard edges, `chiang` | `ggx` | `none` |
|---|---|---|---|
| 9:00 | 0.89% | 0.58% | 0.24% |
| 11:50 | 0.91% | 0.04% | 0.31% |

The line between two facets at `seyda-neen-fence` at 15:00, linear luminance over 24 pixels each side: `chiang` 2.95,
`ggx` 3.48, `none` 5.16; at 17:00 1.30, 1.37 and 1.55.

## Normals square or more apart

MVR PBR's maps paint texels whose own `z` is under nought: 2,352 of its 3,829 `_n` and `_nh` maps hold some. Where the
two normals stand square or more apart the term must not fall to nought, or a map crossing the square draws a hard black
patch; keeping `2c / (c + 1)` there removes them (Cycles returns nought for a diffuse closure on such a normal).

## Elsewhere and cost

At nine other places the mean display luminance differs by at most 0.002 between the three. `ggx` against `chiang`:
frame medians within the runs' spread, noise within 0.03.

## Sources

- [Chiang, Li and Burley, "Taming the Shadow
  Terminator"](https://media.disneyanimation.com/technology/publications/2019/TamingtheShadowTerminator.pdf)
- [Cycles `bsdf.h`](https://github.com/blender/cycles/blob/main/src/kernel/closure/bsdf.h), `bump_shadowing_term`
- [MoonRay's shadow terminator fixes](https://docs.openmoonray.org/user-reference/how-to-guides/shadow-terminators/)
