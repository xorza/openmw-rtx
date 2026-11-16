# What marching a relief costs and how it moves the noise — 2026-11-08

The PBR replacer profile with Tamrielic PBR Dwemer Ruins and Landscape Remastered 2.0 added, release builds.
`--relief` is the A/B: on marches an `_nh` map's relief, off shifts it once as the rasterizer does.

## Cost

With the relief shadows, the ground stack's march and the landing's shift in, `bench --seconds=10`, a warm-up leg then
on, off, off, on:

| place | high-end NVIDIA laptop GPU, frame on / off | AMD handheld (RADV), frame on / off |
|---|---|---|
| `seyda-neen-wall-noon`, a close wall | 6.53–6.61 / 6.35–6.36 ms | 186.1–187.1 / 177.9–178.1 ms |
| `dagon-fel` | 5.92–5.99 / 5.69–5.78 | 104.7–105.6 / 102.1 |
| `vivec` | 7.08–7.12 / 7.09–7.11 | 200.2–200.4 / 195.0–195.2 |

3–5% of a close wall's frame, almost all of it in the trace and the direct light, which march and shadow every relief
texel the eye meets. A bounce's landing marched rather than shifted cost the close wall a further 0.05–0.1 ms; a
landing shifts. The march's arrays are held in registers: indexed by a count the hit
decided, they spilled 32 bytes a lane.

## The step cap

`RELIEF_STEPS` at 16, 32 and 64: the close wall's frame 6.44–6.47, 6.52–6.53 and 6.68–6.69 ms; the ground at a walk's
distance did not move. The cap reaches only a march whose crossing passes it, which a close wall's grazing texels do.

## Noise

`noise --ab=relief` at the close wall: the march first added fireflies (22 against 11 in a thousand on a strafe), from a
history fetched by the triangle's motion, which the texel the march shows does not follow. Measured to that texel, most
of them went (14 against 11 on a strafe, 8 against 8 walking); in the guild, at `vivec` and over ground the march stood
within 0.06 of its absence on every figure. A leg's first reference can show the strafe at another place, so each
switch is read as the second leg.
