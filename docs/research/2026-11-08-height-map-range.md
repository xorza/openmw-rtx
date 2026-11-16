# The heights in `_nh` maps — 2026-11-08

Every `_nh` map in the PBR replacer profile (MVR PBR, Tamrielic PBR Armors Pt. 1), the last data folder winning; the top
level's alpha read as a fraction of 255. The methods surveyed are in `2026-11-08-parallax-occlusion-techniques.md`.

## Which maps a surface reads

1,173 of the 1,189 maps are read (16 are atlases no diffuse names), 130 of them by the 134 land textures of the three
masters; each lookup takes `_nh` before `_n`, so 419 `_n` maps beside them go unread. All are BC3, nearly all with full
mip chains, most 1024² or 2048². The Fullrest Repack holds none.

## The heights

| over 1,172 maps that are not flat | median | 10th percentile | 90th percentile |
|---|---|---|---|
| mean | 0.502 | 0.500 | 0.504 |
| lowest texel | 0.082 | 0.000 | 0.341 |
| highest texel | 0.686 | 0.561 | 0.816 |
| 99th less 1st percentile | 0.463 | 0.169 | 0.686 |

- **The maps are made with their mean at the height OpenMW's parallax bias puts on the triangle** (0.500–0.504 on 80% of
  them).
- **The relief goes further down than up**: at the median a map goes 0.42 of the scale under the triangle and 0.19 over
  it; the lowest texel reaches nought on 379 maps, the highest one on 41.
- **The heights are broad shapes**: a median 63% of their variance is over 1/16 of the texture's side and 2% under
  1/128, so a march sees little single-texel detail.

## Depth in texels

At OpenMW's parallax scale, 0.04 of the texture's side, the 1st-to-99th-percentile spread is 20.7 texels deep at level 0
at the median map (6.4 at the 10th percentile, 50.1 at the 90th, 113 at most). A ray at 60° from the normal crosses 1.73
times that depth sideways, at 80° 5.67 times: 36 and 118 texels at the median map, which is what bounds a march's steps.
