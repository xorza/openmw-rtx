# Physically based shading

The model is glTF 2.0 metal-roughness (`brdf.h`). Check that the code is that model,
the same wherever it is evaluated.

## Lobe

- `α = roughness²`, squared exactly once per reader, floored at `ROUGHNESS_FLOOR`.
- `D = α² / (π ((n·h)²(α² - 1) + 1)²)`.
- `V` is height-correlated Smith with the `1 / (4 n·v n·l)` folded in; a separate division blows up at grazing.
- Schlick's `F` at `v·h`, not `n·v`; `F90 = saturate(50 F0.g)`.
- Negative `n·v` or `n·l` is handled before any division (`facingRay`, `SHADING_MIN_FACING`).
- Visible-normal draws leave `F · G2/G1` in the weight; a draw from `D` leaves an unbounded `(v·h)/(n·v)`.
- The host table, `gather`, the bounce and the water use one `D`, `V`, `F` and `α`.

## Energy

- Multiple scattering: the lobe is scaled by `1 + F0 (1/E - 1)` (Kulla–Conty, Turquin).
- The diffuse base is weighted by what the lobe leaves (`1 - F`, or `1 - E_spec`); `bounceDraw` uses the same shares as `gather`.
- Metals: `albedo = base (1 - metal)`, `F0 = mix(0.04, base, metal)`.
- Lambert is `albedo / π`, and π cancels exactly once against the light's units.
- No reflectance above one reaches a bounce (check de-lit albedo).
- Emission is added in one place.

## Normals

- Decode `2t - 1`; a two-channel map rebuilds `z = √max(0, 1 - x² - y²)`; renormalise.
- Tangent orthogonalised to `n`; bitangent `cross(n, t) · sign`. The frame matches OpenMW's rasterizer (`TangentSpaceGenerator`), not MikkTSpace.
- A light the shading normal faces and the geometric normal does not must not leak (`mClosed`, `litCosine`).
- Minified normal maps widen the roughness, or smooth mapped surfaces sparkle in motion.
- Parallax shifts before every read on that coordinate set; cutouts never shift.

## Texture level

- `λ = ½ log2(t_a / p_a) + log2|W| - log2|n̂·d̂|` (ray cones, Akenine-Möller 2021).
- Curved surfaces widen the cone by `-2k|w| / (n̂·d̂)`.
- A level-0 read on a far surface is deliberate and says so.
- Every read takes the upscaler's bias from `Reconstruction::resolve`.

## Colour and units

- Light is linear; display encoding happens once, at the end.
- Luminance of linear sRGB is `0.2126, 0.7152, 0.0722`, not Rec. 601's weights.
- Sun, moons, sky, lamps and emission share one unit; a disc's irradiance is radiance × solid angle.

## Water and sky

- Water `F0 ≈ 0.020` (IOR 1.33); total internal reflection handled; Beer–Lambert per channel.
- `pathEnd` stands in for untraced bounces and never adds what the traced hemisphere gathered.
