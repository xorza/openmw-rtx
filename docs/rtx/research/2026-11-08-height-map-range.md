# The heights in `_nh` maps — 2026-11-08

Every file under `textures/` whose stem ends in `_nh`, in the data folders of two profiles. The last data folder wins,
as in the game. The alpha of the top level was read as stored, as a fraction of 255. A hand decode of five BC3
alpha blocks of one map agreed with the reader to within one level. The surveys of methods are in
`2026-11-08-parallax-occlusion-techniques.md`.

## Which maps a surface reads

| | count |
|---|---|
| `_nh` maps, PBR replacer profile (MVR PBR, Tamrielic PBR Armors Pt. 1) | 1189 |
| from MVR PBR | 1185 |
| from Tamrielic | 4 |
| with no diffuse of their name in the profile, so read by no surface | 16, all atlases |
| read | 1173 |
| of those, beside an `_n` of the same name, which is then not read | 419 |
| of those, of a land texture of the three masters' `LTEX` records | 130 of 134 land textures |
| `_nh` maps, Fullrest Repack profile | 0 (four icons whose names end in `_nh`) |

- Each lookup takes `_nh` before `_n`: `Shader::AutoMaps` for an object, `ESMTerrain::Storage` for land.
- The format is BC3 for all 1189, with full mip chains on 1188. The common sizes are 2048² (408) and 1024² (400).
- No diffuse comes from an archive: MVR PBR ships each one loose.

## The heights

Over the 1172 read maps that are not flat. One land map, `tx_bm_rock_01_nh`, is flat at 128/255.

| | median | 10th percentile | 90th percentile | least | most |
|---|---|---|---|---|---|
| mean | 0.502 | 0.500 | 0.504 | 0.226 | 0.872 |
| lowest texel | 0.082 | 0.000 | 0.341 | 0.000 | 0.749 |
| highest texel | 0.686 | 0.561 | 0.816 | 0.518 | 1.000 |
| highest less lowest | 0.624 | 0.243 | 0.773 | 0.141 | 0.988 |
| 99th less 1st percentile | 0.463 | 0.169 | 0.686 | 0.071 | 0.953 |
| deviation | 0.103 | 0.038 | 0.163 | 0.016 | 0.299 |
| distinct levels | 160 | 61 | 198 | 26 | 253 |

- 1157 maps have a mean over 0.5, and 15 under it. The mean is 0.500 to 0.504 on 80% of maps, so the maps were
  made with their mean at the height that `PARALLAX_BIAS` puts on the triangle.
- The relief goes further down than up: the lowest texel reaches nought on 379 maps, and the highest reaches one on
  41. At the median, a map goes 0.42 of the scale under the triangle and 0.19 over it.
- The land maps differ little from the rest: a median spread of 0.482 against 0.459, and a mean of 0.500 or more on
  all 129.
- 270 maps have a spread under 0.25 between the 1st and 99th percentiles, and 491 have 0.5 or more.

## The scale of the heights

The share of the variance of the alpha at each scale, as for the specular maps' blue channel. Box-filtered levels
split the variance exactly, so the three shares sum to one on each map.

| band, in fractions of the texture's side | median share | 10th percentile | 90th percentile |
|---|---|---|---|
| under 1/128 | 0.021 | 0.008 | 0.071 |
| 1/128 to 1/16 | 0.343 | 0.192 | 0.544 |
| over 1/16 | 0.634 | 0.395 | 0.795 |

The heights are broad shapes. Almost none of their variance is in single texels.

## Depth in texels

`PARALLAX_SCALE` is 0.04 of the texture's side, so the 1st-to-99th-percentile spread is `0.04 * spread * width`
texels deep at level 0: median 20.7, 10th percentile 6.4, 90th percentile 50.1, most 113. The land maps' median is
38.2, since more of them are 2048 wide. A ray at an angle `θ` from the normal crosses `tan θ` times that depth
sideways: 1.73 times at 60° and 5.67 times at 80°, so 36 and 118 texels at the median map.
